#!/usr/bin/env bash
#
# music-sync.sh — one-way sync of an MP3 library to an 8.3 (FAT, no-LFN) drive,
# renaming every folder/file to numbers and writing a JSON index that maps the
# numeric names back to full song/artist/album metadata.
#
# Built for an ESP32-S3 music player whose firmware is written AGAINST INDEX.JSN.
#
# ---------------------------------------------------------------------------
# INDEX.JSN format (the firmware contract) — written at the destination root:
#
#   {
#     "schema": 1,
#     "generated_at": "2026-06-07T20:30:00Z",
#     "music_root": "MUSIC",          // folder under the dest holding all audio
#     "track_count": 7024,
#     "tracks": [
#       {
#         "path":         "MUSIC/0001/0001/0005.MP3",  // relative to INDEX.JSN
#         "title":        "Hurt",
#         "artist":       "Johnny Cash",
#         "album_artist": "Johnny Cash",
#         "album":        "American IV: The Man Comes Around",
#         "track":        5,
#         "disc":         1,
#         "year":         2002,
#         "genre":        "Country",
#         "duration":     216.41,      // seconds (float)
#         "size":         6925312      // bytes
#       }
#       // ...
#     ]
#   }
#
# - All names on the card are valid 8.3 (digits + ".MP3") so macOS never has to
#   invent a "NAME~1" alias that the ESP32 would see instead.
# - No field is ever null: missing tags fall back to "" / 0 / filename / folder.
# - "path" is relative to the directory that contains INDEX.JSN.
#
# Full field-by-field reference for firmware authors: see INDEX-SCHEMA.md next
# to this script. (That doc is NOT copied to the card — it's repo reference only.)
# ---------------------------------------------------------------------------
#
# Usage:
#   music-sync.sh --dest /Volumes/ESP32SD [--source DIR] [--dry-run]
#                 [--no-delete] [--rebuild] [--width N] [--jobs N]
#
#   --dest DIR     (required) SD-card mount point (or any dir on it).
#   --source DIR   library root to sync from.
#                  default: /Users/rich/Music/Music/Media.localized/Music
#   --dry-run      show what would change; touch nothing.
#   --no-delete    additive only: never delete from the card (no true mirror).
#   --rebuild      ignore saved state; re-extract + re-copy everything.
#   --width N      digits per numeric name (default 4 -> 0001; must be 1..8).
#   --jobs N       parallel ffprobe workers on first scan (default 4).
#   -h, --help     this help.
#
# State (number assignments + change-detection) is kept on the Mac at
#   ~/.music-sync/state.json
# Deleting that file forces a full rebuild on the next run.
#
# Requires: ffprobe (brew install ffmpeg) and python3.

set -euo pipefail

# ----- defaults -------------------------------------------------------------
SOURCE="/Users/rich/Music/Music/Media.localized/Music"
DEST="/Volumes/UNTITLED"
DRY_RUN=0
DO_DELETE=1
REBUILD=0
WIDTH=4
JOBS=4
MUSIC_ROOT="MUSIC"
INDEX_NAME="INDEX.JSN"
SKIPPED_NAME="SKIPPED.LOG"
STATE_DIR="${HOME}/.music-sync"
STATE_FILE="${STATE_DIR}/state.json"

usage() { sed -n '2,60p' "$0" | sed 's/^# \{0,1\}//'; }

# ----- arg parsing ----------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dest)       DEST="${2:-}"; shift 2 ;;
    --source)     SOURCE="${2:-}"; shift 2 ;;
    --dry-run)    DRY_RUN=1; shift ;;
    --no-delete)  DO_DELETE=0; shift ;;
    --rebuild)    REBUILD=1; shift ;;
    --width)      WIDTH="${2:-}"; shift 2 ;;
    --jobs)       JOBS="${2:-}"; shift 2 ;;
    -h|--help)    usage; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; echo "try: $0 --help" >&2; exit 2 ;;
  esac
done

# ----- validation -----------------------------------------------------------
die() { echo "error: $*" >&2; exit 1; }

[[ -n "$DEST" ]]   || die "--dest is required (the SD card mount). See --help."
[[ -d "$SOURCE" ]] || die "source directory not found: $SOURCE"

[[ "$WIDTH" =~ ^[1-8]$ ]] || die "--width must be an integer 1..8 (got: $WIDTH)"
[[ "$JOBS"  =~ ^[1-9][0-9]*$ ]] || die "--jobs must be a positive integer (got: $JOBS)"

command -v python3 >/dev/null 2>&1 || die "python3 not found on PATH."

if ! command -v ffprobe >/dev/null 2>&1; then
  cat >&2 <<'MSG'
error: ffprobe not found.

  ffprobe reads the song tags (title/artist/album/duration). Install it with:

      brew install ffmpeg

  then re-run this command.
MSG
  exit 1
fi

# Don't litter the FAT card with ._* AppleDouble / .DS_Store resource forks.
export COPYFILE_DISABLE=1

# Ensure dest + state dir exist (cheap; safe even on dry-run for the state dir).
if [[ "$DRY_RUN" -eq 0 ]]; then
  mkdir -p "$DEST" || die "cannot create/access dest: $DEST"
  [[ -w "$DEST" ]] || die "dest is not writable: $DEST"
fi
mkdir -p "$STATE_DIR"

# ----- hand off to the Python engine ---------------------------------------
export MS_SOURCE="$SOURCE"
export MS_DEST="$DEST"
export MS_DRY_RUN="$DRY_RUN"
export MS_DO_DELETE="$DO_DELETE"
export MS_REBUILD="$REBUILD"
export MS_WIDTH="$WIDTH"
export MS_JOBS="$JOBS"
export MS_MUSIC_ROOT="$MUSIC_ROOT"
export MS_INDEX_NAME="$INDEX_NAME"
export MS_SKIPPED_NAME="$SKIPPED_NAME"
export MS_STATE_FILE="$STATE_FILE"

python3 - <<'PY'
import concurrent.futures as cf
import datetime as dt
import json
import os
import posixpath
import shutil
import subprocess
import sys

# ---- config from environment ----------------------------------------------
SRC        = os.path.abspath(os.environ["MS_SOURCE"])
DEST       = os.path.abspath(os.environ["MS_DEST"])
DRY_RUN    = os.environ["MS_DRY_RUN"] == "1"
DO_DELETE  = os.environ["MS_DO_DELETE"] == "1"
REBUILD    = os.environ["MS_REBUILD"] == "1"
WIDTH      = int(os.environ["MS_WIDTH"])
JOBS       = int(os.environ["MS_JOBS"])
MUSIC_ROOT = os.environ["MS_MUSIC_ROOT"]
INDEX_NAME = os.environ["MS_INDEX_NAME"]
SKIPPED_NAME = os.environ["MS_SKIPPED_NAME"]
STATE_FILE = os.environ["MS_STATE_FILE"]

MAXNUM = 10 ** WIDTH - 1

def zpad(n):
    if n > MAXNUM:
        sys.exit(f"error: ran out of {WIDTH}-digit numbers (>{MAXNUM}) in one "
                 f"folder; re-run with a larger --width.")
    return str(n).zfill(WIDTH)

# ---- load state ------------------------------------------------------------
# state = {
#   "version": 1, "width": WIDTH,
#   "dirs":     { "<src rel dir>": "0001", ... },   # number per dir, rel to SRC
#   "counters": { "<parent rel dir>": <int last used>, ... },  # "" = top level
#   "files":    { "<src abs path>": {                # one entry per synced song
#       "num": "0005", "dest": "MUSIC/0001/0001/0005.MP3",
#       "size": int, "mtime": float, "meta": { ...index fields... } } }
# }
def empty_state():
    return {"version": 1, "width": WIDTH, "dirs": {}, "counters": {}, "files": {}}

state = empty_state()
if not REBUILD and os.path.exists(STATE_FILE):
    try:
        with open(STATE_FILE, encoding="utf-8") as fh:
            loaded = json.load(fh)
        if loaded.get("width") == WIDTH:          # width change => renumber
            state = loaded
            for k in ("dirs", "counters", "files"):
                state.setdefault(k, {})
        else:
            print(f"note: --width changed ({loaded.get('width')} -> {WIDTH}); "
                  f"rebuilding numbering.")
    except (json.JSONDecodeError, OSError) as e:
        print(f"warning: could not read state ({e}); starting fresh.")

dirs     = state["dirs"]
counters = state["counters"]
files    = state["files"]

# ---- enumerate source MP3s (+ note skipped audio) --------------------------
def is_hidden(name):
    return name.startswith(".")

mp3s = []          # absolute paths
skipped = []       # relative paths of non-mp3 audio we ignored
SKIP_AUDIO_EXT = {".m4a", ".m4p", ".aac", ".flac", ".wav", ".ogg", ".wma", ".aiff", ".alac"}

for root, dnames, fnames in os.walk(SRC):
    dnames[:] = sorted(d for d in dnames if not is_hidden(d))
    for fn in sorted(fnames):
        if is_hidden(fn):
            continue
        ext = os.path.splitext(fn)[1].lower()
        full = os.path.join(root, fn)
        if ext == ".mp3":
            mp3s.append(full)
        elif ext in SKIP_AUDIO_EXT:
            skipped.append(os.path.relpath(full, SRC))

mp3s.sort()
src_set = set(mp3s)

# ---- numbering helpers -----------------------------------------------------
def ensure_dir(reldir):
    """Assign a number to a source directory (rel to SRC) and its ancestors."""
    if reldir in dirs:
        return
    parent = posixpath.dirname(reldir)
    if parent:
        ensure_dir(parent)
    counters[parent] = counters.get(parent, 0) + 1
    dirs[reldir] = zpad(counters[parent])

def dest_rel_for(rel, filenum):
    """Build 'MUSIC/<dirnums...>/<filenum>.MP3' for a source rel path."""
    parent = posixpath.dirname(rel)
    parts = []
    if parent:
        cum = ""
        for comp in parent.split("/"):
            cum = comp if not cum else cum + "/" + comp
            parts.append(dirs[cum])
    parts.append(filenum + ".MP3")
    return MUSIC_ROOT + "/" + "/".join(parts)

# ---- diff source vs state --------------------------------------------------
adds, updates, unchanged = [], [], []

for full in mp3s:
    rel = os.path.relpath(full, SRC).replace(os.sep, "/")
    st = os.stat(full)
    cur = files.get(full)
    if cur is None:
        parent = posixpath.dirname(rel)
        if parent:
            ensure_dir(parent)
        counters[parent] = counters.get(parent, 0) + 1
        filenum = zpad(counters[parent])
        destrel = dest_rel_for(rel, filenum)
        files[full] = {"num": filenum, "dest": destrel,
                       "size": st.st_size, "mtime": st.st_mtime, "meta": None}
        adds.append(full)
    elif int(cur["size"]) != st.st_size or float(cur["mtime"]) != st.st_mtime:
        cur["size"], cur["mtime"] = st.st_size, st.st_mtime
        updates.append(full)
    else:
        unchanged.append(full)

deletions = [p for p in list(files) if p not in src_set]

# ---- dry-run: report and stop ---------------------------------------------
def show(label, items, render):
    if not items:
        return
    print(f"\n{label} ({len(items)}):")
    for p in items[:20]:
        print("  " + render(p))
    if len(items) > 20:
        print(f"  ... and {len(items) - 20} more")

if DRY_RUN:
    print("DRY RUN — no changes will be made.")
    print(f"  source: {SRC}")
    print(f"  dest:   {DEST}")
    show("ADD",    adds,    lambda p: f"{os.path.relpath(p, SRC)}  ->  {files[p]['dest']}")
    show("UPDATE", updates, lambda p: f"{os.path.relpath(p, SRC)}  ->  {files[p]['dest']}")
    if DO_DELETE:
        show("DELETE", deletions, lambda p: f"{files[p]['dest']}  (source gone)")
    else:
        if deletions:
            print(f"\nDELETE skipped (--no-delete): {len(deletions)} orphan(s) "
                  f"would remain on the card.")
    if skipped:
        print(f"\nSKIP (non-mp3 audio): {len(skipped)}")
    print(f"\nsummary: +{len(adds)} ~{len(updates)} -{len(deletions) if DO_DELETE else 0} "
          f"={len(unchanged)} unchanged, {len(skipped)} skipped")
    sys.exit(0)

# ---- metadata extraction (ffprobe) ----------------------------------------
def first_tag(tags, *keys):
    low = {k.lower(): v for k, v in tags.items()}
    for k in keys:
        v = low.get(k.lower())
        if v not in (None, ""):
            return v
    return None

def parse_int(v):
    if v is None:
        return 0
    s = str(v).strip()
    if "/" in s:          # "5/12" -> 5
        s = s.split("/", 1)[0]
    num = ""
    for ch in s:
        if ch.isdigit():
            num += ch
        elif num:
            break
    return int(num) if num else 0

def parse_year(v):
    if v is None:
        return 0
    digits = "".join(ch for ch in str(v) if ch.isdigit())
    return int(digits[:4]) if len(digits) >= 4 else 0

def probe(full):
    """Return the index 'meta' dict for one mp3, with folder/name fallbacks."""
    rel = os.path.relpath(full, SRC).replace(os.sep, "/")
    comps = rel.split("/")
    fb_title  = os.path.splitext(comps[-1])[0]
    fb_album  = comps[-2] if len(comps) >= 2 else ""
    fb_artist = comps[-3] if len(comps) >= 3 else (comps[-2] if len(comps) >= 2 else "")

    tags, duration, size = {}, 0.0, os.path.getsize(full)
    try:
        out = subprocess.run(
            ["ffprobe", "-v", "quiet", "-print_format", "json",
             "-show_format", "-show_streams", full],
            capture_output=True, text=True, check=True).stdout
        data = json.loads(out)
        fmt = data.get("format", {})
        tags = fmt.get("tags", {}) or {}
        try:
            duration = round(float(fmt.get("duration", 0.0)), 2)
        except (TypeError, ValueError):
            duration = 0.0
        try:
            size = int(fmt.get("size", size))
        except (TypeError, ValueError):
            pass
    except (subprocess.CalledProcessError, json.JSONDecodeError, OSError) as e:
        print(f"warning: ffprobe failed for {rel}: {e}", file=sys.stderr)

    artist = first_tag(tags, "artist", "author", "performer") or fb_artist or "Unknown Artist"
    album_artist = first_tag(tags, "album_artist", "albumartist") or artist
    return {
        "title":        first_tag(tags, "title") or fb_title,
        "artist":       artist,
        "album_artist": album_artist,
        "album":        first_tag(tags, "album") or fb_album or "Unknown Album",
        "track":        parse_int(first_tag(tags, "track", "tracknumber")),
        "disc":         parse_int(first_tag(tags, "disc", "discnumber")) or 1,
        "year":         parse_year(first_tag(tags, "date", "year", "originalyear", "creation_time")),
        "genre":        first_tag(tags, "genre") or "",
        "duration":     duration,
        "size":         size,
    }

to_probe = adds + updates
if to_probe:
    print(f"reading tags for {len(to_probe)} file(s) with {JOBS} worker(s)...")
    done = 0
    with cf.ThreadPoolExecutor(max_workers=JOBS) as ex:
        for full, meta in zip(to_probe, ex.map(probe, to_probe)):
            files[full]["meta"] = meta
            done += 1
            if done % 250 == 0:
                print(f"  ...{done}/{len(to_probe)}")

# ---- apply file operations -------------------------------------------------
copied = 0
for full in adds + updates:
    destrel = files[full]["dest"]
    destabs = os.path.join(DEST, *destrel.split("/"))
    os.makedirs(os.path.dirname(destabs), exist_ok=True)
    shutil.copyfile(full, destabs)   # content only -> no xattrs/._ files on FAT
    copied += 1
    if copied % 250 == 0:
        print(f"  copied {copied}/{len(adds) + len(updates)}")

removed = 0
if DO_DELETE:
    for full in deletions:
        destrel = files[full]["dest"]
        destabs = os.path.join(DEST, *destrel.split("/"))
        try:
            if os.path.exists(destabs):
                os.remove(destabs)
            removed += 1
        except OSError as e:
            print(f"warning: could not delete {destrel}: {e}", file=sys.stderr)
        del files[full]

    # prune now-empty numeric dirs and forget their numbers
    music_abs = os.path.join(DEST, MUSIC_ROOT)
    if os.path.isdir(music_abs):
        for root, dnames, fnames in os.walk(music_abs, topdown=False):
            if root == music_abs:
                continue
            if not os.listdir(root):
                try:
                    os.rmdir(root)
                except OSError:
                    pass
    # Forget number assignments for source dirs that no longer have any files,
    # so they don't linger in state. Counters stay at their high-water mark, so
    # numbers are never reused (deletions leave gaps), keeping names stable.
    live_src_dirs = set()
    for full in files:
        rel = os.path.relpath(full, SRC).replace(os.sep, "/")
        parent = posixpath.dirname(rel)
        while parent:
            live_src_dirs.add(parent)
            parent = posixpath.dirname(parent)
    for d in [d for d in dirs if d not in live_src_dirs]:
        del dirs[d]

# ---- write SKIPPED.LOG -----------------------------------------------------
skip_path = os.path.join(DEST, SKIPPED_NAME)
if skipped:
    with open(skip_path, "w", encoding="utf-8") as fh:
        fh.write(f"# Skipped non-MP3 audio ({len(skipped)}) — "
                 f"{dt.datetime.now().isoformat(timespec='seconds')}\n")
        for rel in skipped:
            fh.write(rel + "\n")
elif os.path.exists(skip_path):
    os.remove(skip_path)

# ---- write INDEX.JSN (atomic) ---------------------------------------------
def atomic_write_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(obj, fh, ensure_ascii=False, separators=(",", ":"))
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, path)

tracks = sorted((f["meta"] | {"path": f["dest"]}
                 for f in files.values() if f.get("meta")),
                key=lambda t: t["path"])

index = {
    "schema": 1,
    "generated_at": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "music_root": MUSIC_ROOT,
    "track_count": len(tracks),
    "tracks": tracks,
}
atomic_write_json(os.path.join(DEST, INDEX_NAME), index)

# ---- persist state ---------------------------------------------------------
state["width"] = WIDTH
atomic_write_json(STATE_FILE, state)

# ---- summary ---------------------------------------------------------------
print("\ndone.")
print(f"  added:     {len(adds)}")
print(f"  updated:   {len(updates)}")
print(f"  deleted:   {removed if DO_DELETE else 0}"
      + ("" if DO_DELETE else f"  (--no-delete; {len(deletions)} orphan(s) kept)"))
print(f"  unchanged: {len(unchanged)}")
print(f"  skipped:   {len(skipped)} (non-mp3 audio)")
print(f"  indexed:   {len(tracks)} track(s) -> {os.path.join(DEST, INDEX_NAME)}")
PY

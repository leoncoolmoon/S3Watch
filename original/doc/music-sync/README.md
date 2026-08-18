# music-sync

One-way sync of a Mac MP3 library to an **8.3 / FAT (no long filenames)** SD card,
renaming every folder and file to numbers and writing a JSON index that maps the
numeric names back to full song/artist/album metadata.

Built for an **ESP32-S3 music player** whose firmware reads `INDEX.JSN` from the
card root. The card holds only numbered files; all human-readable metadata lives
in the index.

## Why numbers?

The ESP32's FAT driver sees raw 8.3 short names. If macOS writes a long filename
like `Johnny Cash - Hurt.mp3`, FAT stores a mangled `JOHNNY~1.MP3` alias that the
firmware would have to deal with. By naming everything on the card with digits
plus `.MP3` (e.g. `MUSIC/0001/0001/0005.MP3`), every name is already 8.3-legal —
no aliases, no surprises. The real tags are preserved in `INDEX.JSN`.

## Requirements

- [`ffprobe`](https://ffmpeg.org/) — reads song tags. Install with `brew install ffmpeg`.
- `python3` — does the diff, metadata extraction, and index writing.

## Usage

```sh
./music-sync.sh --dest /Volumes/ESP32SD [options]
```

| Flag           | Meaning                                                          |
|----------------|------------------------------------------------------------------|
| `--dest DIR`   | **(required)** SD-card mount point (or any dir on it).           |
| `--source DIR` | Library root to sync from. Default: `~/Music/Music/Media.localized/Music`. |
| `--dry-run`    | Show what would change; touch nothing.                           |
| `--no-delete`  | Additive only: never delete from the card (no true mirror).      |
| `--rebuild`    | Ignore saved state; re-extract and re-copy everything.           |
| `--width N`    | Digits per numeric name (default `4` → `0001`; must be 1–8).     |
| `--jobs N`     | Parallel `ffprobe` workers on first scan (default `4`).          |
| `-h`, `--help` | Show help.                                                       |

### Examples

```sh
# Preview a sync without writing anything
./music-sync.sh --dest /Volumes/ESP32SD --dry-run

# Mirror the default library to the card
./music-sync.sh --dest /Volumes/ESP32SD

# Sync a different folder, additively (keep anything already on the card)
./music-sync.sh --dest /Volumes/ESP32SD --source ~/Music/Playlists --no-delete

# Force a clean re-number and re-copy of everything
./music-sync.sh --dest /Volumes/ESP32SD --rebuild
```

## What lands on the card

```
/Volumes/ESP32SD/
├── INDEX.JSN        # the firmware contract: numeric path -> full metadata
├── SKIPPED.LOG      # non-MP3 audio that was ignored (only if any were found)
└── MUSIC/
    └── 0001/        # = first source artist/folder
        └── 0001/    # = first album under it
            └── 0005.MP3
```

Only file **content** is copied (via `shutil.copyfile` with `COPYFILE_DISABLE=1`),
so no AppleDouble `._*` or `.DS_Store` resource forks litter the FAT card.

Non-MP3 audio (`.m4a`, `.flac`, `.wav`, `.aac`, …) is **skipped**, not converted,
and listed in `SKIPPED.LOG`.

## State & change detection

Number assignments and per-file size/mtime are kept on the Mac at:

```
~/.music-sync/state.json
```

On each run the script diffs the source against this state and only copies
**added** or **changed** files. Deleting the state file (or passing `--rebuild`)
forces a full re-number and re-copy.

Numeric names are **stable**: deletions leave gaps rather than renumbering, so a
file's path on the card never changes out from under the firmware. Changing
`--width` renumbers everything.

## The index format

`INDEX.JSN` is plain JSON (the `.JSN` extension is just an 8.3-legal stand-in for
`.json`). The full field-by-field contract — top-level keys and every `tracks[]`
field, with fallback rules and guarantees — is documented in
[INDEX-SCHEMA.md](INDEX-SCHEMA.md). That doc is repo reference for firmware
authors and is **not** copied to the card.

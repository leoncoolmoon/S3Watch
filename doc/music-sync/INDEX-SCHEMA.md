# INDEX.JSN — ESP32 music index format

`INDEX.JSN` lives at the root of the SD card. It is plain JSON (the `.JSN`
extension is just an 8.3-legal stand-in for `.json`; parse it with cJSON).

Top level:

| field          | type   | meaning                                            |
|----------------|--------|----------------------------------------------------|
| `schema`       | int    | format version (currently 1)                       |
| `generated_at` | string | UTC ISO-8601 timestamp of the last sync            |
| `music_root`   | string | folder under the card root holding all audio       |
| `track_count`  | int    | number of entries in `tracks`                      |
| `tracks`       | array  | one object per song (see below)                    |

Each `tracks[]` object:

| field          | type   | notes                                              |
|----------------|--------|----------------------------------------------------|
| `path`         | string | file path **relative to this index file** (8.3)    |
| `title`        | string | falls back to the file name if untagged            |
| `artist`       | string |                                                    |
| `album_artist` | string | falls back to `artist`                             |
| `album`        | string |                                                    |
| `track`        | int    | 0 if unknown                                        |
| `disc`         | int    | defaults to 1                                       |
| `year`         | int    | 0 if unknown                                        |
| `genre`        | string | "" if unknown                                       |
| `duration`     | float  | seconds                                             |
| `size`         | int    | bytes                                               |

Guarantees:
- No field is ever `null`.
- Every on-card name is valid 8.3 (digits + `.MP3`), so no `NAME~1` aliases.
- `tracks` is sorted by `path`.

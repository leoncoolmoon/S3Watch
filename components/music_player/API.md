# music_player API

## Types

```c
typedef struct {
    bool     active;            // false until a track has been loaded
    bool     playing;           // true = audibly playing
    uint32_t flat_track_index;
    uint32_t position_s;
    uint32_t duration_s;
    char     title[64];
    char     artist[64];
    char     album[64];
} music_now_playing_t;
```

## Catalog

### `music_player_load_catalog`
```c
esp_err_t music_player_load_catalog(void);
```
Mount SD card and parse `INDEX.JSN`. Idempotent. Returns `ESP_OK` only if a non-empty catalog is available.

---

### `music_player_catalog_ready`
```c
bool music_player_catalog_ready(void);
```
Returns true if the catalog has at least one track.

---

### Hierarchy accessors

```c
uint32_t    music_catalog_artist_count(void);
const char *music_catalog_artist_name(uint32_t artist_index);

uint32_t    music_catalog_album_count(uint32_t artist_index);
const char *music_catalog_album_name(uint32_t artist_index, uint32_t album_index);

uint32_t    music_catalog_track_count(uint32_t artist_index, uint32_t album_index);
const char *music_catalog_track_title(uint32_t artist_index, uint32_t album_index, uint32_t track_index);

uint32_t    music_catalog_track_flat_index(uint32_t artist_index, uint32_t album_index, uint32_t track_index);
uint32_t    music_catalog_total_track_count(void);
```

Safe to call from any task (read-only after catalog build). `music_catalog_track_flat_index()` converts a hierarchy position to a flat index for `music_player_play()`.

---

## Transport

All transport functions lazily start the playback engine on first call. They enqueue commands and return immediately.

### `music_player_play`
```c
void music_player_play(uint32_t flat_track_index);
```
Start playing the track at `flat_track_index`. Flushes the ring buffer before switching.

---

### `music_player_toggle_play_pause`
```c
void music_player_toggle_play_pause(void);
```
Toggle between playing and paused. Opens/closes the codec on each toggle to prevent I2S noise while paused.

---

### `music_player_next` / `music_player_prev`
```c
void music_player_next(void);
void music_player_prev(void);
```
Advance or rewind by one track. Respects shuffle if enabled.

---

### `music_player_stop`
```c
void music_player_stop(void);
```
Forget the current track entirely: stop output, release the decoder + file handle, and mark now-playing inactive (`active=false`). The SD mount and in-memory catalog are kept, so the next `music_player_play()` starts cleanly. No-op if the engine was never started. Used by the Music app to drop a paused track when the user leaves the app.

---

### `music_player_set_shuffle` / `music_player_get_shuffle`
```c
void music_player_set_shuffle(bool on);
bool music_player_get_shuffle(void);
```
Enable or disable shuffle mode. When on, `next` and `prev` pick a random track (never the current one) rather than advancing sequentially.

---

### `music_player_get_now_playing`
```c
void music_player_get_now_playing(music_now_playing_t *out);
```
Thread-safe snapshot of current playback state. Safe to call from LVGL/UI task. Returns `active=false` if the engine was never started.

---

### `music_player_is_playing`
```c
bool music_player_is_playing(void);
```
Thread-safe; true iff a track is loaded **and** audibly playing (not paused/stopped). Used by the display-wake hook to bring the watch up on the Music Now-Playing screen for one-tap pause/stop.

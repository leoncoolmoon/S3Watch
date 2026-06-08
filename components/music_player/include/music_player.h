#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// MP3 player backed by the SD card's MUSIC/ library and INDEX.JSN catalog
// (format documented in doc/music-sync/INDEX-SCHEMA.md). Two independent
// pieces:
//
//   - Catalog (music_player_load_catalog + music_catalog_* accessors): a
//     read-only Artist -> Album -> Track hierarchy built once, lazily, from
//     INDEX.JSN. Safe to call from the LVGL/UI task.
//
//   - Playback engine (music_player_play / transport functions): a dedicated
//     background task that owns the decoder and the speaker, started lazily
//     on first use and then persistent for the rest of the boot session —
//     playback continues regardless of which screen is showing.
//
// Catalog tracks are addressed by "flat index": their position in INDEX.JSN's
// tracks[] array, which is guaranteed sorted by path (= grouped artist ->
// album -> track in one linear pass). music_catalog_track_flat_index()
// converts a hierarchy position to this flat index for music_player_play().

// ── Catalog ──────────────────────────────────────────────────────────────

// Lazily mounts the SD card, parses INDEX.JSN, and builds the in-memory
// catalog. Idempotent — later calls return the cached result immediately.
// Returns ESP_OK only if a non-empty catalog is now available.
esp_err_t music_player_load_catalog(void);

bool music_player_catalog_ready(void);

uint32_t music_catalog_artist_count(void);
const char *music_catalog_artist_name(uint32_t artist_index);

uint32_t music_catalog_album_count(uint32_t artist_index);
const char *music_catalog_album_name(uint32_t artist_index, uint32_t album_index);

uint32_t music_catalog_track_count(uint32_t artist_index, uint32_t album_index);
const char *music_catalog_track_title(uint32_t artist_index, uint32_t album_index, uint32_t track_index);

// Position-in-hierarchy -> flat catalog index (the value music_player_play()
// and music_now_playing_t::flat_track_index use).
uint32_t music_catalog_track_flat_index(uint32_t artist_index, uint32_t album_index, uint32_t track_index);

uint32_t music_catalog_total_track_count(void);

// ── Transport ────────────────────────────────────────────────────────────
// All of these lazily start the playback engine on first call. They enqueue
// commands to the engine's task and return immediately — playback state
// changes asynchronously; poll music_player_get_now_playing() to observe it.

void music_player_play(uint32_t flat_track_index);
void music_player_toggle_play_pause(void);
void music_player_next(void);
void music_player_prev(void);
void music_player_set_shuffle(bool on);
bool music_player_get_shuffle(void);

typedef struct {
    bool     active;            // false until a track has ever been loaded
    bool     playing;           // true = audibly playing, false = paused
    uint32_t flat_track_index;
    uint32_t position_s;
    uint32_t duration_s;
    char     title[64];
    char     artist[64];
    char     album[64];
} music_now_playing_t;

// Thread-safe snapshot for UI polling (now-playing screen, mini-indicator).
// Safe to call even if the engine was never started (returns active=false).
void music_player_get_now_playing(music_now_playing_t *out);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Internal catalog representation — built once from INDEX.JSN by
// music_catalog_load(), then read by both music_player.h's public catalog
// accessors (browse UI) and the playback engine (path/duration lookups by
// flat index). All strings are owned, PSRAM-allocated copies; the cJSON
// parse tree is freed once this is built (see music_catalog.c).

typedef struct {
    char    *name;
    uint32_t album_start;   // first index into g_music_albums
    uint32_t album_count;
} music_artist_t;

typedef struct {
    char    *name;
    uint32_t artist_index;
    uint32_t track_start;   // first index into g_music_tracks
    uint32_t track_count;
} music_album_t;

typedef struct {
    char    *title;
    char    *path;          // absolute, ready for fopen(): "<mount_point>/MUSIC/0001/0001/0005.MP3"
    uint32_t artist_index;
    uint32_t album_index;
    uint32_t duration_s;
} music_track_t;

extern music_artist_t *g_music_artists;
extern music_album_t  *g_music_albums;
extern music_track_t  *g_music_tracks;
extern uint32_t        g_music_artist_count;
extern uint32_t        g_music_album_count;
extern uint32_t        g_music_track_count;

// Builds the catalog from "<mount>/INDEX.JSN" if not already built. The SD
// card must already be mounted (caller's responsibility — see sd_acquire-
// style lifecycle in music_player.c). Returns ESP_OK only with a non-empty
// catalog; subsequent calls are no-ops that return the cached result.
esp_err_t music_catalog_load(const char *mount_point);

#ifdef __cplusplus
}
#endif

// Parses INDEX.JSN (schema: doc/music-sync/INDEX-SCHEMA.md) and builds a
// compact, read-only Artist -> Album -> Track catalog in PSRAM.
//
// INDEX-SCHEMA.md guarantees tracks[] is sorted by `path`, and on-card paths
// are "MUSIC/<artist_num>/<album_num>/<track_num>.MP3" — so a sorted-by-path
// scan is already grouped artist -> album -> track. One linear pass over the
// array assigns each track to a [start,count) run in g_music_albums, and each
// album to a [start,count) run in g_music_artists, with no sorting needed.
//
// We don't know the artist/album counts ahead of time, so the artist/album
// arrays are allocated at the worst-case size (== track_count, i.e. "every
// track is its own artist/album") and shrunk to their real size with
// heap_caps_realloc() once the scan completes — the resident catalog ends up
// far smaller than the cJSON parse tree it was built from, which is freed
// immediately after.

#include "music_catalog.h"

#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "MUSIC_CAT";

#define INDEX_FILE_FMT "%s/INDEX.JSN"

music_artist_t *g_music_artists     = NULL;
music_album_t  *g_music_albums      = NULL;
music_track_t  *g_music_tracks      = NULL;
uint32_t        g_music_artist_count = 0;
uint32_t        g_music_album_count  = 0;
uint32_t        g_music_track_count  = 0;

static bool s_loaded = false;

static char *psram_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = (char *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (p) memcpy(p, s, len);
    return p;
}

static const char *json_str(const cJSON *obj, const char *key, const char *fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : fallback;
}

static void free_catalog(void)
{
    for (uint32_t i = 0; i < g_music_artist_count; i++) heap_caps_free(g_music_artists[i].name);
    for (uint32_t i = 0; i < g_music_album_count; i++)  heap_caps_free(g_music_albums[i].name);
    for (uint32_t i = 0; i < g_music_track_count; i++) {
        heap_caps_free(g_music_tracks[i].title);
        heap_caps_free(g_music_tracks[i].path);
    }
    heap_caps_free(g_music_artists);
    heap_caps_free(g_music_albums);
    heap_caps_free(g_music_tracks);
    g_music_artists = NULL;
    g_music_albums  = NULL;
    g_music_tracks  = NULL;
    g_music_artist_count = g_music_album_count = g_music_track_count = 0;
}

// Loads the whole file into a malloc'd buffer and cJSON_Parse()s it — the
// same load-whole-file-then-parse pattern as settings_restore_from_sd()
// (components/settings/settings.c).
static cJSON *load_index_json(const char *mount_point)
{
    char path[128];
    snprintf(path, sizeof(path), INDEX_FILE_FMT, mount_point);

    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        ESP_LOGW(TAG, "%s not found", path);
        return NULL;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "Could not open %s", path);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)st.st_size + 1);
    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG, "Could not allocate %ld bytes for %s", (long)st.st_size, path);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    buf[n] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse %s", path);
    }
    return root;
}

static esp_err_t build_from_json(cJSON *root, const char *mount_point)
{
    cJSON *tracks_json = cJSON_GetObjectItemCaseSensitive(root, "tracks");
    if (!cJSON_IsArray(tracks_json)) {
        ESP_LOGW(TAG, "INDEX.JSN has no tracks[] array");
        return ESP_FAIL;
    }
    int total = cJSON_GetArraySize(tracks_json);
    if (total <= 0) {
        ESP_LOGW(TAG, "INDEX.JSN tracks[] is empty");
        return ESP_FAIL;
    }
    size_t cap = (size_t)total;

    music_artist_t *artists = (music_artist_t *)heap_caps_malloc(cap * sizeof(*artists), MALLOC_CAP_SPIRAM);
    music_album_t  *albums  = (music_album_t  *)heap_caps_malloc(cap * sizeof(*albums),  MALLOC_CAP_SPIRAM);
    music_track_t  *tracks  = (music_track_t  *)heap_caps_malloc(cap * sizeof(*tracks),  MALLOC_CAP_SPIRAM);
    if (!artists || !albums || !tracks) {
        ESP_LOGE(TAG, "Catalog allocation failed (cap=%u tracks)", (unsigned)cap);
        heap_caps_free(artists);
        heap_caps_free(albums);
        heap_caps_free(tracks);
        return ESP_ERR_NO_MEM;
    }

    uint32_t n_artists = 0, n_albums = 0, n_tracks = 0;
    const char *cur_artist = NULL; // points into the live cJSON tree — valid for the loop's lifetime
    const char *cur_album  = NULL;
    size_t mount_len = strlen(mount_point);

    cJSON *t;
    cJSON_ArrayForEach(t, tracks_json) {
        const char *artist   = json_str(t, "album_artist", json_str(t, "artist", "Unknown Artist"));
        const char *album    = json_str(t, "album", "Unknown Album");
        const char *title    = json_str(t, "title", "Unknown Title");
        const char *rel_path = json_str(t, "path", NULL);
        if (!rel_path) continue; // unusable entry — schema guarantees this won't happen, but don't trust blindly

        cJSON *dur_json = cJSON_GetObjectItemCaseSensitive(t, "duration");
        double duration = cJSON_IsNumber(dur_json) ? dur_json->valuedouble : 0.0;

        bool new_artist = (n_artists == 0) || (strcmp(artist, cur_artist) != 0);
        if (new_artist) {
            artists[n_artists].name        = psram_strdup(artist);
            artists[n_artists].album_start = n_albums;
            artists[n_artists].album_count = 0;
            n_artists++;
            cur_artist = artist;
            cur_album  = NULL; // force a new album group under the new artist
        }

        bool new_album = new_artist || (strcmp(album, cur_album) != 0);
        if (new_album) {
            albums[n_albums].name         = psram_strdup(album);
            albums[n_albums].artist_index = n_artists - 1;
            albums[n_albums].track_start  = n_tracks;
            albums[n_albums].track_count  = 0;
            artists[n_artists - 1].album_count++;
            n_albums++;
            cur_album = album;
        }

        // Build the absolute path once here ("<mount>/<rel_path>") so the
        // playback engine can fopen() it directly with no per-play formatting.
        char *abs_path = (char *)heap_caps_malloc(mount_len + 1 + strlen(rel_path) + 1, MALLOC_CAP_SPIRAM);
        if (abs_path) {
            memcpy(abs_path, mount_point, mount_len);
            abs_path[mount_len] = '/';
            strcpy(abs_path + mount_len + 1, rel_path);
        }

        tracks[n_tracks].title        = psram_strdup(title);
        tracks[n_tracks].path         = abs_path;
        tracks[n_tracks].artist_index = n_artists - 1;
        tracks[n_tracks].album_index  = n_albums - 1;
        tracks[n_tracks].duration_s   = duration > 0 ? (uint32_t)(duration + 0.5) : 0;
        albums[n_albums - 1].track_count++;
        n_tracks++;
    }

    if (n_tracks == 0) {
        heap_caps_free(artists);
        heap_caps_free(albums);
        heap_caps_free(tracks);
        return ESP_FAIL;
    }

    // Shrink the worst-case allocations down to the real counts.
    artists = (music_artist_t *)heap_caps_realloc(artists, n_artists * sizeof(*artists), MALLOC_CAP_SPIRAM);
    albums  = (music_album_t  *)heap_caps_realloc(albums,  n_albums  * sizeof(*albums),  MALLOC_CAP_SPIRAM);
    tracks  = (music_track_t  *)heap_caps_realloc(tracks,  n_tracks  * sizeof(*tracks),  MALLOC_CAP_SPIRAM);

    g_music_artists      = artists;
    g_music_albums       = albums;
    g_music_tracks       = tracks;
    g_music_artist_count = n_artists;
    g_music_album_count  = n_albums;
    g_music_track_count  = n_tracks;

    ESP_LOGI(TAG, "Catalog built: %u artists, %u albums, %u tracks",
             (unsigned)n_artists, (unsigned)n_albums, (unsigned)n_tracks);
    return ESP_OK;
}

esp_err_t music_catalog_load(const char *mount_point)
{
    if (s_loaded) return ESP_OK;

    cJSON *root = load_index_json(mount_point);
    if (!root) return ESP_FAIL; // no card / no INDEX.JSN / bad JSON — retryable on next call (e.g. card inserted later)

    esp_err_t err = build_from_json(root, mount_point);
    cJSON_Delete(root); // reclaim the (much larger) parse tree now that the compact catalog is built

    if (err != ESP_OK) {
        free_catalog();
        return err;
    }
    s_loaded = true;
    return ESP_OK;
}

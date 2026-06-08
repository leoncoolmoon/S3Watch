// Main tileview construction + dynamic-tile lifecycle.
//
// Layout: watchface at (0,1) (home), controls at (1,1), SignalK alerts at
// (0,0) (swipe down), app picker at (0,2) (swipe up). Dynamic settings tiles
// open to the right of controls at (2,1) and (3,1). The app tile (1,2) opens
// to the right of the app picker when an app icon is tapped, and is deleted
// on navigate-away. All dynamic tiles use async deletion via the tileview's
// VALUE_CHANGED auto-clean handler to avoid freeing tiles mid-animation.

#include "ui.h"
#include "ui_tileview.h"
#include "ui_screens.h"
#include "watchface.h"
#include "settings_screen.h"
#include "app_picker.h"
#include "signalk_alerts.h"
#include "signalk_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "UI_TV";

static lv_obj_t* main_screen     = NULL;
static lv_obj_t* tile_alerts     = NULL;  // SignalK alerts     (0,0)
static lv_obj_t* tile2           = NULL;  // watchface          (0,1)
static lv_obj_t* tile4           = NULL;  // controls           (1,1)
static lv_obj_t* tile_picker     = NULL;  // app picker         (0,2)
static lv_obj_t* dynamic_tile    = NULL;  // settings menu / etc. (2,1)
static lv_obj_t* dynamic_subtile = NULL;  // settings sub-screens (3,1)
static lv_obj_t* s_app_tile      = NULL;  // dynamic app screen (1,2)

void ui_tileview_back(void) {
  if (active_screen_get() != get_main_screen()) {
    load_screen(NULL, get_main_screen(), LV_SCR_LOAD_ANIM_OVER_TOP);
  }
  // Prefer closing level-2 first, then level-1, then app tile, then home.
  if (dynamic_subtile) {
    ui_dynamic_subtile_close();
    return;
  }
  if (dynamic_tile) {
    ui_dynamic_tile_close();
    return;
  }
  if (s_app_tile) {
    ui_app_tile_close();
    return;
  }
  // Already at watchface/controls level — make sure we're showing watchface.
  if (tile2 && lv_tileview_get_tile_active(main_screen) != tile2) {
    lv_tileview_set_tile(main_screen, tile2, LV_ANIM_ON);
  }
}

static void tileview_change_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

  lv_obj_t* act = lv_tileview_get_tile_active(main_screen);

  // Any SignalK tile (app tile hosting the dashboard, OR alerts) brings WiFi +
  // WS up on enter, tears them down on leave to a non-signalk tile.
  // signalk_client_{start,stop} are idempotent and no-op when host is empty.
  {
    static lv_obj_t* prev_sk = NULL;
    bool act_is_sk  = (act     == s_app_tile || act     == tile_alerts);
    bool prev_is_sk = (prev_sk == s_app_tile || prev_sk == tile_alerts);
    if (act_is_sk && !prev_is_sk) {
      ESP_LOGI(TAG, "Entering SignalK tile — starting client");
      signalk_client_start();
    } else if (!act_is_sk && prev_is_sk) {
      ESP_LOGI(TAG, "Leaving SignalK tile — stopping client");
      signalk_client_stop();
    }
    prev_sk = act;
  }

  // Delete the app tile if navigated away from it.
  if (s_app_tile && act != s_app_tile) {
    ESP_LOGI(TAG, "Auto-clean: deleting app tile (1,2)");
    lv_obj_del_async(s_app_tile);
    s_app_tile = NULL;
  }

  // Delete level-2 if not active. Use async deletion: we're inside the
  // tileview's own VALUE_CHANGED handler, and the tileview keeps touching its
  // child tiles (scroll-snap / animation bookkeeping) after this returns —
  // a synchronous delete here frees a tile out from under it → crash.
  if (dynamic_subtile && act != dynamic_subtile) {
    ESP_LOGI(TAG, "Auto-clean: deleting dynamic subtile (3,1)");
    lv_obj_del_async(dynamic_subtile);
    dynamic_subtile = NULL;
  }
  // Delete level-1 if neither level-1 nor level-2 are active
  if (dynamic_tile && act != dynamic_tile && act != dynamic_subtile) {
    ESP_LOGI(TAG, "Auto-clean: deleting dynamic tile (2,1)");
    lv_obj_del_async(dynamic_tile);
    dynamic_tile = NULL;
  }
}

void swatch_tileview(void) {
  main_screen = lv_tileview_create(NULL);
  lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_add_style(main_screen, ui_get_main_style(), 0);
  lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(main_screen, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);

  // Observe tile changes to clean up dynamic tiles when not visible
  lv_obj_add_event_cb(main_screen, tileview_change_cb, LV_EVENT_ALL, NULL);

  // SignalK alerts above the watchface at (0,0). LV_DIR_BOTTOM enables
  // swipe-up to scroll back down to the watchface.
  tile_alerts = lv_tileview_add_tile(main_screen, 0, 0, (lv_dir_t)LV_DIR_BOTTOM);
  signalk_alerts_create(tile_alerts);

  // Watchface (0,1) is home. LV_DIR_TOP enables swipe-down (reveals the
  // tile above at (0,0)); LV_DIR_BOTTOM enables swipe-up (reveals the tile
  // below at (0,2)).
  tile2 = lv_tileview_add_tile(main_screen, 0, 1,
                               (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT | LV_DIR_TOP | LV_DIR_BOTTOM));
  watchface_create(tile2);

  tile4 = lv_tileview_add_tile(main_screen, 1, 1, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
  control_screen_create(tile4);

  // App picker below the watchface at (0,2). LV_DIR_TOP enables swipe-down
  // back to the watchface; LV_DIR_RIGHT allows sliding to the app tile (1,2).
  tile_picker = lv_tileview_add_tile(main_screen, 0, 2, (lv_dir_t)(LV_DIR_TOP | LV_DIR_RIGHT));
  app_picker_create(tile_picker);

  // Initial active tile: the watchface.
  lv_tileview_set_tile(main_screen, tile2, LV_ANIM_OFF);

  // Publish the main screen pointer so other modules can navigate to it.
  ui_screens_set_main(main_screen);
}

// Acquire or create a temporary tile to the right of controls (x=2,y=1)
lv_obj_t* ui_dynamic_tile_acquire(void) {
  if (!main_screen) return NULL;
  if (dynamic_tile) {
    // Clean existing content for reuse
    lv_obj_clean(dynamic_tile);
    ESP_LOGI(TAG, "Reusing dynamic tile (2,1)");
    return dynamic_tile;
  }
  dynamic_tile = lv_tileview_add_tile(main_screen, 2, 1, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
  if (dynamic_tile) {
    lv_obj_update_layout(main_screen);
    ESP_LOGI(TAG, "Created dynamic tile (2,1)");
  }
  return dynamic_tile;
}

// Focus the dynamic tile (ensure tileview is the active screen)
void ui_dynamic_tile_show(void) {
  if (!dynamic_tile || !main_screen) return;
  ESP_LOGI(TAG, "Showing dynamic tile (2,1)");

  if (active_screen_get() != get_main_screen()) {
    load_screen(NULL, get_main_screen(), LV_SCR_LOAD_ANIM_NONE);
  }
  lv_tileview_set_tile(main_screen, dynamic_tile, LV_ANIM_ON);
}

// Acquire or create a second-level tile at (3,1)
lv_obj_t* ui_dynamic_subtile_acquire(void) {
  if (!main_screen) return NULL;
  if (dynamic_subtile) {
    lv_obj_clean(dynamic_subtile);
    return dynamic_subtile;
  }
  dynamic_subtile = lv_tileview_add_tile(main_screen, 3, 1, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
  if (dynamic_subtile) {
    lv_obj_update_layout(main_screen);
    lv_obj_update_layout(dynamic_tile);
    ESP_LOGI(TAG, "Created dynamic subtile (3,1)");
  }
  return dynamic_subtile;
}

void ui_dynamic_subtile_show(void) {
  if (!dynamic_subtile || !main_screen) return;
  ESP_LOGI(TAG, "Showing dynamic tile (3,1)");
  if (active_screen_get() != get_main_screen()) {
    load_screen(NULL, get_main_screen(), LV_SCR_LOAD_ANIM_NONE);
  }

  lv_tileview_set_tile(main_screen, dynamic_subtile, LV_ANIM_ON);
}

void ui_dynamic_subtile_close(void) {
  if (!main_screen) return;
  if (dynamic_subtile) {
    if (dynamic_tile) {
      lv_tileview_set_tile(main_screen, dynamic_tile, LV_ANIM_ON);
    }
    else if (tile4) {
      lv_tileview_set_tile(main_screen, tile4, LV_ANIM_ON);
    }
    ESP_LOGI(TAG, "Deleting dynamic subtile (3,1)");
    // Defer deletion: a scroll animation away from this tile was just started;
    // freeing it synchronously mid-animation corrupts tileview scroll state.
    lv_obj_del_async(dynamic_subtile);
    dynamic_subtile = NULL;
  }
}

void ui_tileview_reset_to_watchface(void) {
  if (!main_screen || !tile2) return;
  if (dynamic_subtile) {
    lv_obj_del_async(dynamic_subtile);
    dynamic_subtile = NULL;
  }
  if (dynamic_tile) {
    lv_obj_del_async(dynamic_tile);
    dynamic_tile = NULL;
  }
  if (s_app_tile) {
    lv_obj_del_async(s_app_tile);
    s_app_tile = NULL;
  }
  if (lv_tileview_get_tile_active(main_screen) != tile2) {
    lv_tileview_set_tile(main_screen, tile2, LV_ANIM_OFF);
  }
}

// ---------------------------------------------------------------------------
// Row-2 app tile — opened by the app picker at (0,2), lives at (1,2).
// Mirrors the row-1 dynamic tile pattern exactly.
// ---------------------------------------------------------------------------

lv_obj_t* ui_app_tile_acquire(void) {
  if (!main_screen) return NULL;
  if (s_app_tile) {
    lv_obj_clean(s_app_tile);
    ESP_LOGI(TAG, "Reusing app tile (1,2)");
    return s_app_tile;
  }
  s_app_tile = lv_tileview_add_tile(main_screen, 1, 2, (lv_dir_t)LV_DIR_LEFT);
  if (s_app_tile) {
    lv_obj_update_layout(main_screen);
    ESP_LOGI(TAG, "Created app tile (1,2)");
  }
  return s_app_tile;
}

void ui_app_tile_show(void) {
  if (!s_app_tile || !main_screen) return;
  ESP_LOGI(TAG, "Showing app tile (1,2)");
  if (active_screen_get() != get_main_screen()) {
    load_screen(NULL, get_main_screen(), LV_SCR_LOAD_ANIM_NONE);
  }
  lv_tileview_set_tile(main_screen, s_app_tile, LV_ANIM_ON);
  lv_tileview_set_tile(main_screen, s_app_tile, LV_ANIM_ON);
}

void ui_app_tile_close(void) {
  if (!main_screen) return;
  if (tile_picker) {
    lv_tileview_set_tile(main_screen, tile_picker, LV_ANIM_ON);
  }
  if (s_app_tile) {
    ESP_LOGI(TAG, "Deleting app tile (1,2)");
    lv_obj_del_async(s_app_tile);
    s_app_tile = NULL;
  }
}

// Close and destroy the dynamic tile, returning to controls (tile4)
void ui_dynamic_tile_close(void) {
  if (!main_screen) return;
  if (dynamic_tile) {
    // Navigate back to controls tile then delete
    if (tile4) {
      lv_tileview_set_tile(main_screen, tile4, LV_ANIM_ON);
    }
    ESP_LOGI(TAG, "Deleting dynamic tile (2,1)");
    // Defer deletion (see ui_dynamic_subtile_close) — animation is in flight.
    lv_obj_del_async(dynamic_tile);
    dynamic_tile = NULL;
  }
}

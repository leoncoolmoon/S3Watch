# gui API

The `gui` component's public API is the tile management surface in `ui.h`. Individual screen headers (`music_app.h`, `signalk_dashboard.h`, etc.) are internal — other components do not call screen functions directly.

## Tile management

### Dynamic tile (settings / transient content)
```c
lv_obj_t *ui_dynamic_tile_acquire(void);
void      ui_dynamic_tile_show(void);
void      ui_dynamic_tile_close(void);
```
Acquire a blank tile container, populate it, call `show()` to animate in. Call `close()` to return to the previous tile and free the content. Only one dynamic tile content is active at a time.

---

### App tile (full-screen apps)
```c
lv_obj_t *ui_app_tile_acquire(void);
void      ui_app_tile_show(void);
void      ui_app_tile_close(void);
```
Same pattern as the dynamic tile, for the app row.

---

### App tile — SignalK marker
```c
void ui_app_tile_mark_signalk(void);
```
Mark the currently-acquired app tile as the SignalK dashboard. This prevents the tile coordinator from bringing WiFi/WS up for other apps that happen to share the same tile slot. Cleared automatically on the next `ui_app_tile_acquire()`.

---

### Dynamic subtile
```c
lv_obj_t *ui_dynamic_subtile_acquire(void);
void      ui_dynamic_subtile_show(void);
void      ui_dynamic_subtile_close(void);
```
Second-level tile to the right of the dynamic tile. Used for nested settings (e.g. TZ picker inside Time Settings).

---

## Entry points

```c
void ui_init(void);                    // create all tiles and register watchface
void ui_task(void *pvParameters);      // FreeRTOS task entry point; call from xTaskCreate()
lv_style_t *ui_get_main_style(void);   // shared LVGL style for consistent theming
```

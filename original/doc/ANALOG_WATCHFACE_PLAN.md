# Plan: Analog watchface (Face 3)

## Context
The user wants to add a custom analog watchface, gated on three hard requirements: it must (1) respect/display the background image chosen in Settings, (2) use user-supplied artwork (with alpha transparency) for the dial face, and (3) use user-supplied artwork for the individual hour/minute/second hands, rotating live. They were clear that if these aren't achievable, it's not worth doing.

**Feasibility is confirmed** — every requirement maps directly onto patterns and config already proven elsewhere in this codebase:
- **Background**: faces 1/2 read `settings_get_watchface_bg()` and switch between three compiled-in `LV_IMAGE_DECLARE()` images via `lv_image_create()` + `lv_image_set_src()` — now consolidated into the shared `watchface_add_background()` helper ([watchface.c:39-53](components/gui/src/watchface.c#L39-L53), called from [face_1.c:27](components/gui/src/faces/face_1.c#L27)).
- **Alpha-transparent custom art**: the project already converts PNGs-with-alpha into LVGL C arrays via `ui_assets/LVGLImage.py --ofmt C --cf RGB565A8` (used for every notification icon — see `ui_assets/README.MD`), and `CONFIG_LV_DRAW_SW_SUPPORT_RGB565A8=y` is enabled in `sdkconfig`. The same pipeline works for watchface art — RGB565A8 is a real RGB+alpha format (not "alpha-only", which LVGL explicitly can't transform).
- **Rotating hands**: confirmed LVGL **9.3.0** is in use, which has `lv_image_set_rotation(obj, angle)` (0.1°-resolution, 0..3600, clockwise) and `lv_image_set_pivot(obj, x, y)` ([lv_image.h:117-136](managed_components/lvgl__lvgl/src/widgets/image/lv_image.h#L117-L136)). Even better: **the default pivot is the image's own geometric center** ([lv_image.c:656](managed_components/lvgl__lvgl/src/widgets/image/lv_image.c#L656): `lv_point_set(&img->pivot, LV_PCT(50), LV_PCT(50))`), so no custom pivot math is needed if hand art is drawn symmetrically around its rotation point.

User's chosen design direction (from clarifying questions): match Face 1/2's overlay info (battery + date/weekday), and tick the second hand once per second rather than sweep — both of which let this reuse the existing 1 Hz refresh timer with zero new infrastructure.

Reference: the screen is a rectangular panel with rounded corners (410×502, see `ui_assets/s3watch_0.jpg` / `s3watch_1.jpg` in the README for a photo of the physical display) — *not* round. A circular analog dial drawn within that rectangle leaves its four corners free, which is also where this UI already places overlay info (battery top-left, Bluetooth top-right in the reference photos).

## Design

### 1. Asset spec to hand off to the user
Before art can be converted, the user needs the exact pixel conventions so their PNGs drop in correctly:
- **Dial overlay**: alpha PNG sized to fit the display (`BSP_LCD_H_RES`×`BSP_LCD_V_RES` = 410×502 rectangular panel with rounded corners — *not* a round display, per [esp32_s3_touch_amoled_2_06.h](managed_components/esp32_s3_touch_amoled_2_06/include/bsp/esp32_s3_touch_amoled_2_06.h)), transparent wherever the chosen background should show through (matching how `background_wf*` are sized/centered today). A traditional analog dial will likely be drawn as a circle within that rectangle, leaving its four corners free.
- **Each hand** (hour/minute/second, +optional center cap): drawn on a **symmetric canvas centered on the rotation point** — e.g. a hand that needs to reach 150 px from center should live in a canvas roughly 300×300 (or however tall/wide, as long as the clock-center point sits at the canvas's exact geometric center). This lets LVGL's default center-pivot rotate it correctly with no extra positioning code — just `lv_obj_set_align(hand, LV_ALIGN_CENTER)`.
- Convert with the same command already documented in `ui_assets/README.MD`:
  `python LVGLImage.py <file>.png --ofmt C --cf RGB565A8 -o ../components/gui/icons`
  (generated `.c` files are auto-picked up by the existing glob in [gui/CMakeLists.txt](components/gui/CMakeLists.txt)).

I'll relay this spec to the user and pause for them to produce/convert the art before the face can be visually tuned — the plan below assumes the `.c` descriptors exist (named e.g. `analog_dial`, `analog_hand_hour`, `analog_hand_minute`, `analog_hand_second`).

### 2. Shared face helpers — already done, and face 3 should use all four
Since this plan was drafted, the N4 consolidation landed: `add_background()`,
the battery-widget build/update pair, and the hour-label formatter were all
extracted out of face_1/face_2 into shared helpers declared in
[watchface_iface.h](components/gui/include/watchface_iface.h) and implemented
in [watchface.c:39-124](components/gui/src/watchface.c#L39-L124) (see
`faces/README.md`'s "Shared face helpers" section for the full contract). No
refactor step is needed before face 3 — it should simply call into these from
the start, the same way face_1/face_2 now do:
- `watchface_add_background(lv_obj_t *parent)`
- `watchface_build_battery_widget(parent, &icon, &pct_label, &charge_label)`
- `watchface_update_battery_widget(icon, pct_label, charge_label, vbus_in, charging, battery_percent)`
- `watchface_update_hour_label(label_hour, label_ampm)` — only relevant if face 3 shows a digital hour readout alongside the dial; the analog hands themselves don't need it (see `face3_update_time` below).

### 3. New `components/gui/src/faces/face_3.c`
Modeled on the `watchface_iface_t` contract ([watchface_iface.h](components/gui/include/watchface_iface.h)) and face_1's structure ([face_1.c](components/gui/src/faces/face_1.c), now a slim ~107 lines thanks to the shared helpers):

**`face3_build(lv_obj_t *c)`**
1. `watchface_add_background(c)` — shared helper, see above.
2. Dial overlay: `lv_image_create(c)` + `LV_IMAGE_DECLARE(analog_dial)`, centered.
3. `watchface_build_battery_widget(c, &img_battery, &lbl_batt_pct, &lbl_charge_icon)` for the battery icon/%/charge glyph, plus date + weekday labels following face_1's pattern ([face_1.c:60-77](components/gui/src/faces/face_1.c#L60-L77) for the surrounding label setup) — repositioned into the screen's corners outside the dial circle, where the hands never sweep (exact placement will need visual tuning once real art is in place — mirroring how face_1 tucks these into top-left/right-mid, just relocated to suit a circular dial on a rectangular panel).
4. Three hand images + optional center-cap, each `lv_image_create(c)` + `LV_IMAGE_DECLARE(...)`, `lv_obj_set_align(hand, LV_ALIGN_CENTER)`. No `lv_image_set_pivot()` call needed if art follows the symmetric-canvas convention from step 1 (LVGL defaults to center pivot). Stack order matters — create in order: hour, minute, second, cap (each later `lv_image_create` draws on top).

**`face3_update_time(void)`** — mirrors face_1's use of `rtc_get_hour/minute/second()` ([face_1.c:82-94](components/gui/src/faces/face_1.c#L82-L94)) but converts to rotation angles instead of label text (so it does its own thing here rather than calling `watchface_update_hour_label`, which formats 12h/24h label text — not applicable to rotating hands):
```c
int h = rtc_get_hour() % 12, m = rtc_get_minute(), s = rtc_get_second();
lv_image_set_rotation(hand_hour,   (h * 30 + m / 2) * 10);   // 0.1°, smooth hour creep
lv_image_set_rotation(hand_minute, (m * 6 + s / 10) * 10);   // 0.1°, slight minute creep
lv_image_set_rotation(hand_second, (s * 6) * 10);            // ticks once/sec — matches existing 1 Hz timer
```
Plus the same date/weekday label updates as face_1.

**`face3_update_power(...)`** — a thin passthrough to `watchface_update_battery_widget()`, exactly like [face_1.c:96-99](components/gui/src/faces/face_1.c#L96-L99) and [face_2.c](components/gui/src/faces/face_2.c)'s versions (no logic to duplicate — that's the whole point of the shared helper now).

Export `const watchface_iface_t watchface_face3 = { .name = "Face 3 (Analog)", .build = face3_build, .update_time = face3_update_time, .update_power = face3_update_power };`

### 4. Register the face
In [watchface.c](components/gui/src/watchface.c): add `extern const watchface_iface_t watchface_face3;` near the existing externs ([watchface.c:25-26](components/gui/src/watchface.c#L25-L26)) and append `&watchface_face3` to `s_faces[]` ([watchface.c:28-31](components/gui/src/watchface.c#L28-L31)). Per the dispatcher's own header comment ([watchface.c:7-13](components/gui/src/watchface.c#L7-L13)), the picker UI (`setting_watchface_screen`) updates automatically via `watchface_get_count()`/`watchface_get_name()` — no GUI changes needed there.

### 5. Build wiring
None needed — `components/gui/CMakeLists.txt` globs both `src/faces/*.c` and `icons/*.c`, so the new face file and generated image descriptors are picked up automatically, same as every other face/icon.

## Verification
- User builds and flashes manually (per standing instruction — no `idf.py build` from me).
- Open Settings → Watchface, select "Face 3 (Analog)"; confirm it becomes active and the picker round-trips correctly.
- Cycle through all three `watchface_bg` options on face 3; confirm the chosen background shows through the dial's transparent regions exactly like it does on faces 1/2.
- Watch a full minute tick by; confirm hour/minute/second hands all rotate to the correct positions each second (spot-check against a reference clock — e.g. at :15 the minute hand should be at 3 o'clock, hour hand 1/4 of the way to the next number).
- Confirm battery icon (color/percentage/charge glyph) and date/weekday update exactly as they do on face 1 (toggle charging/VBUS if possible).
- Switch faces back and forth a few times; confirm `watchface_rebuild()` cleanly tears down/rebuilds face 3 with no leftover hand images or stale rotations (LVGL's `lv_obj_clean()` on the container should handle this, same as the other faces).
- Spot-check power/CPU isn't visibly worse than faces 1/2 (tick-based second hand should be a wash — same 1 Hz timer, just rotating images instead of re-rendering label text).

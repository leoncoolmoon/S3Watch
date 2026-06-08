# Writing a watchface

A watchface is a self-contained module that builds its widgets into a parent
container and refreshes them on a 1 Hz tick. The dispatcher
([watchface.c](../watchface.c)) owns the container, the timer, and the
registry — a face just implements three callbacks against
[`watchface_iface_t`](../../include/watchface_iface.h) and gets a line in the
registry. The picker UI (`setting_watchface_screen`) drives itself off
`watchface_get_count()`/`watchface_get_name()`, so it updates automatically —
nothing to touch there.

## The contract

```c
typedef struct {
    const char *name;                                                   // shown in the picker
    void (*build)(lv_obj_t *parent);                                    // create widgets, called once per activation
    void (*update_time)(void);                                          // refresh from rtc_lib, called every second
    void (*update_power)(bool vbus_in, bool charging, int battery_pct); // refresh battery UI; no-op if you have none
} watchface_iface_t;
```

Look at [face_1.c](face_1.c) (digital, label-based) for the canonical example
— every step below references it directly.

## Lifecycle

- `build(parent)` runs once when the face becomes active (first boot, or the
  user picks it in Settings → Watchface → `watchface_rebuild()`). Create your
  widgets here and **stash their pointers in file-scope `static` variables** —
  that's how `update_time`/`update_power` reach them later. Reset every static
  to `NULL` at the top of `build`, and guard every later use with `if (ptr)` —
  this makes the callbacks safe no-ops on a half-initialized or
  mid-teardown face (see [face_1.c:23-25](face_1.c#L23-L25)).
- `update_time()` fires once a second from the dispatcher's shared LVGL timer
  ([watchface.c:144-147](../watchface.c#L144-L147)). The dispatcher already calls
  `rtc_refresh_now()` before invoking you — just read `rtc_get_hour()` /
  `_minute()` / `_second()` / `_day()` / `_month()` /
  `rtc_get_weekday_short_string()` etc. (see
  [bsp_extra/include/rtc_lib.h](../../../bsp_extra/include/rtc_lib.h)) and push
  formatted text or transformed widget state — the dispatcher does no
  formatting for you.
- `update_power(vbus_in, charging, battery_percent)` fires whenever the power
  state changes. If your face has no battery UI, implement it as an empty
  function — don't omit it (the struct field would be `NULL` and the
  dispatcher would skip it safely either way, but an explicit no-op documents
  intent).
- Switching away from your face calls `lv_obj_clean()` on the shared
  container, which destroys every widget you created — you don't need your
  own teardown. Just don't keep pointers to anything outside `parent`.

## Reading user prefs

Pull preferences from [`settings.h`](../../include/) rather than hard-coding.
The 12h/24h preference (`settings_get_time_24h()`) is already handled for you
by `watchface_update_hour_label()` (see "Shared face helpers" below) — call
it from `update_time` and it formats `label_hour` and shows/hides
`label_ampm` to match. `settings_get_watchface_bg()` is likewise handled by
`watchface_add_background()` (see Background, below).

## Background

Call `watchface_add_background(c)` (declared in `watchface_iface.h`,
implemented in `watchface.c`) as your face's first `build()` step — it reads
`settings_get_watchface_bg()`, switches between the three compiled-in
backgrounds, and centers the result. See
[face_1.c:27](face_1.c#L27) for the one-line call site. Don't reimplement
this per-face — it's exactly the kind of generic logic that belongs in the
shared dispatcher (see "Shared face helpers" below).

## Shared face helpers

`watchface_iface.h` declares a handful of helpers — implemented once in
`watchface.c` — for the pieces every face needs that aren't face-specific:

- `watchface_add_background(parent)` — background image (see above).
- `watchface_build_battery_widget(parent, &out_icon, &out_pct_label, &out_charge_label)`
  — creates the standard battery icon/percent-label/charge-glyph trio; stash
  the three returned pointers in your file-scope statics.
- `watchface_update_battery_widget(icon, pct_label, charge_label, vbus_in, charging, battery_percent)`
  — call this directly from your `update_power`; it's the entire
  implementation faces 1 and 2 need.
- `watchface_update_hour_label(label_hour, label_ampm)` — formats the current
  hour per the user's 12h/24h preference and shows/hides the AM/PM label to
  match; call it first thing in `update_time`.

See [face_1.c](face_1.c) for a face that uses all four — it contains nothing
but its own layout, styling, and the date/weekday/second labels that are
genuinely face-specific. **If you find yourself writing logic that doesn't
depend on your face's particular layout, it probably belongs here instead —
that's what keeps adding `face_N` cheap.**

## Custom artwork (transparency, icons, dial/hand art, anything beyond labels)

There's no runtime PNG decoding on this device (`CONFIG_LV_USE_LODEPNG=n`) —
all images are **compiled in** as `lv_image_dsc_t` C arrays. The pipeline,
already used for every notification icon and background:

1. Design your art as a PNG with an alpha channel (transparency = the chosen
   background shows through).
2. Convert it with the project's existing tool
   ([ui_assets/README.MD](../../../../ui_assets/README.MD)):
   ```
   python LVGLImage.py your_art.png --ofmt C --cf RGB565A8 -o ../components/gui/icons
   ```
   `RGB565A8` is a real RGB+alpha format — unlike "alpha-only" formats, LVGL
   can transform/rotate it, and `CONFIG_LV_DRAW_SW_SUPPORT_RGB565A8=y` is
   already enabled in `sdkconfig`.
3. The generated `.c` file lands in
   [components/gui/icons/](../../icons/) and is **picked up automatically** —
   `CMakeLists.txt` globs `icons/*.c` (and `src/faces/*.c`) — see
   [gui/CMakeLists.txt](../../CMakeLists.txt). No build-file edits needed.
4. The descriptor symbol matches the source filename, e.g. `your_art.png` →
   `your_art.c` → `LV_IMAGE_DECLARE(your_art); lv_image_set_src(img, &your_art);`
   (this is exactly how `background_wf`/`image_battery_icon`/etc. are wired —
   see any file in `icons/`).

## Rotating images (clock hands, dials, anything that needs to spin)

LVGL 9.3 images can be rotated in place:

```c
lv_image_set_rotation(obj, angle_in_tenths_of_a_degree); // 0..3600, clockwise
lv_image_set_pivot(obj, x, y);                           // rotation center; rarely needed — see below
```

**The default pivot is the image's own geometric center**
(`lv_point_set(&img->pivot, LV_PCT(50), LV_PCT(50))` —
[lv_image.c:656](../../../../managed_components/lvgl__lvgl/src/widgets/image/lv_image.c#L656)).
So: **draw hand/pointer artwork on a canvas that's symmetric around the
rotation point** (e.g. a hand reaching 150px from its pivot belongs on a
~300×300 canvas with the pivot at dead center) and `lv_obj_set_align(hand,
LV_ALIGN_CENTER)` — LVGL then rotates it correctly with zero positioning math.
Only call `lv_image_set_pivot()` if your art is asymmetric and you need to
move the rotation point off-center.

Stack order matters for overlapping elements — widgets created later draw on
top, so e.g. for clock hands create hour → minute → second → center-cap, in
that order.

## Battery / power indicator

Don't build this by hand — call `watchface_build_battery_widget()` from
`build()` and `watchface_update_battery_widget()` from `update_power` (see
"Shared face helpers" above, and [face_1.c:79](face_1.c#L79) /
[face_1.c:96-99](face_1.c#L96-L99) for the two call sites). It gives you the
standard icon (grey idle / blue on VBUS / green charging), `%` label, and
hidden-unless-charging lightning-bolt glyph, all positioned and styled to
match every other face.

## Display geometry

`BSP_LCD_H_RES` × `BSP_LCD_V_RES` = **410 × 502** — a rectangular AMOLED panel
with rounded corners (see
[esp32_s3_touch_amoled_2_06.h](../../../../managed_components/esp32_s3_touch_amoled_2_06/include/bsp/esp32_s3_touch_amoled_2_06.h),
and the photos in the project [README](../../../../README.MD)). It is **not**
round — circular dial art drawn within it leaves the four corners free, which
is exactly where this UI already places overlay info (battery top-left in
every existing face).

## Checklist: adding `face_N`

1. Create `components/gui/src/faces/face_N.c`.
2. `#include "watchface_iface.h"`, `"rtc_lib.h"`, `"settings.h"`, `"ui_fonts.h"`
   (if using the project fonts — see
   [ui_fonts.h](../../include/ui_fonts.h) for the full set:
   `font_normal_*`, `font_bold_*`, `font_numbers_*`), `"lvgl.h"`.
3. Declare your widget pointers as file-scope `static lv_obj_t*`.
4. Implement `faceN_build`, `faceN_update_time`, `faceN_update_power` —
   calling out to `watchface_add_background()`, `watchface_build_battery_widget()`
   /`watchface_update_battery_widget()`, and `watchface_update_hour_label()`
   (see "Shared face helpers" above) for everything generic, so your file
   holds only what's actually specific to this face.
5. Export `const watchface_iface_t watchface_faceN = { .name = "...", .build = faceN_build, .update_time = faceN_update_time, .update_power = faceN_update_power };`
6. In [watchface.c](../watchface.c): add `extern const watchface_iface_t watchface_faceN;`
   near the existing externs and append `&watchface_faceN` to `s_faces[]`.
7. Build wiring: none — both `src/faces/*.c` and `icons/*.c` are auto-globbed.
8. Build and flash manually, then in Settings → Watchface select your new face
   and confirm: background cycles correctly, time/date/weekday update each
   second, battery icon/percentage/charge glyph track real power state, and
   switching away and back rebuilds cleanly with no leftover widgets.

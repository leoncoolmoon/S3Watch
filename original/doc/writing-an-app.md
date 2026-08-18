# Writing an App for S3Watch

Apps are full-screen LVGL UIs launched from the app picker (tile 0,2) into the dynamic app tile (tile 1,2). The user taps an icon, your `_create()` function is called with the tile as parent, and your screen fills it. Swiping left or pressing the hardware back button tears the screen down. Playback, timers, and other background state should survive the tear-down — the user may return.

---

## Files to create

```
components/gui/include/foo_app.h        ← public header
components/gui/src/foo_app.c            ← implementation
components/gui/icons/image_foo_icon.c   ← 96×96 app picker icon
```

No CMakeLists changes needed — the gui component auto-discovers all `.c` files via `GLOB_RECURSE`.

---

## Header

```c
// components/gui/include/foo_app.h
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void foo_app_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
```

`extern "C"` guards are required — `main.cpp` is C++ and headers without them cause linker name-mangling errors.

---

## Implementation skeleton

```c
// components/gui/src/foo_app.c
#include "foo_app.h"
#include "ui_fonts.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "foo_app";

// ---------------------------------------------------------------------------
// Persistent state — survives screen destroy/recreate
// ---------------------------------------------------------------------------

static int s_my_value = 0;

// ---------------------------------------------------------------------------
// Live UI pointers — NULL while screen is not mounted
// ---------------------------------------------------------------------------

static lv_obj_t   *s_lbl_display = NULL;
static lv_timer_t *s_timer       = NULL;

// ---------------------------------------------------------------------------
// Logic
// ---------------------------------------------------------------------------

static void refresh_display(void)
{
    if (!s_lbl_display) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", s_my_value);
    lv_label_set_text(s_lbl_display, buf);
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_display();
}

// ---------------------------------------------------------------------------
// Button callbacks
// ---------------------------------------------------------------------------

static void btn_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_wait_release(lv_indev_active());   // prevent touch bleed-through
    s_my_value++;
    refresh_display();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static void on_delete(lv_event_t *e)
{
    (void)e;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_lbl_display = NULL;
    // NOTE: s_my_value is intentionally left intact
}

// ---------------------------------------------------------------------------
// Screen construction
// ---------------------------------------------------------------------------

void foo_app_create(lv_obj_t *parent)
{
    // Root container — full tile, black bg, flex column
    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_center(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, on_delete, LV_EVENT_DELETE, NULL);

    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_pad_top(title, 14, 0);
    lv_obj_set_style_pad_bottom(title, 8, 0);
    lv_label_set_text(title, "Foo");
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Display label
    s_lbl_display = lv_label_create(screen);
    lv_obj_set_style_text_font(s_lbl_display, &font_numbers_80, 0);
    lv_obj_set_style_text_color(s_lbl_display, lv_color_white(), 0);
    lv_obj_set_flex_grow(s_lbl_display, 1);

    // Button
    lv_obj_t *btn = lv_obj_create(screen);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, lv_pct(60), 58);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x27AE60), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_pad_bottom(btn, 16, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &font_bold_32, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, "Tap me");
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

    // 1 s refresh timer
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_timer = lv_timer_create(timer_cb, 1000, NULL);

    refresh_display();
}
```

---

## Register in the app picker

Open [`components/gui/src/app_picker.c`](../components/gui/src/app_picker.c) and make three additions:

```c
// 1. Include the header
#include "foo_app.h"

// 2. Declare the icon
LV_IMAGE_DECLARE(image_foo_icon);

// 3. Launch wrapper + s_apps[] entry
static void launch_foo(lv_obj_t *tile) { foo_app_create(tile); }

static const app_entry_t s_apps[] = {
    ...
    { "Foo", &image_foo_icon, launch_foo },
};
```

---

## Creating the icon

App picker icons: **96×96 px RGBA PNG** → `LV_COLOR_FORMAT_RGB565A8` C array.

### Convert with Python + Pillow

```python
from PIL import Image
import struct, sys

img = Image.open("foo_icon.png").convert("RGBA").resize((96, 96))
w, h = img.size
rgb565 = []
alpha  = []
for y in range(h):
    for x in range(w):
        r, g, b, a = img.getpixel((x, y))
        rgb565.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
        alpha.append(a)

with open("image_foo_icon.c", "w") as f:
    f.write('#include "lvgl.h"\n\n')
    f.write("static const uint8_t image_foo_icon_map[] = {\n")
    # RGB565 plane (stride = 96 * 2 = 192 bytes per row)
    row = []
    for i, px in enumerate(rgb565):
        row += [px & 0xFF, (px >> 8) & 0xFF]
        if len(row) == 192:
            f.write("    " + ",".join(f"0x{b:02x}" for b in row) + ",\n")
            row = []
    # Alpha plane
    row = []
    for i, a in enumerate(alpha):
        row.append(a)
        if len(row) == 96:
            f.write("    " + ",".join(f"0x{b:02x}" for b in row) + ",\n")
            row = []
    f.write("};\n\n")
    f.write("const lv_image_dsc_t image_foo_icon = {\n")
    f.write("  .header = {\n")
    f.write("    .magic      = LV_IMAGE_HEADER_MAGIC,\n")
    f.write("    .cf         = LV_COLOR_FORMAT_RGB565A8,\n")
    f.write("    .flags      = 0,\n")
    f.write("    .w = 96, .h = 96, .stride = 192,\n")
    f.write("    .reserved_2 = 0,\n")
    f.write("  },\n")
    f.write("  .data_size = sizeof(image_foo_icon_map),\n")
    f.write("  .data      = image_foo_icon_map,\n")
    f.write("  .reserved  = NULL,\n")
    f.write("};\n")
```

Save the output to `components/gui/icons/image_foo_icon.c`.

**Data layout:** all RGB565 pixels first (stride × h = 192 × 96 = 18 432 bytes), then all alpha bytes (96 × 96 = 9 216 bytes) = 27 648 bytes total.

---

## Patterns

### Persistent state vs live UI pointers

Split your state into two groups and comment them clearly:

```c
// Persistent — survives screen destroy/recreate
static int s_count = 0;

// Live — only valid while screen is mounted; NULL otherwise
static lv_obj_t *s_lbl = NULL;
```

Always guard live pointer writes:

```c
static void refresh(void) {
    if (!s_lbl) return;   // screen not mounted — skip
    lv_label_set_text(s_lbl, "...");
}
```

In `on_delete`, null every live pointer and delete any `lv_timer`:

```c
static void on_delete(lv_event_t *e) {
    (void)e;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_lbl = NULL;
}
```

Do **not** reset persistent state in `on_delete` unless you explicitly want a reset (e.g. a navigation app that should not resume mid-session).

### Periodic refresh with `lv_timer`

```c
static lv_timer_t *s_timer = NULL;

static void timer_cb(lv_timer_t *t) { (void)t; refresh_display(); }

// In _create():
if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }   // guard re-entry
s_timer = lv_timer_create(timer_cb, 500, NULL);            // every 500 ms

// In on_delete():
if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
```

`lv_timer` callbacks run on the LVGL task — no lock needed.

### Scrollable list

```c
lv_obj_t *list = lv_obj_create(screen);
lv_obj_remove_style_all(list);
lv_obj_set_width(list, lv_pct(100));
lv_obj_set_flex_grow(list, 1);               // fills remaining height
lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
lv_obj_set_scroll_dir(list, LV_DIR_VER);
lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
```

Add rows with `lv_obj_create(list)` + fixed height + your label/icon children.

Use `lv_obj_move_to_index(row, 0)` to prepend a row at the top.

### Two-button row (Start / Stop style)

```c
lv_obj_t *btn_row = lv_obj_create(screen);
lv_obj_remove_style_all(btn_row);
lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_set_style_pad_bottom(btn_row, 12, 0);
```

Individual buttons at 46% width (`lv_obj_set_width(btn, lv_pct(46))`) sit evenly in the row — see `stopwatch.c::make_button`.

### Reading user settings

```c
#include "settings.h"
bool use_24h = settings_get_time_24h();
uint8_t brightness = settings_get_brightness();
```

---

## Fonts quick reference

| Symbol | Size | Use for |
|--------|------|---------|
| `font_bold_32` | 30 px | Titles, button labels |
| `font_bold_42` | 40 px | Large display text, mixed alpha-numeric |
| `font_normal_26` | 26 px | Secondary labels, list items |
| `font_numbers_80` | 80 px | Primary numeric display (digits, `:`, `-`, `/`, `%`) |
| `font_numbers_120` | 120 px | Oversized numeric (digits, `:`, `-`, `/`) |

`font_numbers_*` contain **digits and a few symbols only**. For "Error", "AM", unit labels alongside numbers, use `font_bold_42`.

---

## Display geometry

The panel is **480×480 round**. The four corners are physically clipped.

- Keep interactive elements ≥ 50 px from each screen corner.
- Buttons at the left/right edge near y ≈ 240 (the equator) are safe.
- For rows near the top or bottom, add horizontal padding ≥ 8 px.
- Reference: the music app back button sits at `LV_ALIGN_LEFT_MID, 50, 0`.

---

## Checklist before shipping

- [ ] `extern "C"` guards in the header
- [ ] `on_delete` callback registered on the root screen object
- [ ] Every `lv_timer` deleted in `on_delete`
- [ ] Every live pointer nulled in `on_delete`
- [ ] `lv_indev_wait_release` at the top of every button click callback
- [ ] Persistent state intentionally preserved (or intentionally reset)
- [ ] Icon at 96×96, `LV_COLOR_FORMAT_RGB565A8`, in `icons/`
- [ ] Entry added to `s_apps[]` in `app_picker.c`
- [ ] App and icon added to `components/gui/README.md` table
- [ ] App bullet added to `README.md` app picker section

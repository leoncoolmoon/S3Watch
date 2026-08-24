# Dynamic Font Directory (`/font/`)

This directory is used to store pre-converted `.bin` font files for runtime dynamic loading using `lv.binfont_create(...)`.

## Available Fonts Reference

| Font Family | Size (px) | Filename | Size | Description |
|---|---:|---|---:|---|
| Chicago | 24 | `chicago24.bin` | ~1.8 KB | Retro Chicago style |
| Chicago | 36 | `chicago36.bin` | ~3.4 KB | Retro Chicago style |
| Chicago | 48 | `chicago48.bin` | ~5.4 KB | Retro Chicago style |
| Cozette | 24 | `cozette24.bin` | ~139 KB | Pixel monospace font |
| Cozette | 36 | `cozette36.bin` | ~268 KB | Pixel monospace font |
| Cozette | 48 | `cozette48.bin` | ~434 KB | Pixel monospace font |
| Montserrat | 18 | `lv_font_montserrat_18.bin` | ~3.4 KB | Extended Montserrat |
| Montserrat | 20 | `lv_font_montserrat_20.bin` | ~4.1 KB | Extended Montserrat |
| Montserrat | 24 | `lv_font_montserrat_24.bin` | ~5.3 KB | Extended Montserrat |
| Montserrat | 24 | `montserrat24.bin` | ~9.8 KB | Extended Montserrat |
| Montserrat | 36 | `montserrat36.bin` | ~20 KB | Extended Montserrat |
| Montserrat | 48 | `montserrat48.bin` | ~36 KB | Extended Montserrat |
| Tabler | 36 | `tabler36.bin` | ~1.4 KB | Minimalist font / icons |
| Tabler | 48 | `tabler48.bin` | ~2.4 KB | Minimalist font / icons |

## Usage Example

```python
import lvgl as lv

# Load font file from filesystem
font = lv.binfont_create("/font/montserrat24.bin")

label = lv.label(lv.screen_active())
label.set_style_text_font(font, 0)
label.set_text("Hello S3Watch!")
```

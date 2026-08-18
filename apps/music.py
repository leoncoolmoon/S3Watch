"""
Music Player Application for MicroPython LVGL v9
Audio player interface with transport controls and track metadata.
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class MusicApp:
    def __init__(self, parent_tile, ui_manager):
        self.parent = parent_tile
        self.ui_manager = ui_manager
        self.container = None
        self.playing = False
        self.track_title = "Sample Track"
        self.artist = "Artist Name"
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x150515), 0)

        title = lv.label(self.container)
        title.set_text("Now Playing")
        title.align(lv.ALIGN.TOP_MID, 0, 15)

        lbl_track = lv.label(self.container)
        lbl_track.set_text(f"{self.track_title}\n{self.artist}")
        lbl_track.center()

        # Transport controls
        btn_prev = lv.button(self.container)
        btn_prev.set_size(70, 50)
        btn_prev.align(lv.ALIGN.BOTTOM_LEFT, 30, -40)
        lbl_p = lv.label(btn_prev)
        lbl_p.set_text("<<")
        lbl_p.center()

        btn_play = lv.button(self.container)
        btn_play.set_size(80, 50)
        btn_play.align(lv.ALIGN.BOTTOM_MID, 0, -40)
        lbl_pl = lv.label(btn_play)
        lbl_pl.set_text("Play")
        lbl_pl.center()

        btn_next = lv.button(self.container)
        btn_next.set_size(70, 50)
        btn_next.align(lv.ALIGN.BOTTOM_RIGHT, -30, -40)
        lbl_n = lv.label(btn_next)
        lbl_n.set_text(">>")
        lbl_n.center()

    def clean(self):
        if self.container:
            self.container.delete()

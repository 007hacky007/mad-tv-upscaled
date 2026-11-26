# dmgbuild settings for the Mad TV disk image.
# Usage: python -m dmgbuild -s packaging/macos/dmg/settings.py -D app=dist/MadTV.app \
#   -D assets=packaging/macos/dmg -D icon=packaging/icons/app-icon.icns "Mad TV Upscaled" dist/MadTV-macOS.dmg
import os
import subprocess
import tempfile

here = os.path.abspath(defines.get("assets", "packaging/macos/dmg"))  # noqa: F821
app = defines["app"]  # noqa: F821 - provided by dmgbuild
app_name = os.path.basename(app.rstrip("/"))

# HiDPI background: the 1x and 2x PNGs combined into one multi-representation TIFF.
_tiff = os.path.join(tempfile.mkdtemp(prefix="mtv-dmg-"), "background.tiff")
subprocess.check_call([
    "tiffutil", "-cathidpicheck",
    os.path.join(here, "background.png"),
    os.path.join(here, "background@2x.png"),
    "-out", _tiff,
])

format = "UDZO"
files = [app]
symlinks = {"Applications": "/Applications"}
icon = defines.get("icon")  # noqa: F821
background = _tiff
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
# 600 background + 28 title bar.
window_rect = ((200, 120), (760, 628))
default_view = "icon-view"
icon_size = 86
text_size = 12
# Icon centres, inside the slots drawn on the background.
icon_locations = {app_name: (100, 100), "Applications": (300, 100)}

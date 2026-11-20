"""Convert the renderer screenshots (PPM) in a folder to PNG, if Pillow is installed."""
import sys, pathlib
try:
    from PIL import Image
except ImportError:
    sys.exit(0)
for p in pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "out").glob("*.ppm"):
    Image.open(p).save(p.with_suffix(".png"))
    p.unlink()

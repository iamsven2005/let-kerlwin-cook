#!/usr/bin/env python3
"""Build docs/screens.png from the host-test screenshots (run `make -C test` first)."""
import sys, os
from PIL import Image, ImageDraw
OUT = sys.argv[1] if len(sys.argv) > 1 else "test/out"
PANELS = [
    ("18_splash", "boot splash (3 s)"), ("15_disconnected", "DISCONNECTED"), ("01_card_UUR_head", "CARD:UUR SEQ:H REG:0010"),
    ("02_square_RDL_H2", "square CARD:RDL SEQ:H2"), ("06_IF_O_LLU", "CARD:IF:O:LLU"), ("09_FOR", "CARD:FOR + HINT"),
    ("12_msg_hint_nohub", "MSG + HINT + NO HUB"), ("14_status_acc_bat", "ACC + low battery"), ("04c_hint_14_caps", "HINT, 14 chars"),
    ("14b_tilt_armed_left", "tilt armed: left (v1.7)"), ("14c_tilt_armed_right", "tilt armed: right (v1.7)"), ("10_waiting", "CARD:- (waiting)"),
]
S, LBL, GAP, COLS = 3, 18, 12, 3
pw, ph = 96 * S, 64 * S + LBL
rows = (len(PANELS) + COLS - 1) // COLS
sheet = Image.new("RGB", (COLS * pw + (COLS - 1) * GAP, rows * ph + (rows - 1) * 6), (34, 34, 34))
d = ImageDraw.Draw(sheet)
for i, (f, label) in enumerate(PANELS):
    x, y = (i % COLS) * (pw + GAP), (i // COLS) * (ph + 6)
    im = Image.open(os.path.join(OUT, f + ".png")).convert("RGB").resize((96 * S, 64 * S), Image.NEAREST)
    sheet.paste(im, (x, y + LBL))
    d.text((x + 2, y + 3), label, fill=(220, 220, 220))
sheet.save("docs/screens.png")
print("docs/screens.png", sheet.size)

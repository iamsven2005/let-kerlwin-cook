#!/usr/bin/env python3
"""Command Card Maze Race — desktop hub. Run `python main.py --help` for options."""
import os

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

from ccmaze.ui.app import main  # noqa: E402

if __name__ == "__main__":
    main()

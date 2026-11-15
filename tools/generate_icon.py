"""Vector source for ergo-active's running figure and urgency tray badges."""

import sys
from pathlib import Path

sys.dont_write_bytecode = True
from iconlib import Canvas, generate

ROOT = Path(__file__).resolve().parents[1]


def draw(canvas: Canvas) -> None:
    """Keep the running figure, on a green disc visible on light and dark taskbars."""
    white = (249, 253, 250)
    canvas.circle(50, 50, 48, (38, 157, 121))
    canvas.circle(62, 20, 8, white)
    canvas.line(((52, 36), (43, 53)), 10, white)
    canvas.line(((51, 35), (31, 35), (22, 49)), 8, white)
    canvas.line(((51, 35), (72, 47), (81, 33)), 8, white)
    canvas.line(((44, 53), (30, 73), (15, 73)), 9, white)
    canvas.line(((44, 53), (63, 66), (62, 86)), 9, white)


def tray(canvas: Canvas, level: int) -> None:
    """The disc and pixel check used by desktop_tray::make_icon, in its 16-unit view."""
    colour = ((78, 201, 111), (224, 160, 32), (224, 64, 64))[level]
    canvas.circle(50, 50, 46.875, colour)
    for x, y in [(5 + i, 9 + i) for i in range(4)] + [(8 + i, 12 - i) for i in range(5)]:
        canvas.rect(x * 6.25, y * 6.25, 6.25, 6.25, (255, 255, 255))


if __name__ == "__main__":
    raise SystemExit(generate(draw, ROOT / "src/res/ergo-active.ico", variants={
        "tray-green": lambda canvas: tray(canvas, 0),
        "tray-amber": lambda canvas: tray(canvas, 1),
        "tray-red": lambda canvas: tray(canvas, 2),
    }))

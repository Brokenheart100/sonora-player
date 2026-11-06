"""生成 Sonora 的应用图标。

设计：漫画分格里的黑胶唱片 —— 米白纸张、粗黑描边、红色标签、加一道黄色闪电。
每一档尺寸都用 LANCZOS 从 2048 的母版缩下来，16px 也还认得出"黑盘 + 红心 + 黄闪电"。

输出：
    assets/icons/sonora.ico   多尺寸（16/24/32/48/64/128/256），PNG 压缩条目
    assets/icons/sonora.png   512px，给文档与运行期 setWindowIcon 用
"""

from __future__ import annotations

import io
import struct
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 2048
ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "assets" / "icons"

PAPER = (244, 239, 228, 255)
INK = (20, 19, 22, 255)
VINYL = (26, 25, 29, 255)
GROOVE = (44, 42, 50, 255)
RED = (226, 58, 46, 255)
YELLOW = (255, 210, 63, 255)
WHITE = (250, 247, 240, 255)


def draw_master() -> Image.Image:
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    # 分格：圆角纸片 + 粗黑描边。outline 画在内侧，避免被画布裁掉。
    margin = 56
    border = 92
    draw.rounded_rectangle(
        (margin, margin, SIZE - margin, SIZE - margin),
        radius=300,
        fill=PAPER,
        outline=INK,
        width=border,
    )

    # 黑胶唱片
    cx, cy, r = 790, 1140, 520
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=VINYL, outline=INK, width=26)
    for groove_r in (r - 70, r - 140, r - 210, r - 280):
        draw.ellipse(
            (cx - groove_r, cy - groove_r, cx + groove_r, cy + groove_r),
            outline=GROOVE,
            width=7,
        )

    # 标签与轴孔
    label_r = 190
    draw.ellipse(
        (cx - label_r, cy - label_r, cx + label_r, cy + label_r),
        fill=RED,
        outline=WHITE,
        width=26,
    )
    hole_r = 46
    draw.ellipse(
        (cx - hole_r, cy - hole_r, cx + hole_r, cy + hole_r),
        fill=PAPER,
        outline=INK,
        width=14,
    )

    # 盘面高光：两道弧
    draw.arc(
        (cx - r + 120, cy - r + 120, cx + r - 120, cy + r - 120),
        start=196,
        end=262,
        fill=(255, 255, 255, 70),
        width=18,
    )

    # 闪电：压在唱片右上角，像是从盘里蹦出来的
    bolt = [
        (1430, 300),
        (1170, 830),
        (1330, 830),
        (1160, 1240),
        (1770, 690),
        (1540, 690),
        (1700, 300),
    ]
    draw.polygon(bolt, fill=YELLOW, outline=INK, width=52)

    # 三颗四角星，收一下画面右上角的空
    for (sx, sy, sr) in ((1750, 1520, 52), (620, 480, 34)):
        draw.polygon(
            [
                (sx, sy - sr),
                (sx + sr * 0.34, sy - sr * 0.34),
                (sx + sr, sy),
                (sx + sr * 0.34, sy + sr * 0.34),
                (sx, sy + sr),
                (sx - sr * 0.34, sy + sr * 0.34),
                (sx - sr, sy),
                (sx - sr * 0.34, sy - sr * 0.34),
            ],
            fill=WHITE,
            outline=INK,
            width=18,
        )

    return image


def png_bytes(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def write_ico(master: Image.Image, path: Path, sizes: tuple[int, ...]) -> None:
    """手写 ICO 容器：每档尺寸都用 LANCZOS 单独缩，不交给 Pillow 的默认缩放。"""
    entries = []
    payloads = []
    offset = 6 + 16 * len(sizes)
    for size in sizes:
        frame = master.resize((size, size), Image.LANCZOS)
        data = png_bytes(frame)
        entries.append((size, len(data), offset))
        payloads.append(data)
        offset += len(data)

    out = bytearray(struct.pack("<HHH", 0, 1, len(sizes)))
    for size, length, start in entries:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, length, start)
    for data in payloads:
        out += data
    path.write_bytes(bytes(out))


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    master = draw_master()

    write_ico(master, OUT_DIR / "sonora.ico", (16, 24, 32, 48, 64, 128, 256))
    master.resize((512, 512), Image.LANCZOS).save(OUT_DIR / "sonora.png", optimize=True)

    for size in (16, 32, 48, 256):
        frame = master.resize((size, size), Image.LANCZOS)
        frame.save(OUT_DIR / f"preview-{size}.png", optimize=True)

    print(f"icon written: {OUT_DIR / 'sonora.ico'} ({(OUT_DIR / 'sonora.ico').stat().st_size} bytes)")
    print(f"png written:  {OUT_DIR / 'sonora.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

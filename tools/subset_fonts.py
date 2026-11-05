#!/usr/bin/env python3
"""把展示字体裁成只含实际用到的字。

为什么需要：ZCOOL KuaiLe 全字重 1.5MB，而 MambaMusic 里真正用到的字形不到 200 个。
裁完通常 20–40KB，首帧少读 1.5MB，qrc 也小一截。

用法：
    python tools/subset_fonts.py            # 裁切，覆盖 assets/fonts/*-Subset.ttf
    python tools/subset_fonts.py --check    # 只报告，不写文件

裁切范围 = 所有 .qml 源文件里出现过的字符 + ASCII 可打印区 + 少量中文标点。
**加了新文案就要重跑一次**，否则新字会掉回系统字体（视觉上会明显不一致）。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
QML_DIR = ROOT / "presentation" / "qml"
SOURCE_FONT = ROOT / "assets" / "fonts" / "ZCOOLKuaiLe-Regular.ttf"
SUBSET_FONT = ROOT / "assets" / "fonts" / "ZCOOLKuaiLe-Subset.ttf"

# 系统可能缺、但设计上一定要有的字符（标点、符号、全角空格）
EXTRA_CHARS = (
    "　！？。，、；：…—～·「」『』（）《》【】"
    "0123456789"
    "+-=*/\\|@#$%&()[]{}<>?!.,;:'\"`~^_"
    "—–…«»“”‘’"
)


def collect_chars() -> str:
    """扫所有 QML，收集用到的字符。"""
    chars: set[str] = set(EXTRA_CHARS)
    chars.update(chr(c) for c in range(0x20, 0x7F))  # ASCII 可打印

    for path in sorted(QML_DIR.rglob("*.qml")):
        chars.update(path.read_text(encoding="utf-8"))
    for path in sorted(QML_DIR.rglob("qmldir")):
        chars.update(path.read_text(encoding="utf-8"))

    # 控制字符和空白不用进字体
    chars = {c for c in chars if c.isprintable() or c == "\u3000"}
    return "".join(sorted(chars))


def main() -> int:
    parser = argparse.ArgumentParser(description="裁切展示字体")
    parser.add_argument("--check", action="store_true", help="只报告，不写文件")
    args = parser.parse_args()

    if not SOURCE_FONT.is_file():
        print(f"找不到源字体：{SOURCE_FONT}", file=sys.stderr)
        return 1

    text = collect_chars()

    options = subset.Options()
    options.layout_features = ["*"]      # 保留 OpenType 特性（字距、替代字形）
    options.name_IDs = ["*"]             # 保留字体名，FontLoader 才能取到 family
    options.notdef_outline = True        # 缺字时给个方块，不要静默空掉

    font = TTFont(SOURCE_FONT)
    font_set = subset.Subsetter(options)
    font_set.populate(text=text)
    font_set.subset(font)

    original_kb = SOURCE_FONT.stat().st_size / 1024

    if args.check:
        print(f"字符数 {len(text)}，源字体 {original_kb:.0f} KB")
        return 0

    SUBSET_FONT.parent.mkdir(parents=True, exist_ok=True)
    font.save(SUBSET_FONT)
    subset_kb = SUBSET_FONT.stat().st_size / 1024
    print(f"字符数 {len(text)}")
    print(f"{SOURCE_FONT.name}: {original_kb:.0f} KB")
    print(f"{SUBSET_FONT.name}: {subset_kb:.0f} KB（{subset_kb / original_kb * 100:.0f}%）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

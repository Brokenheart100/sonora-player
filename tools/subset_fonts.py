#!/usr/bin/env python3
"""把展示字体裁成只含实际用到的字。

为什么需要：ZCOOL KuaiLe 全字重 1.5MB，而 Sonora 里真正用到的字形不到一千个。
裁完约 150KB，首帧少读 1.3MB，qrc 也小一截。

用法：
    python tools/subset_fonts.py            # 裁切，覆盖 assets/fonts/*-Subset.ttf
    python tools/subset_fonts.py --check    # 只报告字符数，不写文件
    python tools/subset_fonts.py --verify   # 检查已提交的子集是否覆盖全部字符（CI 用）

裁切范围 = 所有 .qml 源文件里出现过的字符 + C++ 字符串字面量里的字符
+ ASCII 可打印区 + 少量中文标点。
**加了新文案就要重跑一次**（界面文案或 C++ 里的状态标签都算），
否则新字会掉回系统字体（视觉上会明显不一致）。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
QML_DIR = ROOT / "presentation" / "qml"
SOURCE_FONT = ROOT / "assets" / "fonts" / "ZCOOLKuaiLe-Regular.ttf"
SUBSET_FONT = ROOT / "assets" / "fonts" / "ZCOOLKuaiLe-Subset.ttf"

# 会显示到界面上的 C++ 源码位置。与 xmake/targets.lua 的 target 图对应：
# presentation 是 ViewModel、host 是组合根、其余是各层的实现。
CPP_SOURCES = (
    "presentation/src/*.cpp",
    "presentation/include/**/*.h",
    "host/*.cpp",
    "modules/*/*/src/*.cpp",
    "modules/*/*/include/**/*.h",
    "platform/*/src/*.cpp",
    "platform/*/include/**/*.h",
)

# 普通字符串字面量；转义序列整体跳过（\\" 不会把引号当成结尾）。
C_STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')

# 系统可能缺、但设计上一定要有的字符（标点、符号、全角空格）
EXTRA_CHARS = (
    "　！？。，、；：…—～·「」『』（）《》【】"
    "0123456789"
    "+-=*/\\|@#$%&()[]{}<>?!.,;:'\"`~^_"
    "—–…«»“”‘’"
)


def collect_source_chars() -> set[str]:
    """扫 C++ 里的字符串字面量，收集会显示到界面上的中文。

    为什么不能只扫 QML：状态标签（"播放中" / "已暂停"）、空列表文案、
    错误提示这些是 C++ 写好、QML 直接显示的。只按 QML 裁字体会让它们
    掉回系统字体——同一行里一半手写体一半黑体，视觉上非常刺眼。

    只取引号里的内容、不扫整个文件，是为了不把注释里的中文也拖进字体
    （注释远比界面文案多，一张全字体能把 80KB 撑成 500KB）。
    """
    chars: set[str] = set()
    for pattern in CPP_SOURCES:
        for path in sorted(ROOT.glob(pattern)):
            text = path.read_text(encoding="utf-8")
            for match in C_STRING_LITERAL.finditer(text):
                chars.update(ch for ch in match.group(1) if ord(ch) > 0x7F)
    return chars


def collect_chars() -> str:
    """扫所有 QML 与 C++ 字面量，收集用到的字符。"""
    chars: set[str] = set(EXTRA_CHARS)
    chars.update(chr(c) for c in range(0x20, 0x7F))  # ASCII 可打印

    for path in sorted(QML_DIR.rglob("*.qml")):
        chars.update(path.read_text(encoding="utf-8"))
    for path in sorted(QML_DIR.rglob("qmldir")):
        chars.update(path.read_text(encoding="utf-8"))

    chars |= collect_source_chars()

    # 控制字符和空白不用进字体
    chars = {c for c in chars if c.isprintable() or c == "\u3000"}
    return "".join(sorted(chars))


def verify(text: str) -> int:
    """检查已提交的子集是否覆盖了全部需要显示的字符（CI 用）。

    为什么不比字节：字体子集的字节依赖 fontTools 版本，CI 的 Python 版本
    与本机不同就可能产出"内容一样、字节不同"的文件，那种失败没人会去修。
    真正要守的不变量是「界面上的每个字都在字体里」，这里就只查这个。

    源字体本身就没有的字（比如箭头 →）不算失败：那是字体设计问题，
    重跑多少次都补不上，只能靠回落字体。
    """
    if not SUBSET_FONT.is_file():
        print(f"找不到子集字体：{SUBSET_FONT}，先跑一次 python tools/subset_fonts.py", file=sys.stderr)
        return 1

    source_cmap = set(TTFont(SOURCE_FONT).getBestCmap())
    subset_cmap = set(TTFont(SUBSET_FONT).getBestCmap())

    required = [ch for ch in text if ord(ch) in source_cmap]
    uncovered = [ch for ch in required if ord(ch) not in subset_cmap]
    unsupported = [ch for ch in text if ord(ch) not in source_cmap]

    subset_kb = SUBSET_FONT.stat().st_size / 1024
    print(f"需要 {len(required)} 字，子集里 {len(subset_cmap)} 字形，{subset_kb:.0f} KB")

    if unsupported:
        print(f"源字体没有、只能回落系统字体：{''.join(unsupported)}")

    if uncovered:
        print(f"缺失 {len(uncovered)} 字：{''.join(uncovered)}", file=sys.stderr)
        print("跑一次 python tools/subset_fonts.py 重新裁切并提交", file=sys.stderr)
        return 1

    print("覆盖完整")
    return 0


def main() -> int:
    # 这个脚本会打印中文和「» «」这类符号。Windows 控制台默认是 GBK，
    # 直接 print 会以 UnicodeEncodeError 崩掉（CI 里是 UTF-8，看不出来，
    # 本地第一次跑就会遇到）。启动时统一改成 UTF-8。
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    parser = argparse.ArgumentParser(description="裁切展示字体")
    parser.add_argument("--check", action="store_true", help="只报告，不写文件")
    parser.add_argument("--verify", action="store_true", help="检查已提交的子集是否覆盖全部字符（CI 用）")
    args = parser.parse_args()

    if not SOURCE_FONT.is_file():
        print(f"找不到源字体：{SOURCE_FONT}", file=sys.stderr)
        return 1

    text = collect_chars()

    if args.verify:
        return verify(text)

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

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""边界检查 —— 只兜构建系统管不到的那部分。

构建系统（xmake/targets.lua）已经强制了「谁能 include 谁」：include 路径由
target 图授予，不给就找不到头文件。本脚本补的是编译器发现不了的几种腐化：

  1. 纯 C++ 层（kernel / contracts / */core）里出现 Qt、SQLite、FFmpeg、Win32 的
     类型名或宏名 —— 多半是有人把平台代码 copy 进来了，或是把 <QString> 换了个写法
  2. 模块之间看见了对方的 core —— 应该只看见对方的 *.contracts
  3. 表现层 include 了任何 *.core —— ViewModel 不许持有聚合
  4. <windows.h> 出现在 platform/win 之外
  5. 用 "../" 跨 target 相对 include —— 绕开 target 图的经典手法

行长限制：想在某一行放行，在该行加注释 boundary-allow。

用法：
    python xmake/checks/boundaries.py
退出码：0 干净，1 有违规。
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SUFFIXES = {".h", ".hpp", ".hh", ".hxx", ".inl", ".cpp", ".cc", ".cxx"}
# third_party 不入检查：里面有 9MB 的 sqlite3.c，扫它既慢又没意义（它是外部代码，
# 不适用本项目的分层规矩）。
SKIP_DIRS = {"build", ".xmake", ".git", ".vscode", "docs", "node_modules", "out", "third_party"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')
RELATIVE_ESCAPE_RE = re.compile(r'^\s*#\s*include\s*"\.\./')
ALLOW_MARKER = "boundary-allow"

# 纯 C++ 层禁止出现的技术痕迹：(正则, 人类可读的原因)
PURE_FORBIDDEN = [
    (re.compile(r"\bQ[A-Z][A-Za-z0-9_]*\b"), "Qt 类型/宏"),
    (re.compile(r"\b(?:sqlite3|SQLITE_[A-Z0-9_]+)\b"), "SQLite"),
    (
        re.compile(
            r"\b(?:avcodec|avformat|avutil|swresample|swscale|avio|libav\w*|AVCodec\w*|AVFrame)\b"
        ),
        "FFmpeg",
    ),
    (
        re.compile(r"\b(?:HWND|HANDLE|LPWSTR|LPCWSTR|DWORD|HRESULT|WINAPI|LPVOID)\b"),
        "Win32 类型",
    ),
    (re.compile(r"\bpthread\w*\b"), "POSIX 线程"),
]

PURE_LAYERS = {"kernel", "contracts", "core"}


def classify(rel: Path) -> tuple[str | None, str | None]:
    """把文件路径映射成 (模块名, 层名)。返回 (None, None) 表示不受架构规则约束。

    层名的取值：
        kernel / contracts / core / adapters  —— 业务模块内部三层 + 共享内核
        platform                              —— platform/<name>
        presentation                          —— 表现层
    """
    parts = rel.parts
    if not parts:
        return None, None

    if parts[0] == "modules" and len(parts) >= 3:
        name = parts[1]
        if name == "kernel":
            return "kernel", "kernel"
        if name == "contracts":
            return "contracts", "contracts"
        layer = parts[2]
        if layer in ("contracts", "core", "adapters"):
            return name, layer
        return None, None

    if parts[0] == "platform" and len(parts) >= 3:
        return parts[1], "platform"

    if parts[0] == "presentation":
        return "presentation", "presentation"

    return None, None


def include_allowed(module: str, layer: str, include_path: str) -> bool:
    """判断某层的文件能否 include 这个 mamba 头文件。"""
    segments = include_path.split("/")
    if len(segments) < 2 or segments[0] != "mamba":
        return True  # 项目外的头，别的规则不管

    area = segments[1]

    # 共享内核与共享契约：人人可用（它们零外部依赖）
    if area in ("kernel", "contracts"):
        return True

    # 平台层：只有技术层能用；领域层拿到 SQLite 头就全毁了
    if area == "platform":
        return layer in ("adapters", "platform", "presentation")

    # 表现层自己的内部头
    if area == "presentation":
        return module == "presentation"

    if len(segments) < 3:
        return True
    sub = segments[2]

    # 本模块内部
    if area == module:
        if layer == "contracts":
            return sub == "contracts"      # 契约层不许回头看见自己的领域层
        if layer == "core":
            return sub in ("contracts", "core")   # 领域层不许看见自己的适配器
        return True

    # 跨模块：只许看见对方的 contracts。
    # platform 也在这一列：Mamba.Platform.Audio 实现的就是 Playback.Contracts 里的端口，
    # 实现方看得见抽象、看不见对方的领域模型，这正是依赖倒置要的方向。
    if sub == "contracts":
        return layer in ("core", "adapters", "platform", "presentation")
    return False


def iter_sources():
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for filename in filenames:
            path = Path(dirpath) / filename
            if path.suffix.lower() in SOURCE_SUFFIXES:
                yield path


def main() -> int:
    violations: list[str] = []
    scanned = 0

    for path in sorted(iter_sources()):
        rel = path.relative_to(ROOT)
        module, layer = classify(rel)
        scanned += 1

        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            violations.append(f"{rel}: 不是 UTF-8，MSVC 会按本地代码页解析 → 乱码")
            continue

        for lineno, line in enumerate(text.splitlines(), start=1):
            if ALLOW_MARKER in line:
                continue
            where = f"{rel.as_posix()}:{lineno}"

            # 1. 纯 C++ 层不许有技术痕迹
            if layer in PURE_LAYERS:
                for pattern, reason in PURE_FORBIDDEN:
                    for match in pattern.finditer(line):
                        violations.append(
                            f"{where}: {reason} `{match.group(0)}` 侵入了 {layer} 层"
                        )

            # 2. <windows.h> 只许在 platform/win
            match = INCLUDE_RE.match(line)
            if match:
                include_path = match.group(1)
                if include_path.lower() in (
                    "windows.h",
                    "winsock2.h",
                    "winreg.h",
                    "shlobj.h",
                    "knownfolders.h",
                ):
                    if layer != "platform":
                        violations.append(
                            f"{where}: Win32 头 `{include_path}` 只许出现在 platform/win"
                        )

                # 3. mamba 头文件的可见性
                if module is not None and include_path.startswith("mamba/"):
                    if not include_allowed(module, layer, include_path):
                        violations.append(
                            f"{where}: {layer} 层不许 include `{include_path}`"
                            f"（只能看见 kernel/contracts/自身，跨模块只能看见 *.contracts）"
                        )

                # 4. 依赖倒置：高层不许知道实现层的存在
                if layer in ("core", "contracts", "kernel"):
                    lowered = include_path.replace("\\", "/").lower()
                    if (
                        "/adapters/" in lowered
                        or lowered.startswith("adapters/")
                        or "/platform/" in lowered
                        or lowered.startswith("platform/")
                    ):
                        violations.append(
                            f"{where}: {layer} 层 include 了 `{include_path}` —— 依赖倒置反了"
                            f"（接口应在 core 里声明，由 adapters/platform 去实现）"
                        )

            # 5. 跨 target 相对 include
            if RELATIVE_ESCAPE_RE.match(line):
                violations.append(f'{where}: 用 "../" 跨目录 include 绕开了 target 图')

    if violations:
        print(f"边界检查失败：{len(violations)} 处违规（扫描 {scanned} 个文件）\n")
        for violation in violations:
            print(f"  [FAIL] {violation}")
        print("\n这些不是风格问题：每一条都是一条本不该存在的依赖。")
        return 1

    print(f"边界检查通过（{scanned} 个文件）")
    print("  纯 C++ 层无 Qt/SQLite/FFmpeg/Win32 痕迹 · 无跨模块 core 依赖 · 无 ../ 逃逸")
    return 0


if __name__ == "__main__":
    if sys.stdout.encoding and sys.stdout.encoding.lower() not in ("utf-8", "utf8"):
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except AttributeError:  # pragma: no cover - Python < 3.7
            pass
    sys.exit(main())

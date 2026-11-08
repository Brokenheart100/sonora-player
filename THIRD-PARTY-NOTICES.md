# 第三方组件与许可证（Sonora）

Sonora 自身代码以 MIT 发布（见 `LICENSE`）。发布包里另外包含下列第三方组件，
它们各自的许可证只约束自己那份二进制，不改变 Sonora 的许可证。

| 组件 | 版本 | 许可证 | 发布包里是什么 | 义务 |
| --- | --- | --- | --- | --- |
| Qt | 6.12.0 | LGPL v3 | `Qt6*.dll`、`platforms/`、`tls/`、`qml/` | 动态链接；替换 Qt DLL 即可换版本（见下） |
| FFmpeg（libav*） | 9.0.2 | LGPL v2.1+ | `avcodec-*.dll`、`avformat-*.dll`、`avutil-*.dll`、`swresample-*.dll` | 只启用 avcodec/avformat/avutil/swresample，未启用 GPL / nonfree 组件 |
| TagLib | 2.3.2 | LGPL v2.1+ / MPL 2.0 | 静态链接进 `sonora.exe` | 提供本声明与获取源码的途径 |
| SQLite | 3.54.1 | Public Domain | 静态链接进 `sonora.exe` | 无 |
| nlohmann/json | 3.12.0 | MIT | 仅头文件，编译进 `sonora.exe` | 保留版权声明 |
| qr-code-generator | 1.8.0 | MIT | 编译进 `sonora.exe`（登记二维码） | 保留版权声明 |
| ZCOOL KuaiLe 字体 | 1.0 | SIL OFL 1.1 | `assets/fonts` 里的子集（编译进 qrc） | 不改名分发；子集化属于允许的修改 |
| doctest | 2.5.3 | MIT | **不随包发布**（仅测试二进制） | 无 |
| Microsoft VC++ 运行库 | VS 2022 | Microsoft 可再发行组件条款 | `vcruntime140*.dll`、`msvcp140*.dll` 等 | 按 `--compiler-runtime` 原样分发，不修改 |

## 关于 Qt 的 LGPL v3

- Sonora 以**动态链接**方式使用 Qt：发布包里是独立的 `Qt6*.dll`，替换这些 DLL
  （同 ABI 的另一个 Qt 6 构建）即可使用不同的 Qt 版本，无需重新编译 Sonora。
- Qt 源码可从 <https://download.qt.io/official_releases/qt/> 获取。
- 本项目未修改 Qt 源码。
- 如需 LGPL §4(d) 所述的"可重链接"，替换 DLL 这一条在本项目成立。

## 关于 FFmpeg 的 LGPL v2.1+

- 本项目使用的 FFmpeg 由 vcpkg 的 `ffmpeg[core,avcodec,avformat,swresample]` 构建，
  **不含** `gpl` 与 `nonfree` 特性（没有 x264/x265 等 GPL 编码器）。
- FFmpeg 源码可从 <https://ffmpeg.org/download.html> 获取。
- 本项目未修改 FFmpeg 源码。

## 关于 TagLib（静态链接）

TagLib 是 LGPL v2.1+ 且可选用 MPL 2.0。本项目把它静态链接进 `sonora.exe`。
按 LGPL 的要求，这里提供获取其源码与构建方式的信息：

- 源码：<https://taglib.org/>（本项目使用 2.3.2）
- 构建：由 xmake 的 `taglib` 包自动下载并编译，`xmake.lua` 中可见版本与开关。

如果你需要把 TagLib 替换成自己构建的版本，请从源码重新构建 Sonora（`xmake -y`），
这一点在静态链接下无法避免，已在 `docs/DEPENDENCIES.md` 记录并接受。

## 文本许可证全文

- LGPL v3：<https://www.gnu.org/licenses/lgpl-3.0.html>
- LGPL v2.1：<https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>
- MPL 2.0：<https://www.mozilla.org/en-US/MPL/2.0/>
- MIT：<https://opensource.org/licenses/MIT>
- SIL OFL 1.1：<https://openfontlicense.org/>

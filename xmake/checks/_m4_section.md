### M4 · 换成 FFmpeg 引擎（4 人日，**技术风险最高**）✅ 已完成 · 实际 3.5 人日

**目标**：在端口契约完全不变的前提下替换实现。做对了，后面所有 DSP 才有地基。

**步骤**

- [x] `AudioRing`（SPSC 无锁环，容量 `48000*2*2`）· 0.5 天
- [x] **单测先行**：两个线程，生产者写 100 万样本，消费者逐个校验，不丢不重不越界 · 含在上一条
- [x] 开写前过一遍 [DEPENDENCIES §8](DEPENDENCIES.md) 的 9.x 差异表 · 0.1 天
- [x] `FfmpegDecoder`：open / decode loop / seek / flush · 0.8 天
- [x] `AudioOutDevice : QIODevice` + `QAudioSink` 拉模式 · 0.4 天
- [x] 位置由样本计数推导 · 含在上一条
- [x] `FfmpegOutput` 实现 `IAudioOutput`，在 Host 里替换掉 `QtMultimediaOutput`（**一行切换**）· 0.9 天
- [x] vcpkg 装 MSVC 版 FFmpeg · 0.2 天（编译本身 12 分钟，机器自己跑）

**验收（实测）**

| 项 | 结果 |
| --- | --- |
| 四种格式出声 | wav / mp3 / flac / m4a 全部解码成功，进度与总时长对得上 |
| 进度跟手 | 33ms 拉 `IPositionStream`，位置每秒走 1 秒，无跳变 |
| 拖/点进度条 | 点 70% → `1:27 / 2:02`（flac）；越界回绕已修，位置永不大于总时长 |
| 暂停 / 继续 | 暂停后位置冻结在 `0:06`，按钮变「播放」，再按从原处继续 |
| 播完自动下一首 | wav 队列连播到第 6 首 |
| 队列播完 | 输出停止、按钮回「播放」、进度归零 |
| 命令延迟 | `[warn] 命令同步耗时 224.97ms` 修复后消失（残下一次 6.56ms，来自 `stopWorker` 的 join） |
| 单测 | `mamba_tests` 68 用例 / 3353 断言全绿（含 `AudioRing` 百万样本压测） |
| 边界 | `boundaries.py` 126 文件通过 |
| 切歌 50 次 / 拔耳机 / 320kbps CPU 对比 | **没测**，见偏差第 5 条 |

**踩过的坑**

| 坑 | 症状 | 修法 |
| --- | --- | --- |
| vcpkg classic 不认 `--features` | `error: unexpected switch` | 写成 `ffmpeg[core,avcodec,avformat,swresample]:x64-windows` |
| debug 链 release 库 | 只在 `av_malloc` 里炸，堆损坏 | debug 构建链 `installed/x64-windows/debug/lib`（/MDd） |
| 没拷 FFmpeg DLL | 双击 exe 立刻退出、零输出 | `MambaMusic` 的 `after_build` 拷 `av*.dll` / `sw*.dll` |
| 在命令线程里 open | `命令同步耗时 224.97ms`，UI 卡 | `load()` 只登记 + 起线程；解码线程 open，GUI 线程建 sink |
| 位置单位混用 | 跳 50% 后显示 `45:28 / 2:02` | 样本与毫秒必须先合成再换算 |
| `av_seek_frame(BACKWARD)` 不精确 | 跳 80% 后位置走到 `2:24 / 2:02` | 先 `avformat_seek_file(INT64_MIN, ts, INT64_MAX)`，再按首帧 pts 丢掉多出的样本；`positionMs()` 再 clamp 到 duration |
| 队列播完不停输出 | 按钮永远显示「暂停」、位置卡在末尾 | `handleFinished()` 在 `advance()` 失败时显式 `output_.stop()` |

**与计划的偏差**

1. **没有代际号**：用 `stopWorker()` + `decoder.close()` + 把所有回调排队到 GUI 线程来保证安全，跳转用"丢样本"而不是换代际。
2. **没做输出设备热切换**：打开时按解码器格式请求 `Float`，设备不支持则降 `Int16`（`AudioOutDevice` 里做转换）。拔耳机/换设备的监听留到 M6 一起做。
3. **`AudioRing` 容量不是"≈2s"**：固定 `48000*2*2`，且向上取整到 2 的幂（192000 → 262144，约 1.36 秒立体声）。多出来的空间吸收"解大块 / 取小块"的抖动。
4. **`PlayerViewModel` 不是零改动**：为了 clamp 位置和删调试输出改了两处。原计划想用它证明"换实现不动上层"，实际证明的是"只动了 2 处显示逻辑，没动任何接口"。
5. **CPU 对比、50 次切歌没测**：这两项需要长时间 soak，放到 M6 一起做；`vcpkg` 与 FFmpeg 9.x 的组合已验证可用。

---

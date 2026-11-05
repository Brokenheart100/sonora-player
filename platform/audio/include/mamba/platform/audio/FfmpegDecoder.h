#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <mamba/kernel/Result.h>

namespace mamba::platform::audio {

class AudioRing;

// 用 FFmpeg 解的音频解码器。**只在解码线程上使用**——AVFormatContext /
// AVCodecContext / SwrContext 都不是线程安全的，跨线程调用是数据竞争的经典来源。
//
// 输出一律是**交错 float32**（AV_SAMPLE_FMT_FLT）：不管源是 mp3 的 float planar、
// flac 的 int32、还是 aac 的 16 位，都经 swresample 转成同一种格式。
// 统一在这里做，下游（环形缓冲、设备、DSP）就都不需要知道自己吃的是什么。
class FfmpegDecoder {
public:
    struct Format {
        int sampleRate = 48000;
        int channels = 2;
        std::int64_t durationMs = 0;
    };

    FfmpegDecoder();
    ~FfmpegDecoder();

    FfmpegDecoder(const FfmpegDecoder&) = delete;
    FfmpegDecoder& operator=(const FfmpegDecoder&) = delete;

    // 打开文件并探测格式。成功后可立刻读 format()。
    kernel::Result<Format> open(std::string_view utf8Path);
    void close() noexcept;

    const Format& format() const noexcept { return format_; }

    // 往 ring 里灌数据，直到 ring 满、或者这一轮解码暂时产不出更多采样。
    // 返回本次写入的样本数（不是帧数）。0 不代表结束——要看 atEnd()。
    kernel::Result<std::size_t> pump(AudioRing& ring);

    // 所有 packet 都喂完、解码器也排空、且没有待写出的采样。
    bool atEnd() const noexcept { return drained_ && pendingCount_ == 0; }

    // 跳到 ms。语义是"从这里开始解"，不是"精确落在这个采样上"——容器只能定位到
    // 帧/块边界，误差通常 < 50ms，播放器不需要更准。
    kernel::Result<std::int64_t> seekTo(std::int64_t ms);

    // 从 seek 点算起已经产出的采样数（不是帧），供输出端推导播放位置。
    std::int64_t samplesProduced() const noexcept { return samplesProduced_; }

private:
    kernel::Result<bool> decodeOneFrame();   // false = 暂时没有更多帧
    void convertFrame() noexcept;

    // 不透明指针：公开头里一个 FFmpeg 类型都不出现，否则下游为了 include 这个头
    // 就得拿到 libav* 的 include 路径（M3 的 QAudioOutput 坑就是这个形状）。
    struct Impl;
    Impl* impl_ = nullptr;

    Format format_{};
    std::vector<float> pending_;     // 已转好的交错采样
    std::size_t pendingCount_ = 0;   // 单位是样本，不是元素
    bool drained_ = false;
    std::int64_t samplesProduced_ = 0;

    // 跳转精度：容器 seek 只能落到关键帧附近（FLAC 尤其随意），
    // 所以 seek 之后要按"目标 - 实际帧时间"丢掉开头那段，位置才对得上。
    std::int64_t seekTargetMs_ = 0;
    bool seekPending_ = false;
    std::int64_t skipSamples_ = 0;
};

}  // namespace mamba::platform::audio

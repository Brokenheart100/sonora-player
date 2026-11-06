#pragma once

#include <QIODevice>

#include <cstddef>
#include <vector>

namespace sonora::platform::audio {

class AudioRing;

// QAudioSink 的**拉模式**数据源：Qt 在音频线程里调 readData()，我们从环形缓冲里
// 舀一批 float 采样填进去。用拉模式而不是把数据 push 给 sink，是因为位置与缓冲
// 深度都由我们自己掌握——推模式下"到底播到哪了"要靠 sink 的 bytesFree 反推。
//
// 格式约定：环形缓冲里永远是**交错 float32**（FFmpeg 侧统一经 swresample 转成
// AV_SAMPLE_FMT_FLT）。设备若是 float32，readData 直接映射，零转换；若是 int16
// （部分老设备），在这里转一次，转换成本是一帧两次乘加。
class AudioOutDevice final : public QIODevice {
public:
    enum class SampleFormat {
        Float32,   // 目标缓冲也是 float，直接把 ring 的字节搬过去
        Int16,     // 目标缓冲是 int16，读出来之后转换
    };

    AudioOutDevice(AudioRing& ring, int channels, SampleFormat format);

    AudioOutDevice(const AudioOutDevice&) = delete;
    AudioOutDevice& operator=(const AudioOutDevice&) = delete;

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char* data, qint64 maxSize) override;

private:
    // 读不满时补静音而不是返回 0：返回 0 会让 QAudioSink 提前结束，
    // 补零只是这一小段安静，解码追上来就自动接上。
    AudioRing& ring_;
    int channels_ = 2;
    SampleFormat format_ = SampleFormat::Float32;

    // 音频线程里不许分配内存，所以换格式用的暂存区在这里一次性备好。
    std::vector<float> scratch_;
};

}  // namespace sonora::platform::audio

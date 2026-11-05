#include "mamba/platform/audio/AudioOutDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "mamba/platform/audio/AudioRing.h"

namespace mamba::platform::audio {
namespace {

// Qt 一次最多问这么多字节（默认 16KB 的若干倍），暂存区按这个上限备着。
constexpr qint64 kMaxReadBytes = 64 * 1024;

}  // namespace

AudioOutDevice::AudioOutDevice(AudioRing& ring, int channels, SampleFormat format)
    : ring_(ring),
      channels_(channels > 0 ? channels : 2),
      format_(format)
{
    if (format_ == SampleFormat::Int16) {
        scratch_.resize(static_cast<std::size_t>(kMaxReadBytes) / sizeof(float));
    }
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

qint64 AudioOutDevice::bytesAvailable() const
{
    const std::size_t samples = ring_.availableToRead();
    const qint64 bytesPerSample = (format_ == SampleFormat::Float32) ? 4 : 2;
    return static_cast<qint64>(samples) * bytesPerSample + QIODevice::bytesAvailable();
}

qint64 AudioOutDevice::readData(char* data, qint64 maxSize)
{
    if (data == nullptr || maxSize <= 0) {
        return 0;
    }

    if (format_ == SampleFormat::Float32) {
        // 零转换：ring 里就是 float32，且是交错排列，跟设备的期望完全一致。
        const qint64 maxSamples = maxSize / static_cast<qint64>(sizeof(float));
        const std::size_t got = ring_.read(reinterpret_cast<float*>(data),
                                           static_cast<std::size_t>(maxSamples));
        const qint64 filledBytes = static_cast<qint64>(got) * static_cast<qint64>(sizeof(float));
        if (filledBytes < maxSize) {
            std::memset(data + filledBytes, 0, static_cast<std::size_t>(maxSize - filledBytes));
        }
        return maxSize;   // 永远是满的：不够的部分是静音
    }

    const qint64 maxSamples = maxSize / static_cast<qint64>(sizeof(std::int16_t));
    const auto wanted = static_cast<std::size_t>(maxSamples);
    const std::size_t target = std::min(wanted, scratch_.size());
    const std::size_t got = ring_.read(scratch_.data(), target);

    auto* out = reinterpret_cast<std::int16_t*>(data);
    for (std::size_t i = 0; i < got; ++i) {
        // clamp 之后再乘：超过 1.0 的样本在解码侧是允许出现的（重采样振铃），
        // 直接收缩会溢出成反相噪音。
        const float clamped = std::clamp(scratch_[i], -1.0F, 1.0F);
        out[i] = static_cast<std::int16_t>(clamped * 32767.0F);
    }
    const std::size_t filledSamples = got;
    for (std::size_t i = filledSamples; i < wanted; ++i) {
        out[i] = 0;
    }
    return maxSize;
}

qint64 AudioOutDevice::writeData(const char*, qint64)
{
    return -1;   // 只读数据源
}

}  // namespace mamba::platform::audio

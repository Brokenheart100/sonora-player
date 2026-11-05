#include "mamba/platform/audio/AudioRing.h"

#include <algorithm>
#include <cstring>

namespace mamba::platform::audio {
namespace {

// 向上取整到 2 的幂；至少 2，否则掩码退化成 1 个样本的缓冲。
std::size_t roundUpToPowerOfTwo(std::size_t value) noexcept
{
    std::size_t result = 2;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

}  // namespace

AudioRing::AudioRing(std::size_t capacitySamples)
    : capacity_(roundUpToPowerOfTwo(capacitySamples)),
      mask_(capacity_ - 1)
{
    buffer_.resize(capacity_);
}

std::size_t AudioRing::availableToRead() const noexcept
{
    // acquire 保证：读到 writePos_ 的新值时，生产者已经写完那批采样。
    const std::uint64_t write = writePos_.load(std::memory_order_acquire);
    const std::uint64_t read = readPos_.load(std::memory_order_relaxed);
    return static_cast<std::size_t>(write - read);
}

std::size_t AudioRing::availableToWrite() const noexcept
{
    const std::uint64_t write = writePos_.load(std::memory_order_relaxed);
    // acquire 保证：读到 readPos_ 的新值时，消费者确实已经读走了那批采样，
    // 我们不会覆盖它正在读的内存。
    const std::uint64_t read = readPos_.load(std::memory_order_acquire);
    return capacity_ - static_cast<std::size_t>(write - read);
}

std::size_t AudioRing::write(const float* source, std::size_t count) noexcept
{
    if (source == nullptr || count == 0) {
        return 0;
    }

    const std::uint64_t write = writePos_.load(std::memory_order_relaxed);
    const std::uint64_t read = readPos_.load(std::memory_order_acquire);
    const std::size_t used = static_cast<std::size_t>(write - read);
    const std::size_t space = capacity_ - used;
    const std::size_t n = std::min(count, space);
    if (n == 0) {
        return 0;
    }

    // 从 write 位置起写 n 个，可能跨过缓冲末端，所以分两段。
    const std::size_t first = std::min(n, capacity_ - (write & mask_));
    std::memcpy(buffer_.data() + (write & mask_), source, first * sizeof(float));
    if (n > first) {
        std::memcpy(buffer_.data(), source + first, (n - first) * sizeof(float));
    }

    // release 保证：上面两段拷贝对消费者可见之后，writePos_ 才更新。
    writePos_.store(write + n, std::memory_order_release);
    return n;
}

std::size_t AudioRing::read(float* destination, std::size_t count) noexcept
{
    if (destination == nullptr || count == 0) {
        return 0;
    }

    const std::uint64_t read = readPos_.load(std::memory_order_relaxed);
    const std::uint64_t write = writePos_.load(std::memory_order_acquire);
    const std::size_t available = static_cast<std::size_t>(write - read);
    const std::size_t n = std::min(count, available);
    if (n == 0) {
        return 0;
    }

    const std::size_t first = std::min(n, capacity_ - (read & mask_));
    std::memcpy(destination, buffer_.data() + (read & mask_), first * sizeof(float));
    if (n > first) {
        std::memcpy(destination + first, buffer_.data(), (n - first) * sizeof(float));
    }

    readPos_.store(read + n, std::memory_order_release);
    return n;
}

void AudioRing::clear() noexcept
{
    // 把读游标推到写游标：消费者视角下缓冲立刻变空。
    // 生产者不需要知道——它看到的空间自然变大，下一批采样覆盖旧区域。
    const std::uint64_t write = writePos_.load(std::memory_order_acquire);
    readPos_.store(write, std::memory_order_release);
}

}  // namespace mamba::platform::audio

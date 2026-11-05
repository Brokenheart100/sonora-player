#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mamba::platform::audio {

// 单生产者 / 单消费者的无锁环形缓冲（SPSC），元素是 float 采样。
//
// 为什么需要它：解码在**解码线程**，出声在**音频线程**（QAudioSink 回调里），
// 两个线程之间只允许"搬数据"这一种交互。锁会让音频线程碰到优先级反转
// （它一被挂起就是爆音），所以这里只有两个原子游标和两段 memcpy。
//
// 使用纪律（违反不会报错，只会偶尔放出杂音）：
//   - write() 只允许**一个**线程调（解码线程）
//   - read() 只允许**一个**线程调（音频线程）
//   - 容量按"样本"算，不是按帧：立体声时一帧 = 2 个样本，2 秒 48kHz 立体声
//     是 192000 个样本。解码线程据此决定一次解多少、卡不卡。
class AudioRing {
public:
    // capacitySamples 向上取整到 2 的幂（用掩码代替取模，热路径里不做除法）。
    explicit AudioRing(std::size_t capacitySamples);

    AudioRing(const AudioRing&) = delete;
    AudioRing& operator=(const AudioRing&) = delete;

    std::size_t capacity() const noexcept { return capacity_; }

    // 可以从缓冲里读出的样本数（消费者视角）
    std::size_t availableToRead() const noexcept;

    // 还能写入的样本数（生产者视角）
    std::size_t availableToWrite() const noexcept;

    // 写入至多 count 个样本，返回实际写入数。缓冲满时返回值小于 count，
    // **不分配、不阻塞、不覆盖未读数据**——丢数据的决定权留给调用者。
    std::size_t write(const float* source, std::size_t count) noexcept;

    // 读取至多 count 个样本，返回实际读出数。缓冲空时返回 0。
    std::size_t read(float* destination, std::size_t count) noexcept;

    // 丢弃所有未读数据。**只在消费者侧或"两边都已停下"的时机调**，
    // 用来实现 seek / 切歌（旧歌的残余采样必须消失，否则会听到前一秒）。
    void clear() noexcept;

    bool empty() const noexcept { return availableToRead() == 0; }

private:
    std::vector<float> buffer_;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;

    // 单调递增的游标，永不回绕（uint64 在 48kHz 下要跑 1200 万年）。
    // 回绕靠掩码与缓冲长度对齐，所以不需要处理整数溢出。
    std::atomic<std::uint64_t> writePos_{0};
    std::atomic<std::uint64_t> readPos_{0};
};

}  // namespace mamba::platform::audio

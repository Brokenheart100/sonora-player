#include "doctest_support.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include <mamba/platform/audio/AudioRing.h>

using mamba::platform::audio::AudioRing;

namespace {

std::vector<float> numbers(std::size_t from, std::size_t count)
{
    std::vector<float> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<float>(from + i);
    }
    return out;
}

}  // namespace

TEST_CASE("AudioRing：容量向上取整到 2 的幂")
{
    CHECK(AudioRing(1000).capacity() == 1024);
    CHECK(AudioRing(1024).capacity() == 1024);
    CHECK(AudioRing(1).capacity() == 2);   // 掩码至少要能区分两个位置
    CHECK(AudioRing(0).capacity() == 2);

    // 2 秒 48kHz 立体声要的是 192000 个样本，但 192000 不是 2 的幂，
    // 实际会拿到 262144（约 2.7 秒）。这不是浪费：多出来的空间让
    // "解码器一次解大块"和"设备一次取小块"之间的抖动有地方可放。
    const AudioRing twoSeconds(48000 * 2 * 2);
    CHECK(twoSeconds.capacity() == 262144);
    CHECK(twoSeconds.capacity() >= 192000);
    CHECK((twoSeconds.capacity() & (twoSeconds.capacity() - 1)) == 0);   // 确实是 2 的幂
}

TEST_CASE("AudioRing：写进去的能原样读出来")
{
    AudioRing ring(16);
    const auto source = numbers(1, 5);

    REQUIRE(ring.write(source.data(), source.size()) == 5);
    CHECK(ring.availableToRead() == 5);
    CHECK(ring.availableToWrite() == ring.capacity() - 5);

    std::vector<float> out(5);
    REQUIRE(ring.read(out.data(), out.size()) == 5);
    CHECK(out == source);
    CHECK(ring.empty());
    CHECK(ring.availableToWrite() == ring.capacity());
}

TEST_CASE("AudioRing：写满时只接受装得下的部分，不覆盖未读数据")
{
    AudioRing ring(8);
    const auto over = numbers(0, 20);

    const std::size_t written = ring.write(over.data(), over.size());
    CHECK(written == 8);
    CHECK(ring.availableToWrite() == 0);
    CHECK(ring.write(over.data(), over.size()) == 0);   // 满了就是 0，不阻塞

    std::vector<float> out(8);
    REQUIRE(ring.read(out.data(), out.size()) == 8);
    CHECK(out == numbers(0, 8));   // 前 8 个而不是后 8 个：没被覆盖
}

TEST_CASE("AudioRing：空的时候读出 0，不越界")
{
    AudioRing ring(8);
    std::vector<float> out(4, -1.0F);

    CHECK(ring.read(out.data(), out.size()) == 0);
    CHECK(out == std::vector<float>(4, -1.0F));   // 一个字节都不许动
    CHECK(ring.read(out.data(), 0) == 0);
    CHECK(ring.write(out.data(), 0) == 0);
    CHECK(ring.write(nullptr, 4) == 0);
    CHECK(ring.read(nullptr, 4) == 0);
}

TEST_CASE("AudioRing：反复回绕，跨越缓冲末端仍然按顺序")
{
    AudioRing ring(8);   // 故意很小，让每次读写都跨尾端
    const auto source = numbers(0, 3);

    for (std::size_t round = 0; round < 1000; ++round) {
        const auto chunk = numbers(round * 3, 3);
        REQUIRE(ring.write(chunk.data(), chunk.size()) == 3);

        std::vector<float> out(3);
        REQUIRE(ring.read(out.data(), out.size()) == 3);
        REQUIRE(out == chunk);
    }
    CHECK(ring.empty());
}

TEST_CASE("AudioRing：clear 丢掉未读数据，腾出整条缓冲")
{
    AudioRing ring(8);
    const auto source = numbers(1, 6);

    REQUIRE(ring.write(source.data(), source.size()) == 6);
    REQUIRE(ring.availableToRead() == 6);

    ring.clear();
    CHECK(ring.empty());
    CHECK(ring.availableToWrite() == ring.capacity());

    // 清空后写入的新数据从缓冲开头开始，读出来必须还是新的那批
    const auto fresh = numbers(100, 4);
    REQUIRE(ring.write(fresh.data(), fresh.size()) == 4);
    std::vector<float> out(4);
    REQUIRE(ring.read(out.data(), out.size()) == 4);
    CHECK(out == fresh);
}

TEST_CASE("AudioRing：两个线程跑 100 万样本，不丢不重不乱序")
{
    constexpr std::size_t kTotal = 1'000'000;
    constexpr std::size_t kCapacity = 4096;   // 远小于总量，逼它回绕几百次

    AudioRing ring(kCapacity);

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};
    std::atomic<std::size_t> mismatches{0};
    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        // 97 与 4096 互质：任何"按块对齐"或"丢一个块"的错都会立刻错位
        std::vector<float> chunk(97);
        std::size_t next = 0;
        while (next < kTotal) {
            const std::size_t n = std::min(chunk.size(), kTotal - next);
            for (std::size_t k = 0; k < n; ++k) {
                chunk[k] = static_cast<float>(next + k);   // 1e6 < 2^24，float 精确
            }
            std::size_t offset = 0;
            while (offset < n) {
                const std::size_t w = ring.write(chunk.data() + offset, n - offset);
                offset += w;
                if (w == 0) {
                    std::this_thread::yield();   // 缓冲满，让消费者跑
                }
            }
            next += n;
            produced.store(next, std::memory_order_relaxed);
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::vector<float> chunk(64);   // 与生产者块长不同，永远拿到不整齐的部分
        std::size_t expected = 0;
        for (;;) {
            const std::size_t n = ring.read(chunk.data(), chunk.size());
            for (std::size_t k = 0; k < n; ++k) {
                if (chunk[k] != static_cast<float>(expected)) {
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                }
                ++expected;
            }
            if (n == 0) {
                if (producerDone.load(std::memory_order_acquire) && ring.empty()) {
                    break;
                }
                std::this_thread::yield();
            }
        }
        consumed.store(expected, std::memory_order_relaxed);
    });

    producer.join();
    consumer.join();

    CHECK(mismatches.load() == 0);
    CHECK(consumed.load() == kTotal);
    CHECK(ring.empty());
    CHECK(ring.availableToWrite() == ring.capacity());
}

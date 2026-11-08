// 队列与播放模式的测试。
//
// 这里守的是 M6 的硬指标："随机模式连续 200 首无重复；上一首能精确回溯"。
// 两条都不是"看起来对"，而是结构保证——预生成排列 + 游标，所以可以断言。
#include "doctest_support.h"

#include <cstdint>
#include <string>
#include <vector>

#include "fake_audio_output.h"

#include "sonora/contracts/PlaybackEvents.h"
#include "sonora/kernel/Error.h"
#include "sonora/kernel/EventBus.h"
#include "sonora/playback/contracts/Commands.h"
#include "sonora/playback/contracts/PlaybackMode.h"
#include "sonora/playback/core/app/PlaybackService.h"
#include "sonora/playback/core/domain/PlayQueue.h"
#include "sonora/playback/core/domain/ShufflePlanner.h"

namespace {

using sonora::playback::contracts::PlaybackMode;
using sonora::playback::contracts::PlayTracks;
using sonora::playback::contracts::QueueEntry;
using sonora::playback::core::AdvanceReason;
using sonora::playback::core::PlayQueue;
using sonora::playback::core::ShufflePlanner;
using sonoratest::FakeAudioOutput;

QueueEntry entry(std::uint64_t id, std::string title) {
    QueueEntry e;
    e.trackId = id;
    e.filePath = "C:/m/" + std::to_string(id) + ".flac";
    e.title = std::move(title);
    return e;
}

std::vector<QueueEntry> makeTracks(std::int32_t count) {
    std::vector<QueueEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        entries.push_back(entry(static_cast<std::uint64_t>(i + 1), "曲目 " + std::to_string(i + 1)));
    }
    return entries;
}

} // namespace

TEST_CASE("顺序模式：到头就停，退到开头就停") {
    PlayQueue queue;
    queue.setEntries(makeTracks(3), 0);

    CHECK(queue.index() == 0);
    CHECK(queue.advance());
    CHECK(queue.index() == 1);
    CHECK(queue.advance());
    CHECK(queue.index() == 2);
    CHECK_FALSE(queue.advance()); // 顺序模式不循环
    CHECK(queue.index() == 2);

    CHECK(queue.retreat());
    CHECK(queue.index() == 1);
    CHECK(queue.retreat());
    CHECK(queue.index() == 0);
    CHECK_FALSE(queue.retreat());
}

TEST_CASE("列表循环：两端都能绕回去") {
    PlayQueue queue;
    queue.setEntries(makeTracks(3), 0);
    queue.setMode(PlaybackMode::RepeatAll);

    CHECK(queue.advance());
    CHECK(queue.advance());
    CHECK(queue.index() == 2);
    CHECK(queue.advance());
    CHECK(queue.index() == 0); // 绕回第一首

    CHECK(queue.retreat());
    CHECK(queue.index() == 2); // 从第一首往回绕到最后一首
}

TEST_CASE("单曲循环：播完重放本曲，手动下一首仍然前进") {
    PlayQueue queue;
    queue.setEntries(makeTracks(3), 0);
    queue.setMode(PlaybackMode::RepeatOne);

    CHECK(queue.advance(AdvanceReason::AutoFinished));
    CHECK(queue.index() == 0); // 游标不动 = 重放

    CHECK(queue.advance(AdvanceReason::Manual));
    CHECK(queue.index() == 1);
}

TEST_CASE("随机模式：排列是完整置换，且第一首就是用户点的那首") {
    const auto order = ShufflePlanner::plan(200, 37, 12345);

    REQUIRE(order.size() == 200);
    CHECK(order.front() == 37);

    std::vector<bool> seen(200, false);
    for (const auto index : order) {
        REQUIRE(index >= 0);
        REQUIRE(index < 200);
        CHECK_FALSE(seen[static_cast<std::size_t>(index)]);
        seen[static_cast<std::size_t>(index)] = true;
    }
}

TEST_CASE("随机模式 + 列表循环：连放 200 首不重复，上一首精确回溯") {
    PlayQueue queue;
    queue.setEntries(makeTracks(200), 0);
    queue.setMode(PlaybackMode::Shuffle, 987654321);
    queue.setMode(PlaybackMode::RepeatAll); // 借用它的"到底绕回"，再切回随机
    queue.setMode(PlaybackMode::Shuffle, 987654321);

    CHECK(queue.mode() == PlaybackMode::Shuffle);

    std::vector<std::uint64_t> played;
    played.push_back(queue.current()->trackId);
    for (int i = 1; i < 200; ++i) {
        REQUIRE(queue.advance());
        played.push_back(queue.current()->trackId);
    }

    // 200 首全部不重复
    std::vector<std::uint64_t> sorted = played;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());

    // 上一首精确回溯：连退 199 次，顺序正好是播放顺序的逆序
    for (int i = 198; i >= 0; --i) {
        REQUIRE(queue.retreat());
        CHECK(queue.current()->trackId == played[static_cast<std::size_t>(i)]);
    }
    CHECK_FALSE(queue.retreat());
}

TEST_CASE("换模式不换正在放的这首") {
    PlayQueue queue;
    queue.setEntries(makeTracks(5), 2);
    const auto before = queue.current()->trackId;

    queue.setMode(PlaybackMode::Shuffle, 42);
    CHECK(queue.current()->trackId == before);
    CHECK(queue.order().front() == 2);

    queue.setMode(PlaybackMode::RepeatAll);
    CHECK(queue.current()->trackId == before);
    CHECK(queue.index() == 2);
}

TEST_CASE("队列面板点某一项：随机模式下也能跳过去") {
    PlayQueue queue;
    queue.setEntries(makeTracks(10), 0);
    queue.setMode(PlaybackMode::Shuffle, 2024);

    CHECK(queue.jumpTo(7));
    CHECK(queue.index() == 7);

    CHECK_FALSE(queue.jumpTo(99));
    CHECK(queue.index() == 7);
}

TEST_CASE("菜单顺序：顺序 → 循环 → 单曲 → 随机 → 顺序") {
    using sonora::playback::contracts::nextMode;
    CHECK(nextMode(PlaybackMode::Sequential) == PlaybackMode::RepeatAll);
    CHECK(nextMode(PlaybackMode::RepeatAll) == PlaybackMode::RepeatOne);
    CHECK(nextMode(PlaybackMode::RepeatOne) == PlaybackMode::Shuffle);
    CHECK(nextMode(PlaybackMode::Shuffle) == PlaybackMode::Sequential);
}

TEST_CASE("服务：切模式会写进快照，队列视图按播放顺序给") {
    FakeAudioOutput output;
    sonora::playback::core::PlaybackService service{output};

    PlayTracks command;
    command.entries = makeTracks(4);
    REQUIRE(service.playTracks(command).hasValue());

    REQUIRE(service.setMode(PlaybackMode::Shuffle).hasValue());
    CHECK(service.snapshot().mode == PlaybackMode::Shuffle);

    const auto view = service.queueView();
    REQUIRE(view.items.size() == 4);
    CHECK(view.mode == PlaybackMode::Shuffle);
    CHECK(view.currentIndex == 0); // 一开随机，当前这首就在最前
    CHECK(view.items.front().current);
    CHECK(view.items.front().trackId == 1);
}

TEST_CASE("服务：单曲循环下播完重放同一首，不换曲") {
    FakeAudioOutput output;
    sonora::playback::core::PlaybackService service{output};

    PlayTracks command;
    command.entries = makeTracks(3);
    REQUIRE(service.playTracks(command).hasValue());
    REQUIRE(service.setMode(PlaybackMode::RepeatOne).hasValue());

    output.finishCurrent();
    CHECK(service.snapshot().trackId == 1);
    CHECK(output.loadedPath == "C:/m/1.flac");
}

TEST_CASE("服务：跳到队列里的某一首") {
    FakeAudioOutput output;
    sonora::playback::core::PlaybackService service{output};

    PlayTracks command;
    command.entries = makeTracks(3);
    REQUIRE(service.playTracks(command).hasValue());

    REQUIRE(service.jumpTo(3).hasValue());
    CHECK(service.snapshot().trackId == 3);
    CHECK(output.loadedPath == "C:/m/3.flac");

    CHECK_FALSE(service.jumpTo(99).hasValue());
}

TEST_CASE("服务：播过 3 秒后上一首先回开头") {
    FakeAudioOutput output;
    sonora::playback::core::PlaybackService service{output, nullptr, &output};

    PlayTracks command;
    command.entries = makeTracks(3);
    command.startIndex = 1;
    REQUIRE(service.playTracks(command).hasValue());

    output.posMs = 5000; // 播过了 3 秒
    REQUIRE(service.previous().hasValue());
    CHECK(service.snapshot().trackId == 2); // 还在第二首
    CHECK(output.posMs == 0);

    output.posMs = 1000; // 刚开头，再按才换上一首
    REQUIRE(service.previous().hasValue());
    CHECK(service.snapshot().trackId == 1);
}

TEST_CASE("追加式队列：append 接在队尾，播到末尾不回绕而是等新歌") {
    PlayQueue queue;
    queue.setAppendable(true);
    queue.setEntries(makeTracks(2), 0);

    CHECK(queue.advance());
    CHECK_FALSE(queue.advance()); // 到头就停，不因为"循环"绕回第一首
    CHECK(queue.atEnd());

    const auto added = queue.append({entry(9, "FM 1"), entry(10, "FM 2")});
    CHECK(added == 2);
    CHECK_FALSE(queue.atEnd());
    CHECK(queue.advance());
    REQUIRE(queue.current() != nullptr);
    CHECK(queue.current()->trackId == 9);
    CHECK(queue.advance());
    CHECK(queue.current()->trackId == 10);
}

TEST_CASE("追加式队列：随机模式下不洗牌（下一批本来就是随机的）") {
    PlayQueue queue;
    queue.setAppendable(true);
    queue.setMode(PlaybackMode::Shuffle);
    queue.setEntries(makeTracks(4), 0);

    // 顺序保持原样：追加式来源不接管"随机"，否则队列顺序和用户看到的对不上。
    for (std::uint64_t i = 0; i < 4; ++i) {
        REQUIRE(queue.current() != nullptr);
        CHECK(queue.current()->trackId == i + 1);
        if (i != 3) {
            CHECK(queue.advance());
        }
    }
}

TEST_CASE("服务：追加式队列播到末尾会喊饿，而不是停在那里") {
    FakeAudioOutput output;
    sonora::kernel::EventBus events;
    int canned = 0;
    events.subscribe<sonora::contracts::QueueNeedsMore>(
        [&canned](const sonora::contracts::QueueNeedsMore &) { ++canned; });

    sonora::playback::core::PlaybackService service{output, nullptr, nullptr, &events};

    PlayTracks command;
    command.entries = makeTracks(2);
    command.appendable = true;
    REQUIRE(service.playTracks(command).hasValue());

    output.finishCurrent();
    CHECK(service.snapshot().trackId == 2); // 还有第二首，正常前进
    CHECK(canned == 0);

    output.finishCurrent();
    CHECK(canned == 1); // 播到末尾：发布 QueueNeedsMore，交给组合根去要下一批
    CHECK(service.snapshot().state == sonora::playback::contracts::PlaybackState::Stopped);

    // 组合根把新一批接上，播放立刻继续
    REQUIRE(service.appendTracks({ {entry(9, "FM 1")}, true }).hasValue());
    CHECK(service.snapshot().trackId == 9);
    CHECK(service.snapshot().state == sonora::playback::contracts::PlaybackState::Playing);
}

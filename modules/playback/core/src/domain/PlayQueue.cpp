#include <sonora/playback/core/domain/PlayQueue.h>

#include <algorithm>
#include <numeric>

#include <sonora/playback/core/domain/ShufflePlanner.h>

namespace sonora::playback::core {

void PlayQueue::setEntries(std::vector<contracts::QueueEntry> entries, std::int32_t startIndex) {
    entries_ = std::move(entries);
    if (entries_.empty()) {
        order_.clear();
        cursor_ = -1;
        return;
    }
    const auto last = size() - 1;
    const auto start = std::clamp(startIndex, 0, last);
    rebuildOrder(start, 0);
}

void PlayQueue::clear() {
    entries_.clear();
    order_.clear();
    cursor_ = -1;
    seed_ = 0;
}

void PlayQueue::setMode(contracts::PlaybackMode mode, std::uint64_t seed) {
    mode_ = mode;
    if (entries_.empty()) {
        return;
    }
    // 保持"现在放的这首"不变：换模式不该把正在播的歌换成另一首。
    const auto keep = std::max<std::int32_t>(index(), 0);
    rebuildOrder(keep, seed);
}

void PlayQueue::rebuildOrder(std::int32_t keepEntry, std::uint64_t seed) {
    if (mode_ == contracts::PlaybackMode::Shuffle) {
        seed_ = seed != 0 ? seed : ShufflePlanner::freshSeed();
        order_ = ShufflePlanner::plan(size(), keepEntry, seed_);
        // 排列的第一项就是 keepEntry，所以游标从 0 开始。
        cursor_ = 0;
        return;
    }

    seed_ = 0;
    order_.resize(entries_.size());
    std::iota(order_.begin(), order_.end(), 0);
    cursor_ = std::clamp(keepEntry, 0, size() - 1);
}

const contracts::QueueEntry* PlayQueue::current() const noexcept {
    if (cursor_ < 0 || cursor_ >= static_cast<std::int32_t>(order_.size())) {
        return nullptr;
    }
    const auto entryIndex = order_[static_cast<std::size_t>(cursor_)];
    if (entryIndex < 0 || entryIndex >= size()) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(entryIndex)];
}

std::int32_t PlayQueue::index() const noexcept {
    if (cursor_ < 0 || cursor_ >= static_cast<std::int32_t>(order_.size())) {
        return -1;
    }
    return order_[static_cast<std::size_t>(cursor_)];
}

bool PlayQueue::advance(AdvanceReason reason) {
    if (empty() || order_.empty()) {
        return false;
    }
    if (reason == AdvanceReason::AutoFinished && mode_ == contracts::PlaybackMode::RepeatOne) {
        // 游标不动 = 重放本曲。调用方拿到 true 会重新装载当前曲目。
        return true;
    }
    if (cursor_ + 1 < static_cast<std::int32_t>(order_.size())) {
        ++cursor_;
        return true;
    }
    if (mode_ == contracts::PlaybackMode::RepeatAll) {
        cursor_ = 0;
        return true;
    }
    return false;
}

bool PlayQueue::retreat() {
    if (empty() || order_.empty()) {
        return false;
    }
    if (cursor_ > 0) {
        --cursor_;
        return true;
    }
    if (mode_ == contracts::PlaybackMode::RepeatAll) {
        cursor_ = static_cast<std::int32_t>(order_.size()) - 1;
        return true;
    }
    return false;
}

bool PlayQueue::jumpTo(std::int32_t entryIndex) {
    if (entryIndex < 0 || entryIndex >= size()) {
        return false;
    }
    const auto found = std::find(order_.begin(), order_.end(), entryIndex);
    if (found == order_.end()) {
        return false;
    }
    cursor_ = static_cast<std::int32_t>(std::distance(order_.begin(), found));
    return true;
}

} // namespace sonora::playback::core

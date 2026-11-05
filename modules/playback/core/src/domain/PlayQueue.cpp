#include <mamba/playback/core/domain/PlayQueue.h>

#include <algorithm>

namespace mamba::playback::core {

void PlayQueue::setEntries(std::vector<contracts::QueueEntry> entries, std::int32_t startIndex) {
    entries_ = std::move(entries);
    if (entries_.empty()) {
        index_ = -1;
        return;
    }
    const auto last = static_cast<std::int32_t>(entries_.size()) - 1;
    index_ = std::clamp(startIndex, 0, last);
}

void PlayQueue::clear() {
    entries_.clear();
    index_ = -1;
}

const contracts::QueueEntry* PlayQueue::current() const noexcept {
    if (index_ < 0 || index_ >= size()) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(index_)];
}

bool PlayQueue::advance() noexcept {
    if (empty() || index_ + 1 >= size()) {
        return false;
    }
    ++index_;
    return true;
}

bool PlayQueue::retreat() noexcept {
    if (empty() || index_ <= 0) {
        return false;
    }
    --index_;
    return true;
}

}  // namespace mamba::playback::core

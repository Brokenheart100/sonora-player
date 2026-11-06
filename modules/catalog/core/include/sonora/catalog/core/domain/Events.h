// Catalog 的领域事件。
//
// 名字用「过去式」是硬要求：事件描述的是**已经发生**的事，不是命令。
// "TrackAdded" 可以被订阅者忽略；"AddTrack" 则会诱导订阅者去执行它。
//
// 注意事件里只放 id，不放整个聚合：事件可能被延迟投递、可能被持久化，
// 而聚合在那一刻已经变了。订阅者需要细节时自己查——反正读侧有投影表。
#pragma once

#include <string_view>

#include "sonora/catalog/core/domain/CatalogIds.h"
#include "sonora/kernel/IDomainEvent.h"

namespace sonora::catalog::core {

class TrackAdded final : public kernel::IDomainEvent {
public:
    explicit TrackAdded(TrackId id) noexcept : id_(id) {}

    [[nodiscard]] std::string_view eventName() const noexcept override
    {
        return "catalog.track_added";
    }

    [[nodiscard]] TrackId id() const noexcept { return id_; }

private:
    TrackId id_;
};

}  // namespace sonora::catalog::core

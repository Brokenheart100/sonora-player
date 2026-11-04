// 强类型 id。
//
// 存在的唯一理由：让 TrackId 和 AlbumId 在**编译期**就不能互换。
// 用裸 uint64_t 传参的代码，两个 id 调换位置编译器一声不吭，运行时才在数据库里
// 写错行——这类 bug 要等到数据脏了才发现。TypedId 用一根 Tag 类型把两者焊死。
//
// 典型用法：
//     struct TrackIdTag;
//     using TrackId = TypedId<TrackIdTag>;      // 在领域包里这样定义一次
//     TrackId::fromValue(42)                     // 构造
//     id.value()                                 // 取出底盘数据（只有边界处用）
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace mamba::kernel {

template <class Tag, class Rep = std::uint64_t>
class TypedId {
public:
    using tag_type = Tag;
    using rep_type = Rep;

    constexpr TypedId() noexcept = default;

    [[nodiscard]] static constexpr TypedId fromValue(Rep value) noexcept { return TypedId{value}; }

    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }

    // 0 是「还没有 id」，不是合法主键。数据库里主键从 1 开始，所以这个判断可信。
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != Rep{}; }

    friend constexpr bool operator==(TypedId, TypedId) noexcept = default;
    friend constexpr auto operator<=>(TypedId, TypedId) noexcept = default;

private:
    explicit constexpr TypedId(Rep value) noexcept : value_(value) {}

    Rep value_{};
};

}  // namespace mamba::kernel

// 让 TypedId 能进 unordered_map / unordered_set。
// 特化写在全局作用域是标准做法，不是偷懒。
template <class Tag, class Rep>
struct std::hash<mamba::kernel::TypedId<Tag, Rep>> {
    std::size_t operator()(const mamba::kernel::TypedId<Tag, Rep> id) const noexcept
    {
        return std::hash<Rep>{}(id.value());
    }
};

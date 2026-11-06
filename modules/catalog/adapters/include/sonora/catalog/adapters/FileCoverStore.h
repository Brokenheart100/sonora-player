// 封面缓存的文件系统实现。
//
// 两张设计约束决定了它长这样：
//   1. **按内容寻址**。键是封面字节的哈希，不是 track id。同一张专辑封面在
//      一万首歌里出现一万次，存一万份就是几百 MB；内容寻址天然把它们收成一份，
//      而且"删掉一首歌要不要删封面"这个问题自动消失（没有引用计数，只有惰性文件）。
//   2. **键里带扩展名**。读侧拿到 cover_key 直接拼路径就能交给 QML 的 Image，
//      不需要再打开文件嗅探格式——列表滚动时每次嗅探都是一次多余的 IO。
#pragma once

#include <string>
#include <string_view>

#include "sonora/catalog/core/ports/Ports.h"

namespace sonora::catalog::adapters
{

    class FileCoverStore final : public core::ICoverStore
    {
    public:
        // 目录不存在时在第一次 store() 里创建。构造不碰磁盘：
        // "这台机器上没有封面目录"不该阻止程序启动。
        explicit FileCoverStore(std::string rootDirectory);

        [[nodiscard]] kernel::Result<std::string> store(
            std::span<const std::uint8_t> bytes) override;

        // 键 → 绝对路径。键为空返回空串（界面据此显示占位图）。
        [[nodiscard]] std::string pathFor(std::string_view key) const;

        [[nodiscard]] const std::string &root() const noexcept { return root_; }

    private:
        std::string root_;
    };

} // namespace sonora::catalog::adapters

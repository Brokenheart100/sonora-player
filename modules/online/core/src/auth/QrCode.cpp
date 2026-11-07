#include "sonora/online/core/auth/QrCode.h"

#include <cstddef>
#include <optional>

#include "qrcodegen.hpp"

namespace sonora::online::core::auth
{

    std::vector<std::string> encodeQrRows(std::string_view text)
    {
        std::vector<std::string> rows;
        if (text.empty() || text.size() > 512)
        {
            return rows;
        }

        // ECC 取 MEDIUM：屏幕上被反光/缩放吃掉一点还能扫。
        // QrCode 没有默认构造，所以用 lambda 把异常挡在外面。
        const auto encoded = [&text]() -> std::optional<qrcodegen::QrCode> {
            try
            {
                return qrcodegen::QrCode::encodeText(std::string{text}.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
            }
            catch (...)
            {
                return std::nullopt;
            }
        }();
        if (!encoded.has_value())
        {
            return rows;
        }
        const qrcodegen::QrCode &code = *encoded;

        const int size = code.getSize();
        const int side = size + 2; // 左右各 1 格白边
        rows.reserve(static_cast<std::size_t>(side));
        const std::string border(static_cast<std::size_t>(side), '0');
        rows.push_back(border);
        for (int y = 0; y < size; ++y)
        {
            std::string line(static_cast<std::size_t>(side), '0');
            for (int x = 0; x < size; ++x)
            {
                if (code.getModule(x, y))
                {
                    line[static_cast<std::size_t>(x + 1)] = '1';
                }
            }
            rows.push_back(std::move(line));
        }
        rows.push_back(border);
        return rows;
    }

} // namespace sonora::online::core::auth

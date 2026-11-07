#include "sonora/online/core/auth/CookieHeader.h"

#include <algorithm>
#include <cctype>

namespace sonora::online::core::auth
{
    namespace
    {
        std::string_view trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                                     text.front() == '\n'))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                                     text.back() == '\n'))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < lhs.size(); ++i)
            {
                const auto a = static_cast<unsigned char>(lhs[i]);
                const auto b = static_cast<unsigned char>(rhs[i]);
                if (std::tolower(a) != std::tolower(b))
                {
                    return false;
                }
            }
            return true;
        }

        // 去掉 `Cookie:` 前缀：用户的粘贴来源不可控，多这么一步就少一类"登录失败"。
        std::string_view stripScheme(std::string_view text)
        {
            text = trim(text);
            constexpr std::string_view kPrefix = "Cookie:";
            if (text.size() >= kPrefix.size() && equalsIgnoreCase(text.substr(0, kPrefix.size()), kPrefix))
            {
                text = trim(text.substr(kPrefix.size()));
            }
            return text;
        }
    } // namespace

    CookieParts parseCookieHeader(std::string_view header)
    {
        CookieParts parts;
        header = stripScheme(header);

        while (!header.empty())
        {
            const std::size_t semicolon = header.find(';');
            std::string_view chunk = semicolon == std::string_view::npos ? header : header.substr(0, semicolon);
            header = semicolon == std::string_view::npos ? std::string_view{} : header.substr(semicolon + 1);

            chunk = trim(chunk);
            const std::size_t equals = chunk.find('=');
            if (equals == std::string_view::npos)
            {
                continue;
            }

            const std::string_view key = trim(chunk.substr(0, equals));
            const std::string_view value = trim(chunk.substr(equals + 1));
            if (value.empty())
            {
                continue;
            }

            if (equalsIgnoreCase(key, "MUSIC_U"))
            {
                parts.musicU.assign(value);
            }
            else if (equalsIgnoreCase(key, "__csrf"))
            {
                parts.csrf.assign(value);
            }
        }
        return parts;
    }

    std::string buildCookieHeader(const CookieParts &parts)
    {
        std::string out;
        if (!parts.musicU.empty())
        {
            out += "MUSIC_U=";
            out += parts.musicU;
        }
        if (!parts.csrf.empty())
        {
            if (!out.empty())
            {
                out += "; ";
            }
            out += "__csrf=";
            out += parts.csrf;
        }
        return out;
    }

} // namespace sonora::online::core::auth

#include "mamba/lyrics/adapters/SidecarLyricsSource.h"

#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "PathText.h"

namespace mamba::lyrics::adapters {
namespace {

using ByteBuffer = std::string;

[[nodiscard]] kernel::Result<ByteBuffer> readAllBytes(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return kernel::Result<ByteBuffer>::fail(
            kernel::Error{kernel::ErrorCode::Unavailable, "歌词文件打不开：" + detail::u8String(path)});
    }
    ByteBuffer bytes;
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size > 0) {
        bytes.reserve(static_cast<std::size_t>(size));
    }
    stream.seekg(0, std::ios::beg);
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return kernel::Result<ByteBuffer>::ok(std::move(bytes));
}

// BOM → 统一转成 UTF-8。返回值带"编码说明"，出错时能告诉用户到底怎么回事。
struct DecodedText {
    std::string text;
    bool fromUtf16 = false;
    bool hadBom = false;
};

[[nodiscard]] kernel::Result<DecodedText> decodeText(const ByteBuffer &bytes)
{
    DecodedText decoded;
    std::string_view view{bytes};

    if (view.size() >= 3 && view.substr(0, 3) == "\xEF\xBB\xBF") {
        view.remove_prefix(3);
        decoded.hadBom = true;
        if (!detail::isValidUtf8(view)) {
            return kernel::Result<DecodedText>::fail(
                kernel::Error{kernel::ErrorCode::Unavailable, "歌词文件带 UTF-8 BOM，但内容不是合法 UTF-8"});
        }
        decoded.text = std::string(view);
        return kernel::Result<DecodedText>::ok(std::move(decoded));
    }
    if (view.size() >= 2 && static_cast<unsigned char>(view[0]) == 0xFF
        && static_cast<unsigned char>(view[1]) == 0xFE) {
        decoded.fromUtf16 = true;
        decoded.hadBom = true;
        decoded.text = detail::utf16ToUtf8(view.substr(2), false);
        return kernel::Result<DecodedText>::ok(std::move(decoded));
    }
    if (view.size() >= 2 && static_cast<unsigned char>(view[0]) == 0xFE
        && static_cast<unsigned char>(view[1]) == 0xFF) {
        decoded.fromUtf16 = true;
        decoded.hadBom = true;
        decoded.text = detail::utf16ToUtf8(view.substr(2), true);
        return kernel::Result<DecodedText>::ok(std::move(decoded));
    }

    if (!detail::isValidUtf8(view)) {
        // 中文机上常见的是 GBK。宁可说"不支持"，也不要把乱码糊到屏幕上——
        // 糊上去用户会以为是我们坏了，说清楚了他还能自己转存成 UTF-8。
        return kernel::Result<DecodedText>::fail(
            kernel::Error{kernel::ErrorCode::Unavailable,
                          "歌词文件不是 UTF-8 编码（很可能是 GBK）。用记事本另存为 UTF-8 即可"});
    }
    decoded.text = std::string(view);
    return kernel::Result<DecodedText>::ok(std::move(decoded));
}

}  // namespace

kernel::Result<std::optional<core::LyricsDocument>>
SidecarLyricsSource::load(std::string_view audioFilePath) const
{
    if (audioFilePath.empty()) {
        return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
    }

    const std::filesystem::path audio = detail::u8Path(std::string(audioFilePath));
    std::error_code error;
    if (!std::filesystem::exists(audio, error)) {
        return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
    }

    // 只换扩展名，保留原文件名（含空格与括号）：`大张伟 - 阳光彩虹小白马.lrc`。
    std::filesystem::path sidecar = audio;
    sidecar.replace_extension(".lrc");
    if (!std::filesystem::exists(sidecar, error)) {
        return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
    }

    auto bytes = readAllBytes(sidecar);
    if (!bytes) {
        return kernel::Result<std::optional<core::LyricsDocument>>::fail(bytes.error());
    }
    auto decoded = decodeText(bytes.value());
    if (!decoded) {
        return kernel::Result<std::optional<core::LyricsDocument>>::fail(decoded.error());
    }

    core::LyricsDocument document;
    document.text = std::move(decoded.value().text);
    document.source = "同名 .lrc";
    return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::move(document));
}

}  // namespace mamba::lyrics::adapters

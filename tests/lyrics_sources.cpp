// 歌词来源（适配器）与查询 handler 的集成测。
//
// 这里用的是**真文件系统 + 真编解码**，不是假端口：M7 的两种来源
// （同名 .lrc / 内嵌标签）里最常出问题的是编码与路径，假端口测不出来。
#include "doctest_support.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "sonora/lyrics/adapters/LyricsSourceChain.h"
#include "sonora/lyrics/adapters/SidecarLyricsSource.h"
#include "sonora/lyrics/contracts/Queries.h"
#include "sonora/lyrics/core/app/GetLyricsHandler.h"
#include "sonora/lyrics/core/ports/Ports.h"

namespace {

namespace fs = std::filesystem;
using sonora::lyrics::adapters::LyricsSourceChain;
using sonora::lyrics::adapters::SidecarLyricsSource;
using sonora::lyrics::contracts::LyricsStatus;
using sonora::lyrics::core::GetLyricsHandler;
using sonora::lyrics::core::ILyricsSource;
using sonora::lyrics::core::LyricsDocument;

fs::path u8Path(const std::string &utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string u8String(const fs::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// 测试工作区放工程内的 .test_tmp/：%TEMP% 在沙箱里可能写不进（M1 踩过）。
struct Workspace {
    fs::path root;

    Workspace()
    {
        root = fs::current_path() / ".test_tmp" / "lyrics_sources";
        std::error_code error;
        fs::remove_all(root, error);
        fs::create_directories(root, error);
    }
    ~Workspace()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }

    [[nodiscard]] fs::path write(const std::string &name, const std::string &bytes) const
    {
        const fs::path path = root / u8Path(name);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path;
    }
};

std::string utf16leBytes(const std::u16string &text)
{
    std::string out;
    out.reserve(text.size() * 2);
    for (const char16_t unit : text) {
        out.push_back(static_cast<char>(unit & 0xFF));
        out.push_back(static_cast<char>((unit >> 8) & 0xFF));
    }
    return out;
}

// 假的来源：用来验证"链"的顺序，不碰文件系统。
struct FakeSource final : ILyricsSource {
    std::optional<LyricsDocument> document;
    mutable int calls = 0;

    [[nodiscard]] sonora::kernel::Result<std::optional<LyricsDocument>>
    load(std::string_view) const override
    {
        ++calls;
        return sonora::kernel::Result<std::optional<LyricsDocument>>::ok(document);
    }
};

}  // namespace

TEST_CASE("同名 .lrc：找到了就能读出 UTF-8 文本")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("song.mp3", "not really audio");
    (void)workspace.write("song.lrc", "[00:01.00]甲\n[00:02.00]乙\n");

    const SidecarLyricsSource source;
    const auto document = source.load(u8String(audio));
    REQUIRE(document.hasValue());
    REQUIRE(document.value().has_value());
    CHECK(document.value()->source == "同名 .lrc");
    CHECK(document.value()->text.find("[00:02.00]乙") != std::string::npos);
}

TEST_CASE("同名 .lrc：没有就是没有，不是错误")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("lonely.mp3", "not really audio");

    const SidecarLyricsSource source;
    const auto document = source.load(u8String(audio));
    REQUIRE(document.hasValue());
    CHECK_FALSE(document.value().has_value());
}

TEST_CASE("同名 .lrc：UTF-16LE + BOM 会被转成 UTF-8")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("wide.mp3", "not really audio");
    const std::string bom = "\xFF\xFE";
    (void)workspace.write("wide.lrc", bom + utf16leBytes(u"[00:01.00]甲\n"));

    const SidecarLyricsSource source;
    const auto document = source.load(u8String(audio));
    REQUIRE(document.hasValue());
    REQUIRE(document.value().has_value());
    CHECK(document.value()->text == "[00:01.00]甲\n");
}

TEST_CASE("同名 .lrc：GBK 文件被明确拒绝，不把乱码画上屏幕")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("gbk.mp3", "not really audio");
    // "一起" 的 GBK 字节，不是合法 UTF-8。
    (void)workspace.write("gbk.lrc", "[00:01.00]\xD2\xBB\xC6\xF0\n");

    const SidecarLyricsSource source;
    const auto document = source.load(u8String(audio));
    REQUIRE(document.hasValue() == false);
    CHECK(document.error().code() == sonora::kernel::ErrorCode::Unavailable);
    CHECK(document.error().message().find("UTF-8") != std::string::npos);
}

TEST_CASE("来源链：按加入顺序找，第一个有文本的赢")
{
    FakeSource first;
    FakeSource second;
    second.document = LyricsDocument{"内嵌标签 LYRICS", "[00:01.00]乙\n"};

    LyricsSourceChain chain;
    chain.add(first);
    chain.add(second);
    CHECK(chain.size() == 2);

    const auto document = chain.load("C:/music/nothing.flac");
    REQUIRE(document.hasValue());
    REQUIRE(document.value().has_value());
    CHECK(document.value()->source == "内嵌标签 LYRICS");
    CHECK(first.calls == 1);
    CHECK(second.calls == 1);
}

TEST_CASE("来源链：全都没有就是 nullopt")
{
    FakeSource first;
    FakeSource second;
    LyricsSourceChain chain;
    chain.add(first);
    chain.add(second);

    const auto document = chain.load("C:/music/nothing.flac");
    REQUIRE(document.hasValue());
    CHECK_FALSE(document.value().has_value());
    CHECK(second.calls == 1);
}

TEST_CASE("GetLyricsHandler：真文件 → Found，行数与时间都对")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("end.mp3", "not really audio");
    (void)workspace.write("end.lrc", "[00:01.00]甲\n[00:01.00]translation\n[00:03.50]乙\n");

    const SidecarLyricsSource source;
    const GetLyricsHandler handler{source};
    const auto view = handler.handle(sonora::lyrics::contracts::GetLyrics{u8String(audio)});
    REQUIRE(view.hasValue());
    CHECK(view.value().status == LyricsStatus::Found);
    REQUIRE(view.value().lines.size() == 2);
    CHECK(view.value().lines[0].text == "甲");
    CHECK(view.value().lines[0].translation == "translation");
    CHECK(view.value().lines[1].timeMs == 3500);
}

TEST_CASE("GetLyricsHandler：没有歌词源 → NotFound，零行")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("nolyrics.mp3", "not really audio");

    const SidecarLyricsSource source;
    const GetLyricsHandler handler{source};
    const auto view = handler.handle(sonora::lyrics::contracts::GetLyrics{u8String(audio)});
    REQUIRE(view.hasValue());
    CHECK(view.value().status == LyricsStatus::NotFound);
    CHECK(view.value().lines.empty());
}

TEST_CASE("GetLyricsHandler：有文件但没有时间标签 → 纯音乐占位一句话")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("inst.mp3", "not really audio");
    (void)workspace.write("inst.lrc", "[ti:某首纯音乐]\n[ar:某人]\n");

    const SidecarLyricsSource source;
    const GetLyricsHandler handler{source};
    const auto view = handler.handle(sonora::lyrics::contracts::GetLyrics{u8String(audio)});
    REQUIRE(view.hasValue());
    CHECK(view.value().status == LyricsStatus::Instrumental);
    REQUIRE(view.value().lines.size() == 1);
    CHECK(view.value().lines[0].text == "纯音乐，请欣赏");
}

TEST_CASE("GetLyricsHandler：空路径是参数错误")
{
    const SidecarLyricsSource source;
    const GetLyricsHandler handler{source};
    const auto view = handler.handle(sonora::lyrics::contracts::GetLyrics{});
    REQUIRE(view.hasValue() == false);
    CHECK(view.error().code() == sonora::kernel::ErrorCode::InvalidArgument);
}

// ── N3：第三个来源（在线歌词）────────────────────────────────────────────
//
// 这里用一个"假在线来源"而不是真 OnlineLyricsSource：后者的行为取决于
// 在线服务的内存状态（取回没有、有没有错），那是 online 模块自己的测。
// 歌词这边要钉住的是**链子与 handler 的语义**：排在最后、pending 会不会
// 停住、取不到时不报错。
namespace {

class StubOnlineLyrics final : public ILyricsSource {
public:
    explicit StubOnlineLyrics(std::optional<LyricsDocument> document)
        : document_(std::move(document)) {}

    [[nodiscard]] sonora::kernel::Result<std::optional<LyricsDocument>>
    load(std::string_view) const override
    {
        return sonora::kernel::Result<std::optional<LyricsDocument>>::ok(std::nullopt);
    }

    [[nodiscard]] sonora::kernel::Result<std::optional<LyricsDocument>>
    load(const sonora::lyrics::core::LyricsRequest &) const override
    {
        return sonora::kernel::Result<std::optional<LyricsDocument>>::ok(document_);
    }

private:
    std::optional<LyricsDocument> document_;
};

LyricsDocument onlineDocument(std::string text, bool pending = false)
{
    LyricsDocument document;
    document.source = "在线歌词";
    document.text = std::move(text);
    document.pending = pending;
    document.online = true;
    return document;
}

}  // namespace

TEST_CASE("在线歌词：本地没有时问在线，取到就是 Found 并标 online")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("online.mp3", "not really audio");

    const SidecarLyricsSource sidecar;
    const StubOnlineLyrics online{onlineDocument("[00:02.00]网上的词\n[00:05.00]第二句")};
    LyricsSourceChain chain;
    chain.add(sidecar);
    chain.add(online);

    const GetLyricsHandler handler{chain};
    sonora::lyrics::contracts::GetLyrics query;
    query.filePath = u8String(audio);
    query.onlineSongId = "12345";

    const auto view = handler.handle(query);
    REQUIRE(view.hasValue());
    CHECK(view.value().status == LyricsStatus::Found);
    CHECK(view.value().onlineSource);
    CHECK(view.value().source == "在线歌词");
    REQUIRE(view.value().lines.size() == 2);
    CHECK(view.value().lines[0].timeMs == 2000);
}

TEST_CASE("在线歌词：还没取回来时是 pending，不是 NotFound 也不是错误")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("pending.mp3", "not really audio");

    const SidecarLyricsSource sidecar;
    const StubOnlineLyrics online{onlineDocument({}, true)};
    LyricsSourceChain chain;
    chain.add(sidecar);
    chain.add(online);

    const GetLyricsHandler handler{chain};
    sonora::lyrics::contracts::GetLyrics query;
    query.filePath = u8String(audio);
    query.onlineSongId = "999";

    const auto view = handler.handle(query);
    REQUIRE(view.hasValue());
    CHECK(view.value().pending);
    CHECK(view.value().status == LyricsStatus::NotFound);
    CHECK(view.value().lines.empty());
}

TEST_CASE("在线歌词：在线也没有词时是 NotFound，不是错误")
{
    const Workspace workspace;
    const fs::path audio = workspace.write("nolyric.mp3", "not really audio");

    const SidecarLyricsSource sidecar;
    const StubOnlineLyrics online{std::nullopt};
    LyricsSourceChain chain;
    chain.add(sidecar);
    chain.add(online);

    const GetLyricsHandler handler{chain};
    sonora::lyrics::contracts::GetLyrics query;
    query.filePath = u8String(audio);
    query.onlineSongId = "777";

    const auto view = handler.handle(query);
    REQUIRE(view.hasValue());
    CHECK(view.value().status == LyricsStatus::NotFound);
    CHECK(view.value().pending == false);
}

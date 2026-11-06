#include "sonora/platform/audio/FfmpegDecoder.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include "sonora/platform/audio/AudioRing.h"

namespace sonora::platform::audio {
namespace {

kernel::Error makeError(kernel::ErrorCode code, std::string message)
{
    return kernel::Error{code, std::move(message)};
}

// av_strerror 转人话。错误码要出现在日志里，否则"为什么这首放不出来"永远查不清。
std::string avError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return std::string{buffer};
}

void logError(const char* what, int code)
{
    qWarning("[ffmpeg] %s 失败：%s", what, avError(code).c_str());
}

}  // namespace

struct FfmpegDecoder::Impl {
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int streamIndex = -1;
    AVRational timeBase{1, 1000};

    void reset() noexcept
    {
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
        if (resampler != nullptr) {
            swr_free(&resampler);
        }
        if (codecContext != nullptr) {
            avcodec_free_context(&codecContext);
        }
        if (formatContext != nullptr) {
            avformat_close_input(&formatContext);
        }
        streamIndex = -1;
    }
};

FfmpegDecoder::FfmpegDecoder()
    : impl_(new Impl())
{
}

FfmpegDecoder::~FfmpegDecoder()
{
    close();
    delete impl_;
}

void FfmpegDecoder::close() noexcept
{
    pending_.clear();
    pending_.shrink_to_fit();
    pendingCount_ = 0;
    drained_ = false;
    samplesProduced_ = 0;
    format_ = Format{};
    impl_->reset();
}

kernel::Result<FfmpegDecoder::Format> FfmpegDecoder::open(std::string_view utf8Path)
{
    close();

    AVDictionary* options = nullptr;
    // 默认的探测会很耐心（几 MB、几秒），本地文件不需要——它只会让"双击一首歌"
    // 感觉慢半拍。0.5s 对本地音频够用，网络流才会需要更多。
    av_dict_set(&options, "probesize", "262144", 0);
    av_dict_set(&options, "analyzeduration", "500000", 0);

    const std::string path{utf8Path};
    int ret = avformat_open_input(&impl_->formatContext, path.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (ret < 0) {
        logError("avformat_open_input", ret);
        return makeError(kernel::ErrorCode::NotFound, "打不开文件：" + path);
    }

    ret = avformat_find_stream_info(impl_->formatContext, nullptr);
    if (ret < 0) {
        logError("avformat_find_stream_info", ret);
        impl_->reset();
        return makeError(kernel::ErrorCode::InvalidArgument, "读不出流信息：" + path);
    }

    const AVCodec* codec = nullptr;
    impl_->streamIndex = av_find_best_stream(impl_->formatContext, AVMEDIA_TYPE_AUDIO,
                                             -1, -1, &codec, 0);
    if (impl_->streamIndex < 0 || codec == nullptr) {
        logError("av_find_best_stream", impl_->streamIndex);
        impl_->reset();
        return makeError(kernel::ErrorCode::NotFound, "这个文件里没有可解码的音轨：" + path);
    }

    AVStream* stream = impl_->formatContext->streams[impl_->streamIndex];
    impl_->timeBase = stream->time_base;

    impl_->codecContext = avcodec_alloc_context3(codec);
    if (impl_->codecContext == nullptr) {
        impl_->reset();
        return makeError(kernel::ErrorCode::Internal, "分配解码器失败");
    }
    ret = avcodec_parameters_to_context(impl_->codecContext, stream->codecpar);
    if (ret < 0) {
        logError("avcodec_parameters_to_context", ret);
        impl_->reset();
        return makeError(kernel::ErrorCode::InvalidArgument, "解码参数不合法");
    }
    // 解码线程常常不是"性能核"，但解码本身是瓶颈；auto 让 FFmpeg 自己挑。
    impl_->codecContext->thread_count = 0;
    ret = avcodec_open2(impl_->codecContext, codec, nullptr);
    if (ret < 0) {
        logError("avcodec_open2", ret);
        impl_->reset();
        return makeError(kernel::ErrorCode::Unavailable, "打不开解码器：" + std::string{codec->name});
    }

    // 统一出口格式：交错 float32、最多两声道。
    const int sourceChannels = impl_->codecContext->ch_layout.nb_channels > 0
                                   ? impl_->codecContext->ch_layout.nb_channels
                                   : 2;
    format_.channels = std::min(sourceChannels, 2);
    format_.sampleRate = impl_->codecContext->sample_rate > 0 ? impl_->codecContext->sample_rate : 48000;

    AVChannelLayout sourceLayout{};
    if (impl_->codecContext->ch_layout.nb_channels > 0) {
        av_channel_layout_copy(&sourceLayout, &impl_->codecContext->ch_layout);
    } else {
        av_channel_layout_default(&sourceLayout, format_.channels);
    }
    AVChannelLayout targetLayout{};
    av_channel_layout_default(&targetLayout, format_.channels);

    // 9.x：swr_alloc_set_opts() 已废弃，必须用 swr_alloc_set_opts2()，
    // 且声道信息是 AVChannelLayout 而不是 channel_layout 位掩码。
    ret = swr_alloc_set_opts2(&impl_->resampler,
                              &targetLayout, AV_SAMPLE_FMT_FLT, format_.sampleRate,
                              &sourceLayout,
                              impl_->codecContext->sample_fmt,
                              impl_->codecContext->sample_rate,
                              0, nullptr);
    av_channel_layout_uninit(&sourceLayout);
    av_channel_layout_uninit(&targetLayout);
    if (ret < 0 || impl_->resampler == nullptr) {
        logError("swr_alloc_set_opts2", ret);
        impl_->reset();
        return makeError(kernel::ErrorCode::Internal, "重采样器初始化失败");
    }
    ret = swr_init(impl_->resampler);
    if (ret < 0) {
        logError("swr_init", ret);
        impl_->reset();
        return makeError(kernel::ErrorCode::Internal, "重采样器初始化失败");
    }

    impl_->packet = av_packet_alloc();
    impl_->frame = av_frame_alloc();
    if (impl_->packet == nullptr || impl_->frame == nullptr) {
        impl_->reset();
        return makeError(kernel::ErrorCode::Internal, "分配 FFmpeg 缓冲失败");
    }

    if (stream->duration > 0) {
        format_.durationMs = av_rescale_q(stream->duration, impl_->timeBase, AVRational{1, 1000});
    } else if (impl_->formatContext->duration > 0) {
        format_.durationMs = impl_->formatContext->duration / (AV_TIME_BASE / 1000);
    }

    qInfo("[ffmpeg] 打开 %s：%d Hz / %d 声道 / %s / %lld ms",
          path.c_str(), format_.sampleRate, format_.channels,
          codec->name, static_cast<long long>(format_.durationMs));
    return kernel::Result<Format>::ok(format_);
}

kernel::Result<bool> FfmpegDecoder::decodeOneFrame()
{
    while (true) {
        int ret = avcodec_receive_frame(impl_->codecContext, impl_->frame);
        if (ret == 0) {
            convertFrame();
            av_frame_unref(impl_->frame);
            return kernel::Result<bool>::ok(true);
        }
        if (ret == AVERROR_EOF) {
            drained_ = true;
            return kernel::Result<bool>::ok(false);
        }
        if (ret != AVERROR(EAGAIN)) {
            logError("avcodec_receive_frame", ret);
            return makeError(kernel::ErrorCode::Internal, "解码失败：数据损坏");
        }

        // 解码器要更多输入
        ret = av_read_frame(impl_->formatContext, impl_->packet);
        if (ret == AVERROR_EOF) {
            // 9.x 的收尾姿势：送一个空 packet，再继续 receive 直到 EOF
            avcodec_send_packet(impl_->codecContext, nullptr);
            continue;
        }
        if (ret < 0) {
            logError("av_read_frame", ret);
            drained_ = true;
            return kernel::Result<bool>::ok(false);
        }
        if (impl_->packet->stream_index != impl_->streamIndex) {
            av_packet_unref(impl_->packet);
            continue;   // 封面图之类，不是音轨
        }

        ret = avcodec_send_packet(impl_->codecContext, impl_->packet);
        av_packet_unref(impl_->packet);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            logError("avcodec_send_packet", ret);
            return makeError(kernel::ErrorCode::Internal, "送包失败");
        }
    }
}

void FfmpegDecoder::convertFrame() noexcept
{
    const int channels = format_.channels;
    const int inSamples = impl_->frame->nb_samples;
    if (inSamples <= 0 || channels <= 0) {
        return;
    }

    const int outCapacity = static_cast<int>(
        av_rescale_rnd(swr_get_delay(impl_->resampler, impl_->codecContext->sample_rate) + inSamples,
                       format_.sampleRate, impl_->codecContext->sample_rate, AV_ROUND_UP));
    if (outCapacity <= 0) {
        return;
    }

    const std::size_t needed = static_cast<std::size_t>(outCapacity) * static_cast<std::size_t>(channels);
    if (pending_.size() < needed) {
        pending_.resize(needed);
    }

    std::uint8_t* out = reinterpret_cast<std::uint8_t*>(pending_.data());
    const int converted = swr_convert(impl_->resampler, &out, outCapacity,
                                      const_cast<const std::uint8_t**>(impl_->frame->extended_data),
                                      inSamples);
    if (converted > 0) {
        pendingCount_ = static_cast<std::size_t>(converted) * static_cast<std::size_t>(channels);

        // 跳转后第一帧：按时间戳把"多了的"开头丢干净，位置才会落到用户点的地方。
        if (seekPending_) {
            seekPending_ = false;
            skipSamples_ = 0;
            if (impl_->frame->pts != AV_NOPTS_VALUE) {
                const std::int64_t frameMs = av_rescale_q(impl_->frame->pts, impl_->timeBase,
                                                          AVRational{1, 1000});
                const std::int64_t overshootMs = seekTargetMs_ - frameMs;
                if (overshootMs > 0) {
                    skipSamples_ = overshootMs * format_.sampleRate / 1000;
                }
            }
        }

        if (skipSamples_ > 0) {
            const auto toDrop = static_cast<std::size_t>(
                std::min<std::int64_t>(skipSamples_, static_cast<std::int64_t>(converted)));
            const std::size_t dropElements = toDrop * static_cast<std::size_t>(channels);
            if (dropElements >= pendingCount_) {
                pendingCount_ = 0;
            } else {
                pending_.erase(pending_.begin(),
                               pending_.begin() + static_cast<std::ptrdiff_t>(dropElements));
                pendingCount_ -= dropElements;
            }
            skipSamples_ -= static_cast<std::int64_t>(toDrop);
        }
    }
}

kernel::Result<std::size_t> FfmpegDecoder::pump(AudioRing& ring)
{
    std::size_t written = 0;

    while (true) {
        // 先把上一帧的残余写完
        if (pendingCount_ > 0) {
            const std::size_t n = ring.write(pending_.data(), pendingCount_);
            written += n;
            if (n < pendingCount_) {
                // 缓冲满了：把剩下的挪到前面，等下次继续
                pending_.erase(pending_.begin(),
                               pending_.begin() + static_cast<std::ptrdiff_t>(n));
                pendingCount_ -= n;
                samplesProduced_ += static_cast<std::int64_t>(n);
                return kernel::Result<std::size_t>::ok(written);
            }
            pendingCount_ = 0;
            samplesProduced_ += static_cast<std::int64_t>(n);
        }

        if (drained_) {
            return kernel::Result<std::size_t>::ok(written);
        }

        // 缓冲还空着才继续解；否则再解也是堆在自己手里占内存
        if (ring.availableToWrite() == 0) {
            return kernel::Result<std::size_t>::ok(written);
        }

        auto more = decodeOneFrame();
        if (!more.hasValue()) {
            return more.error();
        }
        if (!more.value()) {
            return kernel::Result<std::size_t>::ok(written);
        }
    }
}

kernel::Result<std::int64_t> FfmpegDecoder::seekTo(std::int64_t ms)
{
    if (impl_->formatContext == nullptr || impl_->streamIndex < 0) {
        return makeError(kernel::ErrorCode::Unavailable, "还没有打开文件");
    }

    const std::int64_t target = std::max<std::int64_t>(0, ms);
    const std::int64_t timestamp = av_rescale_q(target, AVRational{1, 1000}, impl_->timeBase);
    // avformat_seek_file（而不是 av_seek_frame）才能表达"我要最接近这个时间点"：
    // min/max 都给到极限，让 demuxer 自己挑最合适的落点。
    int ret = avformat_seek_file(impl_->formatContext, impl_->streamIndex,
                                 std::numeric_limits<std::int64_t>::min(), timestamp,
                                 std::numeric_limits<std::int64_t>::max(), 0);
    if (ret < 0) {
        logError("avformat_seek_file", ret);
        ret = av_seek_frame(impl_->formatContext, impl_->streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            logError("av_seek_frame", ret);
            return makeError(kernel::ErrorCode::Unavailable, "跳转失败");
        }
    }

    avcodec_flush_buffers(impl_->codecContext);
    // 重采样器里可能还压着几十毫秒的旧数据，不清掉就会听到"前一秒"。
    swr_convert(impl_->resampler, nullptr, 0, nullptr, 0);

    pendingCount_ = 0;
    drained_ = false;
    samplesProduced_ = 0;
    // 容器只能跳到"附近"：下一帧的时间戳很可能早于目标，记下差额，
    // 等那一帧解出来再从头部丢掉，这样 positionMs 才和用户点的地方对得上。
    seekTargetMs_ = target;
    seekPending_ = true;
    skipSamples_ = 0;
    return kernel::Result<std::int64_t>::ok(target);
}

}  // namespace sonora::platform::audio

#include "VideoBackend.hpp"
#include "../helpers/Log.hpp"
#include <algorithm>

CVideoBackend::~CVideoBackend() {
    stop();
}

bool CVideoBackend::isVideoFile(const std::string& path) {
    static const std::unordered_set<std::string> VIDEO_EXT = {
        ".mp4", ".mkv", ".webm", ".avi", ".mov", ".m4v",
        ".flv", ".wmv", ".ts",   ".m2ts", ".gif"
    };
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return VIDEO_EXT.count(ext) > 0;
}

bool CVideoBackend::open(const std::string& path) {
    if (avformat_open_input(&m_formatCtx, path.c_str(), nullptr, nullptr) < 0) {
        Log::log(Log::ERR, "CVideoBackend: avformat_open_input failed for {}", path);
        return false;
    }
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        Log::log(Log::ERR, "CVideoBackend: avformat_find_stream_info failed for {}", path);
        return false;
    }

    const AVCodec* codec = nullptr;
    m_streamIdx = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (m_streamIdx < 0 || !codec) {
        Log::log(Log::ERR, "CVideoBackend: no video stream found in {}", path);
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        Log::log(Log::ERR, "CVideoBackend: avcodec_alloc_context3 failed");
        return false;
    }

    avcodec_parameters_to_context(m_codecCtx, m_formatCtx->streams[m_streamIdx]->codecpar);

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        Log::log(Log::ERR, "CVideoBackend: avcodec_open2 failed for {}", path);
        return false;
    }

    m_frameW   = m_codecCtx->width;
    m_frameH   = m_codecCtx->height;
    m_timeBase = av_q2d(m_formatCtx->streams[m_streamIdx]->time_base);

    // swsCtx is created lazily per-frame via sws_getCachedContext so that
    // we handle codecs where pix_fmt is only known after the first decode.
    m_frameData.resize(4 * m_frameW * m_frameH);

    Log::log(Log::LOG, "CVideoBackend: opened {} ({}x{}, timebase={:.6f})",
               path, m_frameW, m_frameH, m_timeBase);

    startDecodeThread();
    return true;
}

void CVideoBackend::stop() {
    m_running = false;
    if (m_decodeThread.joinable())
        m_decodeThread.join();
    if (m_swsCtx)    sws_freeContext(m_swsCtx);
    if (m_codecCtx)  avcodec_free_context(&m_codecCtx);
    if (m_formatCtx) avformat_close_input(&m_formatCtx);
    m_swsCtx    = nullptr;
    m_codecCtx  = nullptr;
    m_formatCtx = nullptr;
}

bool CVideoBackend::swapFrame(std::vector<uint8_t>& buf) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (!m_hasNewFrame)
        return false;
    std::swap(m_frameData, buf);
    m_hasNewFrame = false;
    return true;
}

void CVideoBackend::startDecodeThread() {
    m_running   = true;
    m_startTime = std::chrono::steady_clock::now();

    m_decodeThread = std::thread([this]() {
        AVPacket* pkt   = av_packet_alloc();
        AVFrame*  frame = av_frame_alloc();
        // Pre-size the tmp buffer so it's never empty when swapping with m_frameData.
        // An empty vector has data()==null which causes "bad dst image pointers" in sws_scale.
        std::vector<uint8_t> tmpBuf(4 * m_frameW * m_frameH);

        while (m_running) {
            int ret = av_read_frame(m_formatCtx, pkt);

            if (ret == AVERROR_EOF) {
                // Flush decoder's internal buffer
                avcodec_send_packet(m_codecCtx, nullptr);
                while (avcodec_receive_frame(m_codecCtx, frame) == 0)
                    av_frame_unref(frame);

                // Loop: seek back to beginning
                av_seek_frame(m_formatCtx, m_streamIdx, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(m_codecCtx);
                m_startTime = std::chrono::steady_clock::now();
                continue;
            }

            if (ret < 0)
                break; // unrecoverable error

            if (pkt->stream_index != m_streamIdx) {
                av_packet_unref(pkt);
                continue;
            }

            if (avcodec_send_packet(m_codecCtx, pkt) < 0) {
                av_packet_unref(pkt);
                continue;
            }
            av_packet_unref(pkt);

            while (avcodec_receive_frame(m_codecCtx, frame) == 0) {
                if (!m_running)
                    break;

                // Lazily create/update SwsContext to match the frame's actual pixel
                // format (some codecs only report it after the first frame).
                m_swsCtx = sws_getCachedContext(m_swsCtx,
                    frame->width, frame->height, (AVPixelFormat)frame->format,
                    m_frameW, m_frameH, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!m_swsCtx) {
                    av_frame_unref(frame);
                    continue;
                }

                // sws_scale requires 4-element pointer/stride arrays even for
                // packed formats — passing a 1-element array causes UB reads.
                uint8_t* dst[4]    = {tmpBuf.data(), nullptr, nullptr, nullptr};
                int      stride[4] = {4 * m_frameW,  0,       0,       0};
                sws_scale(m_swsCtx,
                          (const uint8_t* const*)frame->data, frame->linesize,
                          0, frame->height, dst, stride);

                // Publish frame via O(1) swap (no memcpy)
                {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    std::swap(m_frameData, tmpBuf);
                    m_hasNewFrame = true;
                }
                // tmpBuf now holds old frame data — overwritten next iteration

                // PTS-based frame pacing
                if (frame->pts != AV_NOPTS_VALUE) {
                    double pts_sec = frame->pts * m_timeBase;
                    auto   target  = m_startTime +
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(pts_sec));
                    auto now       = std::chrono::steady_clock::now();
                    // Safety cap: never sleep > 5s (guards against bogus PTS values)
                    auto maxTarget = now + std::chrono::seconds(5);
                    if (target > now && target < maxTarget)
                        std::this_thread::sleep_until(target);
                }

                av_frame_unref(frame);
            }
        }

        av_packet_free(&pkt);
        av_frame_free(&frame);
    });
}

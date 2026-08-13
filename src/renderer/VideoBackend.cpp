#include "VideoBackend.hpp"
#include "../core/Egl.hpp"
#include "../helpers/Log.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_set>

#include <GLES3/gl32.h>

extern "C" {
#include <libavutil/display.h>
}

CVideoBackend::~CVideoBackend() {
    stop();
}

bool CVideoBackend::isVideoFile(const std::string& path) {
    static const std::unordered_set<std::string> VIDEO_EXT = {".mp4", ".mkv", ".webm", ".avi", ".mov", ".m4v", ".flv", ".wmv", ".ts", ".m2ts", ".gif"};

    auto                                         ext = std::filesystem::path(path).extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return VIDEO_EXT.contains(ext);
}

int CVideoBackend::interruptCallback(void* opaque) {
    // Lets stop() abort a blocking av_read_frame / avformat_open_input on a wedged source.
    return static_cast<CVideoBackend*>(opaque)->m_stopRequested.load() ? 1 : 0;
}

// Maps the frame's colorspace onto swscale's coefficient tables. Without this swscale
// converts everything with its BT.601 defaults, visibly shifting colors of BT.709 content
// (i.e. virtually all modern video).
static int swsColorspaceFor(AVColorSpace space, int height) {
    switch (space) {
        case AVCOL_SPC_BT709: return SWS_CS_ITU709;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M: return SWS_CS_ITU601;
        case AVCOL_SPC_SMPTE240M: return SWS_CS_SMPTE240M;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
        // Unspecified: HD content is almost always BT.709, SD almost always BT.601
        default: return height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    }
}

bool CVideoBackend::open(const std::string& path) {
    m_path = path;

    m_formatCtx = avformat_alloc_context();
    if (!m_formatCtx) {
        Log::logger->log(Log::ERR, "CVideoBackend: avformat_alloc_context failed");
        return false;
    }

    m_formatCtx->interrupt_callback.callback = &CVideoBackend::interruptCallback;
    m_formatCtx->interrupt_callback.opaque   = this;

    // Frees and nulls m_formatCtx on failure
    if (avformat_open_input(&m_formatCtx, path.c_str(), nullptr, nullptr) < 0) {
        Log::logger->log(Log::ERR, "CVideoBackend: avformat_open_input failed for {}", path);
        return false;
    }

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        Log::logger->log(Log::ERR, "CVideoBackend: avformat_find_stream_info failed for {}", path);
        return false;
    }

    const AVCodec* codec = nullptr;
    m_streamIdx          = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (m_streamIdx < 0 || !codec) {
        Log::logger->log(Log::ERR, "CVideoBackend: no video stream found in {}", path);
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        Log::logger->log(Log::ERR, "CVideoBackend: avcodec_alloc_context3 failed");
        return false;
    }

    avcodec_parameters_to_context(m_codecCtx, m_formatCtx->streams[m_streamIdx]->codecpar);

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        Log::logger->log(Log::ERR, "CVideoBackend: avcodec_open2 failed for {}", path);
        return false;
    }

    m_frameW   = m_codecCtx->width;
    m_frameH   = m_codecCtx->height;
    m_timeBase = av_q2d(m_formatCtx->streams[m_streamIdx]->time_base);

    // Display-matrix rotation (typical for phone footage)
    const auto* STREAM = m_formatCtx->streams[m_streamIdx];
    if (const auto* SD = av_packet_side_data_get(STREAM->codecpar->coded_side_data, STREAM->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        SD && SD->size >= 9 * sizeof(int32_t)) {
        const double THETA = av_display_rotation_get((const int32_t*)SD->data);
        if (!std::isnan(THETA)) {
            // av_display_rotation_get returns degrees counterclockwise; we want clockwise
            const int ROT = (((int)std::lround(-THETA) % 360) + 360) % 360;
            m_rotation    = (ROT / 90) * 90;
        }
    }

    // swsCtx is created lazily per-frame via sws_getCachedContext so that
    // we handle codecs where pix_fmt is only known after the first decode.
    const size_t FRAMEBYTES = 4UL * m_frameW * m_frameH;
    m_frameData.resize(FRAMEBYTES);
    m_uploadBuffer.resize(FRAMEBYTES);

    Log::logger->log(Log::INFO, "CVideoBackend: opened {} ({}x{}, timebase={:.6f}, rotation={})", path, m_frameW, m_frameH, m_timeBase, m_rotation);

    startDecodeThread();
    return true;
}

void CVideoBackend::stop() {
    m_stopRequested = true;
    m_stopCV.notify_all();

    if (m_decodeThread.joinable())
        m_decodeThread.join();

    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }

    if (m_codecCtx)
        avcodec_free_context(&m_codecCtx);

    if (m_formatCtx)
        avformat_close_input(&m_formatCtx);

    if (m_texture.m_bAllocated) {
        // GL teardown needs a current EGL context. During shutdown the surfaces may already
        // be destroyed, so fall back to binding the context surfacelessly.
        if (g_pEGL && eglGetCurrentContext() == EGL_NO_CONTEXT)
            g_pEGL->makeCurrent(EGL_NO_SURFACE);
        m_texture.destroyTexture();
    }
}

bool CVideoBackend::updateTexture() {
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (!m_hasNewFrame)
            return false;

        // O(1) swap, no memcpy; the decode thread reuses the swapped-out buffer
        std::swap(m_frameData, m_uploadBuffer);
        m_hasNewFrame = false;
    }

    if (!m_texture.m_bAllocated) {
        m_texture.allocate();
        glBindTexture(GL_TEXTURE_2D, m_texture.m_iTexID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_frameW, m_frameH, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_uploadBuffer.data());
        m_texture.m_vSize   = {(double)m_frameW, (double)m_frameH};
        m_texture.m_iType   = TEXTURE_RGBA;
        m_texture.m_iTarget = GL_TEXTURE_2D;
    } else {
        glBindTexture(GL_TEXTURE_2D, m_texture.m_iTexID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_frameW, m_frameH, GL_RGBA, GL_UNSIGNED_BYTE, m_uploadBuffer.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void CVideoBackend::startDecodeThread() {
    m_stopRequested = false;
    m_threadAlive   = true;
    m_startTime     = std::chrono::steady_clock::now();

    m_decodeThread = std::thread([this]() {
        AVPacket* pkt   = av_packet_alloc();
        AVFrame*  frame = av_frame_alloc();
        // Pre-size the tmp buffer so it's never empty when swapping with m_frameData.
        // An empty vector has data()==null which causes "bad dst image pointers" in sws_scale.
        std::vector<uint8_t> tmpBuf(4UL * m_frameW * m_frameH);

        while (!m_stopRequested) {
            const int RET = av_read_frame(m_formatCtx, pkt);

            if (RET == AVERROR_EOF) {
                // Loop: seek back to the beginning
                if (av_seek_frame(m_formatCtx, m_streamIdx, 0, AVSEEK_FLAG_BACKWARD) < 0) {
                    Log::logger->log(Log::WARN, "CVideoBackend: {} is not seekable, cannot loop; holding the last frame", m_path);
                    break;
                }

                avcodec_flush_buffers(m_codecCtx);
                m_startTime = std::chrono::steady_clock::now();
                continue;
            }

            if (RET < 0)
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

            while (!m_stopRequested && avcodec_receive_frame(m_codecCtx, frame) == 0) {
                // Lazily create/update SwsContext to match the frame's actual pixel
                // format (some codecs only report it after the first frame).
                SwsContext* prevCtx = m_swsCtx;
                m_swsCtx = sws_getCachedContext(m_swsCtx, frame->width, frame->height, (AVPixelFormat)frame->format, m_frameW, m_frameH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                                nullptr, nullptr);
                if (!m_swsCtx) {
                    av_frame_unref(frame);
                    continue;
                }

                if (m_swsCtx != prevCtx || frame->colorspace != m_lastColorspace || frame->color_range != m_lastRange) {
                    m_lastColorspace   = frame->colorspace;
                    m_lastRange        = frame->color_range;
                    const int  SRCFULL = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
                    const int* COEFFS  = sws_getCoefficients(swsColorspaceFor(frame->colorspace, frame->height));
                    // May fail for conversions that don't support it; swscale then keeps its defaults
                    sws_setColorspaceDetails(m_swsCtx, COEFFS, SRCFULL, sws_getCoefficients(SWS_CS_DEFAULT), 1 /* full-range RGB out */, 0, 1 << 16, 1 << 16);
                }

                // sws_scale requires 4-element pointer/stride arrays even for
                // packed formats — passing a 1-element array causes UB reads.
                uint8_t* dst[4]    = {tmpBuf.data(), nullptr, nullptr, nullptr};
                int      stride[4] = {4 * m_frameW, 0, 0, 0};
                sws_scale(m_swsCtx, (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height, dst, stride);

                // Publish the frame via O(1) swap (no memcpy)
                {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    std::swap(m_frameData, tmpBuf);
                    m_hasNewFrame = true;
                }
                // tmpBuf now holds old frame data — overwritten next iteration

                // PTS-based frame pacing, interruptible by stop()
                if (frame->pts != AV_NOPTS_VALUE) {
                    const double PTSSEC = frame->pts * m_timeBase;
                    const auto   TARGET = m_startTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(PTSSEC));
                    const auto   NOW    = std::chrono::steady_clock::now();
                    // Safety cap: never wait > 5s (guards against bogus PTS values)
                    if (TARGET > NOW && TARGET < NOW + std::chrono::seconds(5)) {
                        std::unique_lock<std::mutex> lock(m_stopMutex);
                        m_stopCV.wait_until(lock, TARGET, [this] { return m_stopRequested.load(); });
                    }
                }

                av_frame_unref(frame);
            }
        }

        av_packet_free(&pkt);
        av_frame_free(&frame);

        m_threadAlive = false;
    });
}

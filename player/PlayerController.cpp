#include "PlayerController.h"
#include <spdlog/spdlog.h>
#include "FFmpegWrapper.h"
#include "PlayerWidget.h"
#include <boost/lockfree/spsc_queue.hpp>
#include <future>

namespace {
AVFormatContext *format;
AVCodecContext *videoCodecContext;
AVCodecContext *audioCodecContext;
int videoStream;
int audioStream;
std::chrono::milliseconds g_start_time;
std::condition_variable g_cv_pause;
std::mutex g_mtx_pause;
std::atomic<std::chrono::milliseconds> g_pause_time;
std::atomic_bool g_is_paused = false;
boost::lockfree::spsc_queue<AVPacket *, boost::lockfree::capacity<4096>>
g_buffer_video;
boost::lockfree::spsc_queue<AVPacket *, boost::lockfree::capacity<4096>>
g_buffer_audio;
FFmpeg::SwrResample *g_swr{};

void startReadPacket(std::stop_token token) {
    AVPacket *packet{};
    while (!FFmpeg::readPaket(format, packet).hasErr()) {
        if (token.stop_requested()) {
            spdlog::info("stop decode thread");
            break;
        }
        if (packet->stream_index !=
            audioStream && packet->stream_index != videoStream) {
            spdlog::info("skip packet");
            av_packet_free(&packet);
            continue;
        }
        bool isVideo = packet->stream_index == videoStream;
        if (!g_buffer_video.write_available()) {
            spdlog::warn("buffer is full");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
    retry:
        if (token.stop_requested()) {
            spdlog::info("stop decode thread");
            break;
        }
        if (isVideo && !g_buffer_video.push(packet)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            goto retry;
        } else if (!isVideo && !g_buffer_audio.push(packet)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            goto retry;
        }
    }
}

void startVideoDecode(std::stop_token token, PlayerController *controller) {
    while (!token.stop_requested()) {
        AVPacket *packet{};

        if (!g_buffer_video.pop(packet)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        std::vector<AVFrame *> frames;
        if (FFmpeg::sendPacket2(videoCodecContext, packet, frames).
            hasErr()) {
            spdlog::error("sendPacket2 error");
            continue;
        }
        while (!token.stop_requested() && !frames.empty()) {
            AVFrame *frame = frames.back();
            frames.pop_back();

            uint64_t pts = packet->pts;
            av_packet_free(&packet);
            spdlog::info("sendVideoPacket frame [{}] [{}]", frame->width,
                         frame->height);
            std::vector<uint8_t> y;
            std::vector<uint8_t> u;
            std::vector<uint8_t> v;
            FFmpeg::decodeVideo(frame, y, u, v);
            // VideoFrameReady
            VideoInfo info;
            info.width = frame->width;
            info.height = frame->height;
            av_frame_free(&frame);
            spdlog::info("decodeVideo VideoInfo frame [{}] [{}]",
                         info.width,
                         info.height);
            info.y = y.data();
            info.u = u.data();
            info.v = v.data();

            uint64_t currentPosMillis = av_q2d(
                                            format->streams[videoStream]->
                                            time_base)
                                        * pts * 1000;
            using namespace std::chrono_literals;
            using namespace std::chrono;

            milliseconds deadline = g_start_time + milliseconds(
                                        currentPosMillis);

            while ((duration_cast<milliseconds>(
                       (system_clock::now() - g_pause_time.load()).
                       time_since_epoch()))
                   <
                   deadline && !token.stop_requested()) {
                std::this_thread::sleep_for(10us); // 精细等待
            }
            {
                std::unique_lock<std::mutex> lock(g_mtx_pause);
                while (g_is_paused && !token.stop_requested()) {
                    g_cv_pause.wait(lock);
                }
            }

            if (token.stop_requested()) {
                break;
            }

            QMetaObject::invokeMethod(controller, "VideoFrameReady",
                                      Qt::DirectConnection,
                                      Q_ARG(VideoInfo, info));
        }
    }
}

void startAudioDecode(std::stop_token token, PlayerController *controller) {
    while (!token.stop_requested()) {
        AVPacket *packet{};

        if (!g_buffer_audio.pop(packet)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        std::vector<AVFrame *> frames;
        if (FFmpeg::sendPacket2(audioCodecContext, packet, frames).
            hasErr()) {
            spdlog::error("sendPacket2 error");
            continue;
        }
        while (!token.stop_requested() && !frames.empty()) {
            AVFrame *frame = frames.back();
            frames.pop_back();

            uint64_t pts = packet->pts;
            av_packet_free(&packet);
            if (FFmpeg::decodeAudio(g_swr, frame, audioCodecContext).

                hasErr()) {
                spdlog::error("decodeAudio error");
                continue;
            }

            uint64_t currentPosMillis = av_q2d(
                                            format->streams[audioStream]->
                                            time_base)
                                        * pts * 1000;
            using namespace std::chrono_literals;
            using namespace std::chrono;

            milliseconds deadline = g_start_time + milliseconds(
                                        currentPosMillis);

            while ((duration_cast<milliseconds>(
                       (system_clock::now() - g_pause_time.load()).
                       time_since_epoch()))
                   <
                   deadline && !token.stop_requested()) {
                std::this_thread::sleep_for(10us); // 精细等待
            }
            {
                std::unique_lock<std::mutex> lock(g_mtx_pause);
                while (g_is_paused && !token.stop_requested()) {
                    g_cv_pause.wait(lock);
                }
            }

            if (token.stop_requested()) {
                break;
            }
        }
    }
}
}

PlayerController::PlayerController(PlayerWidget *rendererBridge) {
    qRegisterMetaType<VideoInfo>("VideoInfo");

    connect(this, &PlayerController::VideoFrameReady, rendererBridge,
            &PlayerWidget::onFrameChanged);
}

PlayerController::~PlayerController() {
    g_cv_pause.notify_all();
    // if (g_swr) {
    //     delete g_swr;
    // }
    // if (format) {
    //     avformat_close_input(&format);
    //     format = nullptr;
    // }
    // if (videoCodecContext) {
    //     avcodec_close(videoCodecContext);
    //     videoCodecContext = nullptr;
    // }
    // if (audioCodecContext) {
    //     avcodec_close(audioCodecContext);
    //     audioCodecContext = nullptr;
    // }
}

void PlayerController::Open(const std::string &url) {
    mUrl = url;
    if (mState == PlayerState::Idle) {
        mState = PlayerState::Ready;
        spdlog::info("open url:{}", url);
        FFmpeg::openFile(format, url, audioStream, videoStream);
        FFmpeg::openCodec(videoCodecContext, videoStream, format);
        FFmpeg::openCodec(audioCodecContext, audioStream, format);

        emit StateChanged(mState);
    }
}

namespace {
std::chrono::time_point<std::chrono::system_clock> g_last_pause_point;
}

void PlayerController::Play() {
    if (mState == PlayerState::Playing) {
        mState = PlayerState::Paused;
        g_last_pause_point = std::chrono::system_clock::now();
        g_is_paused = true;
        emit StateChanged(mState);
        return;
    }
    if (mState == PlayerState::Paused) {
        mState = PlayerState::Playing;
        spdlog::info("start decode thread");

        // g_pause_time += std::chrono::duration_cast<std::chrono::milliseconds>(
        //   std::chrono::system_clock::now() - g_last_pause_point);
        auto now = std::chrono::system_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - g_last_pause_point);

        // 自旋 CAS（compare-exchange）实现原子加法
        std::chrono::milliseconds current = g_pause_time.load();
        while (!g_pause_time.compare_exchange_weak(current, current + delta)) {
            // current 被修改为最新值，继续尝试
        }
        {
            std::unique_lock<std::mutex> lock(g_mtx_pause);

            g_is_paused = false;
            g_cv_pause.notify_all();
        }
        // g_start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        //     std::chrono::system_clock::now().time_since_epoch());
        // mReadTask = std::jthread(startReadPacket);
        // mDecodeTask = std::jthread(startDecodeThread, this);
        emit StateChanged(mState);
        return;
    }
    if (mState == PlayerState::Ready) {
        mState = PlayerState::Playing;
        spdlog::info("start decode thread");

        g_start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        mReadTask = std::jthread(startReadPacket);
        mVideoTask = std::jthread(startVideoDecode, this);
        mAudioTask = std::jthread(startAudioDecode, this);
        emit StateChanged(mState);
    }
}


void PlayerController::Quit() {
    if (mState == PlayerState::Playing) {
        mState = PlayerState::Ready;
        emit StateChanged(mState);
    }
}

void PlayerController::SeekTo(int64_t ms) {
    if (mState == PlayerState::Playing) {
        mState = PlayerState::Seeking;
        emit StateChanged(mState);
        mState = PlayerState::Playing;
        emit StateChanged(mState);
    }

    if (mState == PlayerState::Paused) {
        mState = PlayerState::Seeking;
        emit StateChanged(mState);
        mState = PlayerState::Paused;
    }
}

void PlayerController::Tick() {}

#pragma once
#include "JitterBuffer.h"
#include "SwrResample.h"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <future>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

class MyQtMainWindow;

#define AVJitterBuffer JitterBuffer<AVPacket *>

class FileDecode {
public:
    FileDecode()
        : audio_packet_buffer(nullptr),
          video_packet_buffer(nullptr),
          swrResample(nullptr) {}

    ~FileDecode();
    int AVOpenFile(std::string filename);
    int OpenAudioDecode();
    int OpenVideoDecode();
    int StartRead(std::string);
    int InnerStartRead();

    void SetPosition(int position);

    void Close();

    void SetMyWindow(MyQtMainWindow *mywindow);

    void PauseRender();
    void ResumeRender();

    void PauseRead();
    void ResumeRead();

    void ClearJitterBuf();

    static void AVPacketFreeBind(AVPacket *pkt) {
        av_packet_unref(pkt);
        // auto r = std::async(std::launch::async, [pkt]mutable {
        //     av_packet_free(&pkt);
        // });

    }

    std::string getCurrentTimeAsString() {
        auto now = std::chrono::system_clock::now();

        auto now_c = std::chrono::system_clock::to_time_t(now);

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

        std::stringstream ss;
        ss << " [" << std::put_time(std::localtime(&now_c), "%T") << '.'
           << std::setfill('0') << std::setw(3) << ms.count() << "]:";

        return ss.str();
    }

    int64_t GetPlayingMs();
    int64_t GetFileLenMs();
    auto const& getswrResample()const {
        return swrResample;
    }
private:
    int64_t player_start_time_ms = 0;

    inline void StartSysClockMs() {
        if (player_start_time_ms == 0) {
            player_start_time_ms = now_ms();
        }
    }

    inline int64_t now_ms() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch())
            .count();
    }

    inline int64_t GetSysClockMs() {
        if (player_start_time_ms != 0) {
            return now_ms() - player_start_time_ms;
        }
        return 0;
    }

    int64_t video_stream_time = 0;
    int64_t audio_stream_time = 0;

    inline void ClockReset(int64_t seek_time) {
        audio_stream_time = seek_time;
        curr_playing_ms = seek_time;
        video_stream_time = seek_time;

        player_start_time_ms = now_ms() - seek_time;
    }

    int VideoDecodeFun();
    int AudioDecodeFun();
    void RunFFmpeg(std::string url);

    int DecodeAudio(AVPacket *originalPacket);
    int DecodeVideo(AVPacket *originalPacket);
    int ResampleAudio(AVFrame *frame);

    bool is_planar_yuv(enum AVPixelFormat pix_fmt);


private:
    AVFormatContext *formatCtx = NULL;
    AVCodecContext *audioCodecCtx = NULL;
    AVCodecContext *videoCodecCtx = NULL;

#ifdef WRITE_DECODED_PCM_FILE
    FILE *outdecodedfile = NULL;
#endif

#ifdef WRITE_DECODED_YUV_FILE
    FILE *outdecodedYUVfile = NULL;
#endif

    int audioStream;
    int videoStream;

    std::unique_ptr<SwrResample> swrResample;

    MyQtMainWindow *qtWin = NULL;

    bool videoDecodeThreadFlag = true;
    std::thread *videoDecodeThread = nullptr;

    bool audioDecodeThreadFlag = true;
    std::thread *audioDecodeThread = nullptr;

    bool read_frame_flag = true;
    std::thread *player_thread_ = nullptr;

    std::unique_ptr<AVJitterBuffer> audio_packet_buffer;
    std::unique_ptr<AVJitterBuffer> video_packet_buffer;

    int64_t audio_frame_dur = 0;
    int64_t video_frame_dur = 0;

    int64_t file_len_ms;
    int64_t curr_playing_ms;

    std::atomic_bool pauseFlag = false;

    int64_t position_ms = -1;

    std::mutex read_mutex_;

    bool pause_read_flag = true;
};

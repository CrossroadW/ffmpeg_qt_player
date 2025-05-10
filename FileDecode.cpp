#include "FileDecode.h"
#include "MyQtMainWindow.h"
#include <cstring>

extern "C" {
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

FileDecode::~FileDecode() {
    if (qtWin) {
        qtWin = NULL;
    }
}

int64_t FileDecode::GetPlayingMs() {
    return curr_playing_ms;
}

int64_t FileDecode::GetFileLenMs() {
    return file_len_ms;
}

void FileDecode::SetPosition(int position) {
    position_ms = file_len_ms * position / 1000;
}

int FileDecode::AVOpenFile(std::string filename) {
#ifdef WRITE_DECODED_PCM_FILE
    outdecodedfile = fopen("decode.pcm", "wb");
    if (!outdecodedfile) {
        std::cout << "open out put file failed";
    }
#endif

#ifdef WRITE_DECODED_YUV_FILE
    outdecodedYUVfile = fopen("decoded_video.yuv", "wb");
    if (!outdecodedYUVfile) {
        std::cout << "open out put YUV file failed";
    }
#endif

    int openInputResult =
        avformat_open_input(&formatCtx, filename.c_str(), NULL, NULL);
    if (openInputResult != 0) {
        std::cout << "open input failed" << std::endl;
        return -1;
    }

    if (avformat_find_stream_info(formatCtx, NULL) < 0) {
        std::cout << "find stram info faild" << std::endl;
        return -1;
    }

    av_dump_format(formatCtx, 0, filename.c_str(), 0);

    audioStream =
        av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audioStream < 0) {
        std::cout << "av find best audio stream failed" << std::endl;
        return -1;
    }

    videoStream =
        av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (videoStream < 0) {
        std::cout << "av find best video stream failed" << std::endl;
        return -1;
    }

    return 0;
}

int FileDecode::OpenAudioDecode() {
    AVStream *stream = formatCtx->streams[audioStream];
    AVCodec const *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "cannot find audio codec id: "
            << stream->codecpar->codec_id << std::endl;
        return -1;
    }

    audioCodecCtx = avcodec_alloc_context3(codec);
    if (!audioCodecCtx) {
        std::cerr << "Failed to allocate audio codec context" << std::endl;
        return -1;
    }

    if (avcodec_parameters_to_context(audioCodecCtx, stream->codecpar) < 0) {
        std::cerr << "Failed to copy audio codec parameters to context"
            << std::endl;
        return -1;
    }

    AVDictionary *dict = nullptr;

    if (avcodec_open2(audioCodecCtx, codec, &dict) < 0) {
        std::cerr << "open audio decode failed" << std::endl;
        return -1;
    }
    AVRational pts_base = stream->time_base;
    file_len_ms = stream->duration * av_q2d(pts_base) * 1000;

    return 0;
}

int FileDecode::OpenVideoDecode() {
    AVStream *stream = formatCtx->streams[videoStream];
    AVCodec const *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::cout << "cannot find video codec id: "
            << stream->codecpar->codec_id << std::endl;
        return -1;
    }

    videoCodecCtx = avcodec_alloc_context3(codec);
    if (!videoCodecCtx) {
        std::cerr << "Failed to allocate video codec context" << std::endl;
        return -1;
    }

    if (avcodec_parameters_to_context(videoCodecCtx, stream->codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters to context" << std::endl;
        return -1;
    }

    if (avcodec_open2(videoCodecCtx, codec, nullptr) < 0) {
        std::cerr << "Failed to open video codec" << std::endl;
        return -1;
    }

    int codec_width = videoCodecCtx->coded_width;
    qtWin->initData(videoCodecCtx->width, videoCodecCtx->height, codec_width);

    return 0;
}

int FileDecode::StartRead(std::string fildName) {
    read_frame_flag = true;
    player_thread_ = new std::thread(&FileDecode::RunFFmpeg, this, fildName);

    return 0;
}

int FileDecode::InnerStartRead() {
    audio_packet_buffer = std::make_unique<AVJitterBuffer>(10);
    video_packet_buffer = std::make_unique<AVJitterBuffer>(10);

    videoDecodeThreadFlag = true;
    videoDecodeThread = new std::thread(&FileDecode::VideoDecodeFun, this);

    audioDecodeThreadFlag = true;
    audioDecodeThread = new std::thread(&FileDecode::AudioDecodeFun, this);

    AVRational video_pts_base = formatCtx->streams[videoStream]->time_base;
    AVRational audio_pts_base = formatCtx->streams[audioStream]->time_base;
    int64_t audio_pts_begin = formatCtx->streams[audioStream]->start_time;
    int64_t video_pts_begin = formatCtx->streams[videoStream]->start_time;

    StartSysClockMs();

    int result = 0;
    read_frame_flag = true;
    do {
        std::unique_lock<std::mutex> lock(read_mutex_);
        if (!pause_read_flag) {
            usleep(2000);
            lock.unlock();
            continue;
        }

        if (position_ms != -1) {
            int64_t base_position =
                (double)position_ms / (av_q2d(audio_pts_base) * 1000);
            int seek_flag = (position_ms <= curr_playing_ms)
                                ? AVSEEK_FLAG_BACKWARD
                                : AVSEEK_FLAG_FRAME;

            av_seek_frame(formatCtx, audioStream, base_position, seek_flag);

            ClearJitterBuf();
            ClockReset(position_ms);
            position_ms = -1;

        }

        AVPacket *avpkt = av_packet_alloc();

        int expand_size = 0;

        int read_ret = av_read_frame(formatCtx, avpkt);
        if (read_ret < 0) {
            char errmsg[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errmsg, AV_ERROR_MAX_STRING_SIZE, read_ret);
            if (read_ret == AVERROR_EOF) {
                read_frame_flag = false;
                av_packet_unref(avpkt);
                result = -1;
                break;
            } else {
                continue;
            }
        }

        if (avpkt->stream_index == audioStream) {
            int64_t pkt_dur = avpkt->duration * av_q2d(audio_pts_base) * 1000;
            if (pkt_dur != 0 && pkt_dur != audio_frame_dur) {
                expand_size = 1000 / pkt_dur;
                audio_frame_dur = pkt_dur;
            }
            int64_t read_time =
                (avpkt->pts - audio_pts_begin) * av_q2d(audio_pts_base) * 1000;
            // std::cout << "push read audio frame ms: " << read_time << ":"
            //     << audio_packet_buffer->size() << std::endl;;
            audio_packet_buffer->Push(avpkt, expand_size);
            // std::cout << "push read audio frame end ms: " << read_time << ":"
            //     << audio_packet_buffer->size() << std::endl;;
        } else if (avpkt->stream_index == videoStream) {
            int64_t pkt_dur = avpkt->duration * av_q2d(video_pts_base) * 1000;
            if (pkt_dur != 0 && pkt_dur != video_frame_dur) {
                expand_size = 1000 / pkt_dur;
                audio_frame_dur = pkt_dur;
            }
            int64_t read_time =
                (avpkt->pts - video_pts_begin) * av_q2d(video_pts_base) * 1000;
            // std::cout << "push read video frame ms: " << read_time << ":"
            //     << video_packet_buffer->size() << std::endl;;
            video_packet_buffer->Push(avpkt, expand_size);
            // std::cout << "push read video frame end ms: " << read_time <<
            //     std::endl;;
        }
    } while (read_frame_flag);

    return result;
}

int FileDecode::VideoDecodeFun() {
    AVRational pts_base = formatCtx->streams[videoStream]->time_base;
    int64_t pts_begin = formatCtx->streams[videoStream]->start_time;

    do {
        if (pauseFlag) {
            usleep(2000);
            continue;
        }

        if (video_stream_time > GetSysClockMs()) {
            usleep(2000);
            continue;
        }
        // std::cout << "video pop before: " << std::endl;
        AVPacket *avpkt = video_packet_buffer->Pop(false);
        if (avpkt == nullptr) {
            if (!read_frame_flag) {
                break;
            }

            usleep(2000);

            continue;
        }

        if (avpkt->stream_index == videoStream) {
            DecodeVideo(avpkt);

            video_stream_time =
                (avpkt->pts - pts_begin) * av_q2d(pts_base) * 1000;

            av_packet_unref(avpkt);
        }
    } while (videoDecodeThreadFlag);

    return 0;
}

int FileDecode::AudioDecodeFun() {
    AVRational pts_base = formatCtx->streams[audioStream]->time_base;
    int64_t pts_begin = formatCtx->streams[audioStream]->start_time;

    do {
        if (pauseFlag) {
            usleep(2000);

            continue;
        }
        if (audio_stream_time > GetSysClockMs()) {
            usleep(2000);

            continue;
        }

        AVPacket *avpkt = audio_packet_buffer->Pop(false);
        if (avpkt == nullptr) {
            if (!read_frame_flag) {
                break;
            }
            usleep(2000);

            continue;
        }

        if (avpkt->stream_index == audioStream) {
            DecodeAudio(avpkt);

            audio_stream_time =
                (avpkt->pts - pts_begin) * av_q2d(pts_base) * 1000;
            curr_playing_ms = audio_stream_time;

            av_packet_unref(avpkt);
        }
    } while (audioDecodeThreadFlag);

    return 0;
}

void FileDecode::Close() {
    read_frame_flag = false;
    if (player_thread_ && player_thread_->joinable()) {
        player_thread_->join();
        player_thread_ = nullptr;
    }

    videoDecodeThreadFlag = false;
    if (videoDecodeThread && videoDecodeThread->joinable()) {
        videoDecodeThread->join();
        videoDecodeThread = nullptr;
    }

    audioDecodeThreadFlag = false;
    if (audioDecodeThread && audioDecodeThread->joinable()) {
        audioDecodeThread->join();
        audioDecodeThread = nullptr;
    }

    if (swrResample) {
        swrResample->Close();
    }

#ifdef WRITE_DECODED_PCM_FILE
    if (outdecodedfile) {
        fclose(outdecodedfile);
        outdecodedfile = NULL;
    }
#endif

#ifdef WRITE_DECODED_YUV_FILE
    if (outdecodedYUVfile) {
        fclose(outdecodedYUVfile);
        outdecodedYUVfile = NULL;
    }
#endif

    if (audioCodecCtx) {
        avcodec_close(audioCodecCtx);

        audioCodecCtx = nullptr;
    }
    if (videoCodecCtx) {
        avcodec_close(videoCodecCtx);

        videoCodecCtx = nullptr;
    }

    if (formatCtx) {
        avformat_close_input(&formatCtx);
        formatCtx = nullptr;
    }

    ClearJitterBuf();

    qtWin = nullptr;
}

void FileDecode::SetMyWindow(MyQtMainWindow *mywindow) {
    this->qtWin = mywindow;
}

void FileDecode::PauseRender() {
    pauseFlag = true;
}

void FileDecode::ResumeRender() {
    pauseFlag = false;
}

void FileDecode::PauseRead() {
    std::unique_lock<std::mutex> lock(read_mutex_);
    pause_read_flag = false;
}

void FileDecode::ResumeRead() {
    std::unique_lock<std::mutex> lock(read_mutex_);
    pause_read_flag = true;
}

int FileDecode::DecodeAudio(AVPacket *originalPacket) {
    int ret = avcodec_send_packet(audioCodecCtx, originalPacket);
    if (ret < 0) {
        return -1;
    }
    AVFrame *frame = av_frame_alloc();
    ret = avcodec_receive_frame(audioCodecCtx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return -2;
    } else if (ret < 0) {
        std::cout << "error decoding";
        return -1;
    }

    int data_size = av_get_bytes_per_sample(audioCodecCtx->sample_fmt);
    if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        std::cout << "Failed to calculate data size\n";
        return -1;
    }

#ifdef WRITE_DECODED_PCM_FILE
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < audioCodecCtx->channels; ch++) {
            fwrite(frame->data[ch] + data_size * i, 1, data_size,
                   outdecodedfile);
        }
    }
#endif

    ResampleAudio(frame);

    av_frame_free(&frame);
    return 0;
}

int FileDecode::ResampleAudio(AVFrame *frame) {
    if (!swrResample) {
        swrResample = std::make_unique<SwrResample>();

        int src_ch_layout = audioCodecCtx->channel_layout;
        int src_rate = audioCodecCtx->sample_rate;
        enum AVSampleFormat src_sample_fmt = audioCodecCtx->sample_fmt;

        int dst_ch_layout = AV_CH_LAYOUT_STEREO;
        int dst_rate = 44100;
        enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;

        int src_nb_samples = frame->nb_samples;

        swrResample->Init(src_ch_layout, dst_ch_layout, src_rate, dst_rate,
                          src_sample_fmt, dst_sample_fmt, src_nb_samples);
    }

    swrResample->WriteInput(frame);

    int res = swrResample->SwrConvert();

    return res;
}


int FileDecode::DecodeVideo(AVPacket *originalPacket) {
    int ret = avcodec_send_packet(videoCodecCtx, originalPacket);
    if (ret < 0) {
        return -1;
    }

    AVFrame *frame = av_frame_alloc();
    ret = avcodec_receive_frame(videoCodecCtx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        av_frame_free(&frame);
        return -2;
    } else if (ret < 0) {
        av_frame_free(&frame);
        return -1;
    }
#if 0
    // 判断帧的格式，只处理 YUV420P, YUV422P 和 YUV444P 格式
    if (frame->format != AV_PIX_FMT_YUV420P &&
        frame->format != AV_PIX_FMT_YUV422P &&
        frame->format != AV_PIX_FMT_YUV444P) {
        std::cout << "Unsupported format: "
            << av_get_pix_fmt_name((AVPixelFormat)frame->format) << std::endl;
        av_frame_free(&frame);
        return -3;
    }

    // 计算 Y, U, V 分量的大小
    int y_size = frame->width * frame->height;
    int uv_size = y_size / 4;

    // 使用 std::vector 替代原始数组，避免手动内存管理
    std::vector<uint8_t> aligned_y(y_size);
    std::vector<uint8_t> aligned_u(uv_size);
    std::vector<uint8_t> aligned_v(uv_size);

    // 判断是否存在对齐
    bool needs_alignment = frame->linesize[0] != frame->width ||
                           frame->linesize[1] != frame->width / 2 ||
                           frame->linesize[2] != frame->width / 2;

    if (needs_alignment) {
        // 拷贝 Y 分量
        for (int i = 0; i < frame->height; ++i) {
            std::memcpy(
                aligned_y.data() + i * frame->width,
                frame->data[0] + i * frame->linesize[0],
                frame->width
                );
        }

        // 根据格式处理 U 和 V 分量
        if (frame->format == AV_PIX_FMT_YUV422P) {
            // YUV 422 转 YUV 420
            for (int i = 0; i < frame->height / 2; ++i) {
                std::memcpy(
                    aligned_u.data() + i * frame->width / 2,
                    frame->data[1] + i * frame->linesize[1],
                    frame->width / 2
                    );
                std::memcpy(
                    aligned_v.data() + i * frame->width / 2,
                    frame->data[2] + i * frame->linesize[2],
                    frame->width / 2
                    );
            }
        } else if (frame->format == AV_PIX_FMT_YUV444P) {
            // YUV 444 转 YUV 420
            for (int i = 0; i < frame->height / 2; ++i) {
                std::memcpy(
                    aligned_u.data() + i * frame->width / 2,
                    frame->data[1] + i * frame->linesize[1],
                    frame->width / 2
                    );
                std::memcpy(
                    aligned_v.data() + i * frame->width / 2,
                    frame->data[2] + i * frame->linesize[2],
                    frame->width / 2
                    );
            }
        } else if (frame->format == AV_PIX_FMT_YUV420P) {
            // 对于 YUV 420P 格式，去除填充并拷贝数据
            for (int i = 0; i < frame->height; ++i) {
                std::memcpy(
                    aligned_y.data() + i * frame->width,
                    frame->data[0] + i * frame->linesize[0],
                    frame->width
                    );
            }
            for (int i = 0; i < frame->height / 2; ++i) {
                std::memcpy(
                    aligned_u.data() + i * frame->width / 2,
                    frame->data[1] + i * frame->linesize[1],
                    frame->width / 2
                    );
                std::memcpy(
                    aligned_v.data() + i * frame->width / 2,
                    frame->data[2] + i * frame->linesize[2],
                    frame->width / 2
                    );
            }
        }

        // 更新 YUV 数据
        qtWin->updateYuv(
            aligned_y.data(),
            aligned_u.data(),
            aligned_v.data()
            );
    } else {
        // 如果没有对齐问题，直接传递原始数据
        qtWin->updateYuv(
            frame->data[0],
            frame->data[1],
            frame->data[2]
            );
    }
#else
    if (frame->format != AV_PIX_FMT_YUV420P) {
        std::cout << "AV_PIX_FMT_YUV420P:"
            << av_get_pix_fmt_name((AVPixelFormat)frame->format) << std::endl;
        av_frame_free(&frame);
        return -3;
    }

    if (frame->linesize[0] != frame->width ||
        frame->linesize[1] != frame->width / 2) {
        int y_size = frame->width * frame->height;
        int uv_size = y_size / 4;

        std::vector<uint8_t> aligned_y(y_size);
        std::vector<uint8_t> aligned_u(uv_size);
        std::vector<uint8_t> aligned_v(uv_size);

        // 拷贝 Y 分量
        for (int i = 0; i < frame->height; ++i) {
            std::memcpy(
                aligned_y.data() + i * frame->width,
                frame->data[0] + i * frame->linesize[0],
                frame->width
                );
        }

        // 拷贝 U 和 V 分量
        for (int i = 0; i < frame->height / 2; ++i) {
            std::memcpy(
                aligned_u.data() + i * frame->width / 2,
                frame->data[1] + i * frame->linesize[1],
                frame->width / 2
                );
            std::memcpy(
                aligned_v.data() + i * frame->width / 2,
                frame->data[2] + i * frame->linesize[2],
                frame->width / 2
                );
        }

        qtWin->updateYuv(
            aligned_y.data(),
            aligned_u.data(),
            aligned_v.data()
            );
    } else {
        qtWin->updateYuv(
            frame->data[0],
            frame->data[1],
            frame->data[2]
            );
    }
#endif
#ifdef WRITE_DECODED_YUV_FILE

#endif

    av_frame_free(&frame);
    return 0;
}

bool FileDecode::is_planar_yuv(enum AVPixelFormat pix_fmt) {
    switch (pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P: return 1;
    default: return 0;
    }
}

void FileDecode::RunFFmpeg(std::string url) {
    int ret = AVOpenFile(url);
    if (ret != 0) {
        std::cout << "AVOpenFile Faild";
    }

    ret = OpenVideoDecode();
    if (ret != 0) {
        std::cout << "OpenVideoDecode Faild";
    }
    ret = OpenAudioDecode();
    if (ret != 0) {
        std::cout << "OpenAudioDecode Faild";
    }
    ret = InnerStartRead();
    if (ret == -1) {
        std::cout << "InnerStartRead failed,"
            << std::endl;

        std::ostringstream oss;
        oss << std::this_thread::get_id();
        qDebug() << oss.str().c_str();
        QMetaObject::invokeMethod(qtWin, &MyQtMainWindow::ClosePlayer);
    }
}

void FileDecode::ClearJitterBuf() {
    std::unique_ptr<AVJitterBuffer> old = std::move(audio_packet_buffer);
    std::unique_ptr<AVJitterBuffer> old2 = std::move(video_packet_buffer);
    std::thread([old= std::move(old),old2= std::move(old2)] {
        if (old) {
            qDebug() << "start complete clear buffer";
            std::function<void(AVPacket *pkt)> func =
                &FileDecode::AVPacketFreeBind;
            old->Clear(func);
            old2->Clear(func);
        }
    }).detach();
    audio_packet_buffer = std::make_unique<AVJitterBuffer>(10);
    video_packet_buffer = std::make_unique<AVJitterBuffer>(10);
}

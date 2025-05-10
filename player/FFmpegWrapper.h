#pragma once
#include <QAudioOutput>
#include <QIODevice>


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include <functional>
#include <source_location>
#include <spdlog/spdlog.h>
#include <vector>

class FFmpeg {
public:
    static void throwOnError(bool expected, int ret,
                             std::source_location loc =
                                 std::source_location::current()) {
        if (!expected) {
            char errorBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errorBuf, sizeof(errorBuf));
            spdlog::error("Error at {}:{} {}", loc.file_name(), loc.line(),
                          errorBuf);
            throw std::runtime_error(errorBuf);
        }
    }

    struct HasError {
        bool hasError{};

        operator bool() const {
            return hasError;
        }

        bool hasErr() const {
            return hasError;
        }
    };

    static constexpr HasError NoError = {false};
    static constexpr HasError Error = {true};

    static HasError warnOnError(bool expected, int ret,
                                std::source_location loc =
                                    std::source_location::current()) {
        if (!expected) {
            char errorBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errorBuf, sizeof(errorBuf));
            spdlog::warn("Non-critical error at {}:{} {}", loc.file_name(),
                         loc.line(), errorBuf);
            return Error;
        }
        return NoError;
    }

    static void openFile(AVFormatContext *&formatCtx,
                         std::string const &filename,
                         int &audioStream, int &videoStream) {
        int ret =
            avformat_open_input(&formatCtx, filename.c_str(), NULL, NULL);
        throwOnError(ret == 0, ret);
        ret = avformat_find_stream_info(formatCtx, NULL);
        throwOnError(ret >= 0, ret);

        // av_dump_format(formatCtx, 0, filename.c_str(), 0);

        audioStream =
            av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
        warnOnError(audioStream >= 0, audioStream);

        videoStream =
            av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        warnOnError(videoStream >= 0, videoStream);
    }

    static void openCodec(AVCodecContext *&codecCtx, int streamIndex,
                          AVFormatContext const *formatCtx) {
        AVStream *stream = formatCtx->streams[streamIndex];
        AVCodec const *codec = avcodec_find_decoder(stream->codecpar->codec_id);

        codecCtx = avcodec_alloc_context3(codec);

        avcodec_parameters_to_context(codecCtx, stream->codecpar);

        avcodec_open2(codecCtx, codec, nullptr);
    }

    static HasError readPaket(AVFormatContext *formatCtx, AVPacket *&packet) {
        packet = av_packet_alloc();

        int read_ret = av_read_frame(formatCtx, packet);
        if (warnOnError(read_ret == 0, read_ret)) {
            return Error;
        }

        return NoError;
    }

    static HasError sendPacket(AVCodecContext *videoCodecCtx,
                               AVPacket *&originalPacket,
                               AVFrame *&frame) {
        int ret = avcodec_send_packet(videoCodecCtx, originalPacket);
        if (warnOnError(ret == 0, ret).hasErr()) {
            av_packet_free(&originalPacket);
            return Error;
        }
        // av_packet_free(&originalPacket);
        frame = av_frame_alloc();
        ret = avcodec_receive_frame(videoCodecCtx, frame);
        if (warnOnError(ret == 0, ret).hasErr()) {
            return Error;
        }
        return NoError;
    }

    static HasError sendPacket2(AVCodecContext *codecCtx,
                                AVPacket *&originalPacket,
                                std::vector<AVFrame *> &frames) {
        int ret = avcodec_send_packet(codecCtx, originalPacket);
        if (warnOnError(ret == 0, ret).hasErr()) {
            av_packet_free(&originalPacket);
            return Error;
        }
        // av_packet_free(&originalPacket);
        while (true) {
            AVFrame *frame = av_frame_alloc();
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_frame_free(&frame); // 这些情况 frame 是无效的
                break;
            }
            if (ret < 0) {
                av_frame_free(&frame);
                warnOnError(false, ret); // 打印错误码
                return Error;
            }
            frames.push_back(frame);
        }
        return NoError;
    }

    static HasError decodeVideo(AVFrame *frame, std::vector<uint8_t> &aligned_y,
                                std::vector<uint8_t> &aligned_u,
                                std::vector<uint8_t> &aligned_v) {
        if (frame->format != AV_PIX_FMT_YUV420P) {
            return Error;
        }
        spdlog::info("decodeVideo width:{} height:{}", frame->width,
                     frame->height);
        int y_size = frame->width * frame->height;
        int uv_size = y_size / 4;
        aligned_y = std::vector<uint8_t>(y_size);
        aligned_u = std::vector<uint8_t>(uv_size);
        aligned_v = std::vector<uint8_t>(uv_size);
        if (frame->linesize[0] == frame->width && frame->linesize[1] == frame->
            width / 2) {
            std::memcpy(
                aligned_y.data(),
                frame->data[0],
                y_size
                );

            std::memcpy(
                aligned_u.data(),
                frame->data[1],
                uv_size
                );

            std::memcpy(
                aligned_v.data(),
                frame->data[2],
                uv_size
                );
        } else {
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
        }

        return NoError;
    }


    // struct SwrResample {
    //     struct AudioPlayer {
    //         ~AudioPlayer() {
    //             if (audioOutput) {
    //                 audioOutput->stop();
    //             }
    //         }
    //
    //         void SetFormat(int rate, int sample_size,
    //                        int nch) {
    //             QAudioFormat format;
    //             format.setSampleRate(rate);
    //             format.setChannelCount(nch);
    //             format.setSampleSize(sample_size);
    //             format.setCodec("audio/pcm");
    //             format.setByteOrder(QAudioFormat::LittleEndian);
    //             format.setSampleType(QAudioFormat::SignedInt);
    //
    //             audioOutput = new QAudioOutput(format);
    //             audioOutput->setVolume(1.0);
    //             outputDevice = audioOutput->start();
    //         }
    //
    //         void writeData(const char *data, qint64 len) {
    //             if (outputDevice)
    //                 outputDevice->write(data, len);
    //         }
    //
    //     private:
    //         QIODevice *outputDevice{};
    //         QAudioOutput *audioOutput{};
    //     };
    //
    //     int Init(int64_t src_ch_layout, int64_t dst_ch_layout,
    //              int src_rate, int dst_rate,
    //              AVSampleFormat src_sample_fmt,
    //              AVSampleFormat dst_sample_fmt,
    //              int src_nb_samples) {
    //         src_sample_fmt_ = src_sample_fmt;
    //         dst_sample_fmt_ = dst_sample_fmt;
    //
    //         int ret;
    //         /* create resampler context */
    //         swr_ctx = swr_alloc();
    //         if (!swr_ctx) {
    //             spdlog::error("Could not allocate resampler context");
    //             return AVERROR(ENOMEM);
    //         }
    //
    //         if (src_sample_fmt == AV_SAMPLE_FMT_NONE ||
    //             dst_sample_fmt == AV_SAMPLE_FMT_NONE) {
    //             spdlog::error("Invalid sample format!");
    //             return -1;
    //         }
    //
    //         av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
    //         av_opt_set_int(swr_ctx, "in_sample_rate", src_rate, 0);
    //         av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);
    //
    //         av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
    //         av_opt_set_int(swr_ctx, "out_sample_rate", dst_rate, 0);
    //         av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);
    //
    //         if ((ret = swr_init(swr_ctx)) < 0) {
    //             spdlog::error("Failed to initialize the resampling context");
    //             return -1;
    //         }
    //
    //         src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout);
    //
    //         ret = av_samples_alloc_array_and_samples(&src_data_, &src_linesize,
    //             src_nb_channels, src_nb_samples,
    //             src_sample_fmt, 0);
    //         if (ret < 0) {
    //             spdlog::error("Could not allocate source samples");
    //             return -1;
    //         }
    //         src_nb_samples_ = src_nb_samples;
    //
    //         dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);
    //
    //         ret = av_samples_alloc_array_and_samples(&dst_data_, &dst_linesize,
    //             dst_nb_channels, dst_nb_samples_,
    //             dst_sample_fmt, 0);
    //         if (ret < 0) {
    //             spdlog::error("Could not allocate destination samples");
    //             return -1;
    //         }
    //
    //         int data_size = av_get_bytes_per_sample(dst_sample_fmt_);
    //         audioPlayer.SetFormat(dst_rate, data_size * 8,
    //                               dst_nb_channels);
    //     }
    //
    //     int WriteInput(AVFrame *frame) {
    //         int planar = av_sample_fmt_is_planar(src_sample_fmt_);
    //         int data_size = av_get_bytes_per_sample(src_sample_fmt_);
    //         if (planar) {
    //             for (int ch = 0; ch < src_nb_channels; ch++) {
    //                 memcpy(src_data_[ch], frame->data[ch],
    //                        data_size * frame->nb_samples);
    //             }
    //         } else {
    //             for (int i = 0; i < frame->nb_samples; i++) {
    //                 for (int ch = 0; ch < src_nb_channels; ch++) {
    //                     memcpy(src_data_[0], frame->data[ch] + data_size * i,
    //                            data_size);
    //                 }
    //             }
    //         }
    //     }
    //
    //     int SwrConvert() {
    //         int ret = swr_convert(swr_ctx, dst_data_, dst_nb_samples_,
    //                               (const uint8_t **)src_data_, src_nb_samples_);
    //         if (ret < 0) {
    //             spdlog::error("Error while converting");
    //             exit(1);
    //         }
    //
    //         int dst_bufsize = av_samples_get_buffer_size(
    //             &dst_linesize, dst_nb_channels,
    //             ret, dst_sample_fmt_, 1);
    //
    //         return dst_bufsize;
    //     }
    //
    //     void Close() {
    //         if (src_data_)
    //             av_freep(&src_data_[0]);
    //
    //         av_freep(&src_data_);
    //
    //         if (dst_data_)
    //             av_freep(&dst_data_[0]);
    //
    //         av_freep(&dst_data_);
    //         swr_free(&swr_ctx);
    //     }
    //
    //     AudioPlayer audioPlayer;
    //
    // private:
    //     SwrContext *swr_ctx;
    //
    //     uint8_t **src_data_;
    //     uint8_t **dst_data_;
    //
    //     int src_nb_channels, dst_nb_channels;
    //     int src_linesize, dst_linesize;
    //     int src_nb_samples_, dst_nb_samples_;
    //
    //     AVSampleFormat dst_sample_fmt_;
    //
    //     AVSampleFormat src_sample_fmt_;
    // };

    class AudioPlayer {
    public:
        AudioPlayer() : audioOutput(nullptr), outputDevice(nullptr) {}

        ~AudioPlayer() {
            Quit(); // 析构时也确保资源清理
        }

        void SetFormat(int dst_nb_samples, int rate, int sample_size, int nch) {
            Quit(); // 保证旧的 QAudioOutput 释放

            QAudioFormat format;
            format.setSampleRate(rate);
            format.setChannelCount(nch);
            format.setSampleSize(sample_size);
            format.setCodec("audio/pcm");
            format.setByteOrder(QAudioFormat::LittleEndian);
            format.setSampleType(QAudioFormat::SignedInt);

            audioOutput = new QAudioOutput(format);
            audioOutput->setVolume(1.0);
            outputDevice = audioOutput->start();
        }

        void writeData(const char *data, qint64 len) {
            if (outputDevice)
                outputDevice->write(data, len);
        }

        void Quit() {
            if (audioOutput) {
                audioOutput->stop();
            }
            delete audioOutput;
            audioOutput = nullptr;
            delete outputDevice;
            outputDevice = nullptr;
        }

    private:
        QIODevice *outputDevice{};
        QAudioOutput *audioOutput{};
    };

    class SwrResample {
    public:
        SwrResample() {}

        ~SwrResample() {
            Close();
        }

        int Init(int64_t src_ch_layout, int64_t dst_ch_layout,
                 int src_rate, int dst_rate,
                 enum AVSampleFormat src_sample_fmt,
                 enum AVSampleFormat dst_sample_fmt,
                 int src_nb_samples) {
#ifdef WRITE_RESAMPLE_PCM_FILE
    outdecodedswffile = fopen("decode_resample.pcm", "wb");
    if (!outdecodedswffile) {
        std::cout << "open out put swr file failed";
    }
#endif

            src_sample_fmt_ = src_sample_fmt;
            dst_sample_fmt_ = dst_sample_fmt;

            int ret;
            /* create resampler context */
            swr_ctx = swr_alloc();
            if (!swr_ctx) {
                spdlog::error("Could not allocate resampler context");
                // std::cout << "Could not allocate resampler context" << std::endl;
                return AVERROR(ENOMEM);
            }

            if (src_sample_fmt == AV_SAMPLE_FMT_NONE ||
                dst_sample_fmt == AV_SAMPLE_FMT_NONE) {
                spdlog::error("Invalid sample format!");
                // std::cerr << "Invalid sample format!" << std::endl;
                return -1;
            }

            /* set options */
            av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
            av_opt_set_int(swr_ctx, "in_sample_rate", src_rate, 0);
            av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);

            av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
            av_opt_set_int(swr_ctx, "out_sample_rate", dst_rate, 0);
            av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

            /* initialize the resampling context */
            if ((ret = swr_init(swr_ctx)) < 0) {
                // std::cout << "Failed to initialize the resampling context" << std::endl;
                return -1;
            }

            src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout);

            ret = av_samples_alloc_array_and_samples(&src_data_, &src_linesize,
                src_nb_channels, src_nb_samples,
                src_sample_fmt, 0);
            if (ret < 0) {
                // std::cout << "Could not allocate source samples\n" << std::endl;
                return -1;
            }
            src_nb_samples_ = src_nb_samples;

            int max_dst_nb_samples = dst_nb_samples_ =
                                     av_rescale_rnd(
                                         src_nb_samples, dst_rate, src_rate,
                                         AV_ROUND_UP);

            dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);

            ret = av_samples_alloc_array_and_samples(&dst_data_, &dst_linesize,
                dst_nb_channels, dst_nb_samples_,
                dst_sample_fmt, 0);

            if (ret < 0) {
                // std::cout << "Could not allocate destination samples" << std::endl;
                return -1;
            }

            int data_size = av_get_bytes_per_sample(dst_sample_fmt_);
            audioPlayer.SetFormat(dst_nb_samples_, dst_rate, data_size * 8,
                                  dst_nb_channels);
            return 0;
        }

        int WriteInput(AVFrame *frame) {
            int planar = av_sample_fmt_is_planar(src_sample_fmt_);
            int data_size = av_get_bytes_per_sample(src_sample_fmt_);
            if (planar) {
                for (int ch = 0; ch < src_nb_channels; ch++) {
                    memcpy(src_data_[ch], frame->data[ch],
                           data_size * frame->nb_samples);
                }
            } else {
                // memcpy(src_data_[0], frame->data[0],
                //        data_size * frame->nb_samples * src_nb_channels);

                for (int i = 0; i < frame->nb_samples; i++) {
                    for (int ch = 0; ch < src_nb_channels; ch++) {
                        memcpy(src_data_[0], frame->data[ch] + data_size * i,
                               data_size);
                    }
                }
            }
            return 0;
        }

        int SwrConvert() {
            int ret = swr_convert(swr_ctx, dst_data_, dst_nb_samples_,
                                  (uint8_t const **)src_data_, src_nb_samples_);
            if (ret < 0) {
                fprintf(stderr, "Error while converting\n");
                exit(1);
            }

            int dst_bufsize = av_samples_get_buffer_size(
                &dst_linesize, dst_nb_channels,
                ret, dst_sample_fmt_, 1);

            int planar = av_sample_fmt_is_planar(dst_sample_fmt_);
            if (planar) {
                int data_size = av_get_bytes_per_sample(dst_sample_fmt_);
            } else {
                audioPlayer.writeData((const char *)(dst_data_[0]),
                                      dst_bufsize);
            }

            return dst_bufsize;
        }

        void Close() {
            if (src_data_) {
                av_freep(&src_data_[0]);
            }

            av_freep(&src_data_);

            if (dst_data_) {
                av_freep(&dst_data_[0]);
            }

            av_freep(&dst_data_);

            swr_free(&swr_ctx);

            audioPlayer.Quit();
        }

        AudioPlayer audioPlayer;

    private:
        struct SwrContext *swr_ctx;

        uint8_t **src_data_;
        uint8_t **dst_data_;

        int src_nb_channels, dst_nb_channels;
        int src_linesize, dst_linesize;
        int src_nb_samples_, dst_nb_samples_;

        enum AVSampleFormat dst_sample_fmt_;

        enum AVSampleFormat src_sample_fmt_;

#ifdef WRITE_RESAMPLE_PCM_FILE
        FILE* outdecodedswffile;
#endif
    };


    static HasError decodeAudio(SwrResample *&swrResample, AVFrame *frame,
                                AVCodecContext *audioCodecCtx
        ) {
        if (!swrResample) {
            swrResample = new SwrResample{};

            int src_ch_layout = audioCodecCtx->channel_layout;
            int src_rate = audioCodecCtx->sample_rate;
            AVSampleFormat src_sample_fmt = audioCodecCtx->sample_fmt;

            int dst_ch_layout = AV_CH_LAYOUT_STEREO;
            int dst_rate = 44100;
            AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;

            int src_nb_samples = frame->nb_samples;

            swrResample->Init(src_ch_layout, dst_ch_layout, src_rate, dst_rate,
                              src_sample_fmt, dst_sample_fmt, src_nb_samples);
        }

        swrResample->WriteInput(frame);

        swrResample->SwrConvert();
        return NoError;
    }
};

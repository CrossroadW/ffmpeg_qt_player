#include <iostream>
#include <string>

#include <filesystem>
#include <unistd.h>


extern "C" {
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

void print_av_error(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    std::cerr << "FFmpeg error: " << errbuf << " (" << errnum << ")" <<
        std::endl;
}


using std::cout;
using std::endl;
using std::string;


#define WRITE_RESAMPLE_PCM_FILE

class SwrResample {
public:
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
#endif // WRITE_RESAMPLE_PCM_FILE

        src_sample_fmt_ = src_sample_fmt;
        dst_sample_fmt_ = dst_sample_fmt;

        int ret;
        /* create resampler context */
        swr_ctx = swr_alloc();
        if (!swr_ctx) {
            std::cout << "Could not allocate resampler context" << std::endl;
            ret = AVERROR(ENOMEM);
            return ret;
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
            std::cout << "Failed to initialize the resampling context" <<
                std::endl;
            return -1;
        }

        //配置输入的参数
        /*
        * src_nb_samples: 描述一整的采样个数 比如这里就是 1024
        * src_linesize: 描述一行采样字节长度
        *   当是planr 结构 LLLLLRRR 的时候 比如 一帧1024个采样，32为表示。那就是 1024*4 = 4096
        *   当是非palner 结构的时候 比如一帧1024采样 32位表示 双通道   1024*4*2 = 8196 要乘以通道
        * src_nb_channels : 可以根据布局获得音频的通道
        * ret 返回输入数据的长度 比如这里 1024 * 4 * 2 = 8196 (32bit，双声道，1024个采样)
        */
        src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout);

        ret = av_samples_alloc_array_and_samples(
            &src_data_, &src_linesize, src_nb_channels,
            src_nb_samples, src_sample_fmt, 0);
        if (ret < 0) {
            std::cout << "Could not allocate source samples\n" << std::endl;
            return -1;
        }
        src_nb_samples_ = src_nb_samples;

        //配置输出的参数
        int max_dst_nb_samples = dst_nb_samples_ =
                                 av_rescale_rnd(src_nb_samples, dst_rate,
                                                src_rate, AV_ROUND_UP);

        dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);

        ret = av_samples_alloc_array_and_samples(
            &dst_data_, &dst_linesize, dst_nb_channels,
            dst_nb_samples_, dst_sample_fmt, 0);
        if (ret < 0) {
            std::cout << "Could not allocate destination samples" << std::endl;
            return -1;
        }
        return 0;
    }

    int WriteInput(AVFrame *frame) {
        int planar = av_sample_fmt_is_planar(src_sample_fmt_);
        int data_size = av_get_bytes_per_sample(src_sample_fmt_);
        if (planar) {
            //src是planar类型的话，src_data里面数据是LLLLLLLRRRRR 结构，src_data_[0] 指向全部的L，src_data_[1] 指向全部R
            // src_data_ 里面其实一个长 uint8_t *buf，src_data_[0] 指向L开始的位置，src_data_[1]指向R的位置
            // linesize 是 b_samples * sample_size 就是比如 48000*4
            for (int ch = 0; ch < src_nb_channels; ch++) {
                memcpy(src_data_[ch], frame->data[ch],
                       data_size * frame->nb_samples);
            }
        } else {
            //src是非planar类型的话，src_data里面数据是LRLRLRLR 结构，src_data_[0] 指向全部数据 没有src_data[1]
            // linesize 是nb_samples * sample_size * nb_channels 比如 48000*4*2
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
                              (const uint8_t **)src_data_, src_nb_samples_);
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
            for (int i = 0; i < dst_nb_samples_; i++) {
                for (int ch = 0; ch < dst_nb_channels; ch++) {
                    fwrite(dst_data_[ch] + i * data_size, 1, data_size,
                           outdecodedswffile);
                }
            }
        } else {
            //非planr结构，dst_data_[0] 里面存在着全部数据
            fwrite(dst_data_[0], 1, dst_bufsize, outdecodedswffile);
        }

        return dst_bufsize;
    }

    void Close() {
#ifdef WRITE_RESAMPLE_PCM_FILE
        fclose(outdecodedswffile);
#endif

        if (src_data_)
            av_freep(&src_data_[0]);

        av_freep(&src_data_);

        if (dst_data_)
            av_freep(&dst_data_[0]);

        av_freep(&dst_data_);
        swr_free(&swr_ctx);
    }

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
    FILE *outdecodedswffile;
#endif
};


struct FileDecode {
    // AVCodec *codec = nullptr;
    AVFormatContext *formatCtx = nullptr;
    // AVCodecParameters *codecPara = nullptr;
    AVCodecContext *codecCtx = nullptr;

    FILE *outdecodedfile = nullptr;

    int audioStream = -1;
    SwrResample *swrResample = nullptr;

    ~FileDecode() {
        if (formatCtx) {
            avformat_close_input(&formatCtx);
        }
        if (codecCtx) {
            avcodec_free_context(&codecCtx);
        }
        if (outdecodedfile) {
            fclose(outdecodedfile);
        }
    }

    bool open(string filename) {
        outdecodedfile = fopen("decode.pcm", "wb");
        if (!outdecodedfile) {
            std::cout << "open out put file failed";
            return false;
        }
        int res = avformat_open_input(&formatCtx, filename.c_str(), nullptr,
                                      nullptr);

        if (res < 0) {
            print_av_error(res);
            return false;
        }
        res = avformat_find_stream_info(formatCtx, nullptr);
        if (res < 0) {
            print_av_error(res);
            return false;
        }
        av_dump_format(formatCtx, 0, filename.c_str(), 0);

        audioStream = av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1,
                                          nullptr, 0);
        // codecPara = formatCtx->streams[audioStream]->codecpar;;

        return true;
    }

    int OpenAudioDecode() {
        AVStream *audioStreamPtr = formatCtx->streams[audioStream];

        // 获取 codec parameters（新接口）
        AVCodecParameters *codecPara = audioStreamPtr->codecpar;

        // 查找解码器
        const AVCodec *codec = avcodec_find_decoder(codecPara->codec_id);
        if (!codec) {
            std::cerr << "Cannot find codec id: " << codecPara->codec_id <<
                std::endl;
            return -1;
        }

        // 分配解码上下文（新方式）
        codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx) {
            std::cerr << "Could not allocate AVCodecContext" << std::endl;
            return -1;
        }

        // 将参数从 codecpar 拷贝到 codecCtx
        if (avcodec_parameters_to_context(codecCtx, codecPara) < 0) {
            std::cerr << "Failed to copy codec parameters to decoder context" <<
                std::endl;
            return -1;
        }

        // 打开解码器
        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            std::cerr << "Failed to open decoder" << std::endl;
            return -1;
        }

        return 0;
    }

    bool Decode() {
        AVPacket *pkt = av_packet_alloc();
        if (!pkt) {
            std::cerr << "Could not allocate AVPacket" << std::endl;
            return false;
        }
        do {
            if (av_read_frame(formatCtx, pkt) < 0) {
                //没有读到数据，说明结束了
                return true;
            }
            if (pkt->stream_index == audioStream) {
                //std::cout << "read one audio frame" << std::endl;
                DecodeAudio(pkt);
            } else {
                //暂时不处理其他针
            }
            av_packet_unref(pkt);
        } while (pkt->data == nullptr);
        av_packet_unref(pkt);
        av_packet_free(&pkt);
        return true;
    }

    bool DecodeAudio(AVPacket *originalPacket) {
        int res = avcodec_send_packet(codecCtx, originalPacket);
        if (res == AVERROR_EOF || res == AVERROR(EAGAIN)) {
            //需要更多数据
            return true;
        }
        if (res < 0) {
            print_av_error(res);
            return false;
        }
        AVFrame *frame = av_frame_alloc();
        res = avcodec_receive_frame(codecCtx, frame);
        if (res == AVERROR_EOF || res == AVERROR(EAGAIN)) {
            print_av_error(res);
            av_frame_free(&frame);
            return true;
        }
        if (res < 0) {
            print_av_error(res);
            av_frame_free(&frame);
            return false;
        }
        int data_size = av_get_bytes_per_sample(codecCtx->sample_fmt);
        if (data_size < 0) {
            /* This should not occur, checking just for paranoia */
            print_av_error(res);
            av_frame_free(&frame);
            return -1;
        }
        // for (int i = 0; i < frame->nb_samples; i++)
        //     for (int ch = 0; ch < codecCtx->channels; ch++)
        //         fwrite(frame->data[ch] + data_size * i, 1, data_size,
        //                outdecodedfile);
        if (av_sample_fmt_is_planar(codecCtx->sample_fmt)) {
            for (int i = 0; i < frame->nb_samples; i++) {
                for (int ch = 0; ch < codecCtx->channels; ch++) {
                    fwrite(frame->data[ch] + i * data_size, 1, data_size,
                           outdecodedfile);
                }
            }
        } else {
            fwrite(frame->data[0], 1,
                   data_size * frame->nb_samples * codecCtx->channels,
                   outdecodedfile);
        }

        // 把AVFrame里面的数据拷贝到，预备的src_data里面
        if (swrResample == NULL) {
            swrResample = new SwrResample();

            //创建重采样信息
            int src_ch_layout = codecCtx->channel_layout;
            int src_rate = codecCtx->sample_rate;
            enum AVSampleFormat src_sample_fmt = codecCtx->sample_fmt;

            int dst_ch_layout = AV_CH_LAYOUT_STEREO;
            int dst_rate = 44100;
            enum AVSampleFormat dst_sample_fmt = codecCtx->sample_fmt;

            //aac编码一般是这个,实际这个值只能从解码后的数据里面获取，所有这个初始化过程可以放在解码出第一帧的时候
            int src_nb_samples = frame->nb_samples;

            swrResample->Init(src_ch_layout, dst_ch_layout,
                              src_rate, dst_rate,
                              src_sample_fmt, dst_sample_fmt,
                              src_nb_samples);
        }

        res = swrResample->WriteInput(frame);

        res = swrResample->SwrConvert();

        av_frame_free(&frame);
        av_frame_unref(frame);
        av_frame_free(&frame);
        return true;
    }
};


int main(int argc, char *argv[]) {
    cout << "workding dir: " << std::filesystem::current_path() << endl;
    FileDecode fileDecode;
    if (!fileDecode.open("/home/awe/Videos/oceans.mp4")) {
        return -1;
    }
    if (fileDecode.OpenAudioDecode() < 0) {
        return -1;
    }
    if (!fileDecode.Decode()) {
        return -1;
    }
    cout << "done" << endl;
}

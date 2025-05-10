#include <iostream>
#include <string>

#include <filesystem>
#include <unistd.h>

extern "C" {
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

struct FileDecode {
    // AVCodec *codec = nullptr;
    AVFormatContext *formatCtx = nullptr;
    // AVCodecParameters *codecPara = nullptr;
    AVCodecContext *codecCtx = nullptr;

    FILE *outdecodedfile = nullptr;

    int audioStream = -1;

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

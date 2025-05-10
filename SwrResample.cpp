#include "SwrResample.h"

int SwrResample::Init(int64_t src_ch_layout, int64_t dst_ch_layout,
                      int src_rate, int dst_rate,
                      enum AVSampleFormat src_sample_fmt,
                      enum AVSampleFormat dst_sample_fmt, int src_nb_samples) {
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
        std::cout << "Could not allocate resampler context" << std::endl;
        return AVERROR(ENOMEM);
    }

    if (src_sample_fmt == AV_SAMPLE_FMT_NONE ||
        dst_sample_fmt == AV_SAMPLE_FMT_NONE) {
        std::cerr << "Invalid sample format!" << std::endl;
        return -1;
    }

    if (!swr_ctx) {
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
        std::cout << "Failed to initialize the resampling context" << std::endl;
        return -1;
    }

    src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout);

    ret = av_samples_alloc_array_and_samples(&src_data_, &src_linesize,
                                             src_nb_channels, src_nb_samples,
                                             src_sample_fmt, 0);
    if (ret < 0) {
        std::cout << "Could not allocate source samples\n" << std::endl;
        return -1;
    }
    src_nb_samples_ = src_nb_samples;

    int max_dst_nb_samples = dst_nb_samples_ =
                             av_rescale_rnd(src_nb_samples, dst_rate, src_rate,
                                            AV_ROUND_UP);

    dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);

    ret = av_samples_alloc_array_and_samples(&dst_data_, &dst_linesize,
                                             dst_nb_channels, dst_nb_samples_,
                                             dst_sample_fmt, 0);
    if (ret < 0) {
        std::cout << "Could not allocate destination samples" << std::endl;
        return -1;
    }

    int data_size = av_get_bytes_per_sample(dst_sample_fmt_);
    audioPlayer.SetFormat(dst_nb_samples_, dst_rate, data_size * 8,
                          dst_nb_channels);
    return 0;
}

int SwrResample::WriteInput(AVFrame *frame) {
    int planar = av_sample_fmt_is_planar(src_sample_fmt_);
    int data_size = av_get_bytes_per_sample(src_sample_fmt_);
    if (planar) {
        for (int ch = 0; ch < src_nb_channels; ch++) {
            memcpy(src_data_[ch], frame->data[ch],
                   data_size * frame->nb_samples);
        }
    } else {
        memcpy(src_data_[0], frame->data[0], data_size * frame->nb_samples * src_nb_channels);

        // for (int i = 0; i < frame->nb_samples; i++) {
        //     for (int ch = 0; ch < src_nb_channels; ch++) {
        //         memcpy(src_data_[0], frame->data[ch] + data_size * i,
        //                data_size);
        //     }
        // }
    }
    return 0;
}

int SwrResample::SwrConvert() {
    int ret = swr_convert(swr_ctx, dst_data_, dst_nb_samples_,
                          (uint8_t const **)src_data_, src_nb_samples_);
    if (ret < 0) {
        fprintf(stderr, "Error while converting\n");
        exit(1);
    }

    int dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
                                                 ret, dst_sample_fmt_, 1);

    int planar = av_sample_fmt_is_planar(dst_sample_fmt_);
    if (planar) {
        int data_size = av_get_bytes_per_sample(dst_sample_fmt_);
#ifdef WRITE_RESAMPLE_PCM_FILE
        for (int i = 0; i < dst_nb_samples_; i++) {
            for (int ch = 0; ch < dst_nb_channels; ch++) {
                fwrite(dst_data_[ch] + i * data_size, 1, data_size,
                       outdecodedswffile);
            }
        }
#endif
    } else {
#ifdef WRITE_RESAMPLE_PCM_FILE
        fwrite(dst_data_[0], 1, dst_bufsize, outdecodedswffile);
#endif
        audioPlayer.writeData((const char *)(dst_data_[0]), dst_bufsize);
    }

    return dst_bufsize;
}

void SwrResample::Close() {
#ifdef WRITE_RESAMPLE_PCM_FILE
    fclose(outdecodedswffile);
#endif

    if (src_data_) {
        av_freep(&src_data_[0]);
    }

    av_freep(&src_data_);

    if (dst_data_) {
        av_freep(&dst_data_[0]);
    }

    av_freep(&dst_data_);

    swr_free(&swr_ctx);

    audioPlayer.Stop();
}

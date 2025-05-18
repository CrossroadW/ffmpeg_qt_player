//
// Created by awe on 25-5-9.
//

#ifndef DEMUXER_H
#define DEMUXER_H

#include <cstdint>
#include <vector>

struct VideoInfo {
    uint8_t *y,  *u,  *v;
    int width, height;
};

using VideoFrame = VideoInfo;
using VideoFrame2 = struct AVFrame*;
using AudioFrame = std::vector<const char *>;

#endif //DEMUXER_H

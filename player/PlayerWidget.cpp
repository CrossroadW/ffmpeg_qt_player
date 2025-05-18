#include "PlayerWidget.h"
#include <spdlog/spdlog.h>
#include "PlayerController.h"
#include <QPainter>
#include <spdlog/spdlog.h>
#include "libyuv.h"

extern "C" {
#include <libavutil/frame.h>
}

PlayerWidget::PlayerWidget(QWidget *parent):
#ifdef use_gl_widget
    QOpenGLWidget(parent)
#else
    QWidget(parent)
#endif
{}

namespace {
uint8_t *yuvData = nullptr;
uint8_t *rgbaData = nullptr;

int width_ = 0;
int height_ = 0;
}

static QRect
scaleKeepAspectRatio(const QRect &outer, int inner_w, int inner_h) {
    // 无效输入检查
    if (inner_w <= 0 || inner_h <= 0)
        return QRect(0, 0, 0, 0);

    const int outer_w = outer.width();
    const int outer_h = outer.height();

    // 整数运算优化（避免浮点除法）
    const bool width_limited = (outer_w * inner_h) < (outer_h * inner_w);
    const int scaled_w =
        width_limited ? outer_w : (inner_w * outer_h / inner_h);
    const int scaled_h =
        width_limited ? (inner_h * outer_w / inner_w) : outer_h;

    // 一次性构造结果矩形（比分别设置x/y/width/height更快）
    return QRect(
        outer.left() + (outer_w - scaled_w) / 2, // 自动居中x
        outer.top() + (outer_h - scaled_h) / 2, // 自动居中y
        scaled_w,
        scaled_h
        );
}

namespace {
std::vector<uint8_t> g_rgbaData;
int g_width = 0;
int g_height = 0;
int g_stride = 0;
#ifdef use_gl_widget
GLuint textureId;
#endif
}
#ifndef use_gl_widget

void PlayerWidget::onFrameChanged(VideoFrame2 frame) {
    const uint8_t *src_y = frame->data[0];
    const uint8_t *src_u = frame->data[1];
    const uint8_t *src_v = frame->data[2];

    int src_stride_y = frame->linesize[0];
    int src_stride_u = frame->linesize[1];
    int src_stride_v = frame->linesize[2];
    // spdlog::warn("onFrameChanged: VideoFrame2");
    g_width = frame->width;
    g_height = frame->height;

    g_rgbaData = std::vector<uint8_t>(g_width * g_height * 4);
    g_stride = g_width * 4;

    // 调用转换
    libyuv::I420ToARGB(
        src_y, src_stride_y,
        src_u, src_stride_u,
        src_v, src_stride_v,
        g_rgbaData.data(), g_stride,
        g_width, g_height
        );
    this->update();
}
#else
namespace {
int lastWidth = 0;
int lastHeight = 0;
}

void PlayerWidget::onFrameChanged(VideoFrame2 frame) {
    const uint8_t *src_y = frame->data[0];
    const uint8_t *src_u = frame->data[1];
    const uint8_t *src_v = frame->data[2];

    int src_stride_y = frame->linesize[0];
    int src_stride_u = frame->linesize[1];
    int src_stride_v = frame->linesize[2];
    // spdlog::warn("onFrameChanged: VideoFrame2");
    g_width = frame->width;
    g_height = frame->height;

    g_rgbaData = std::vector<uint8_t>(g_width * g_height * 4);
    g_stride = g_width * 4;
    spdlog::warn(" onFrameChangedbefore: VideoFrame2");
    // 调用转换
    libyuv::I420ToABGR(
        src_y, src_stride_y,
        src_u, src_stride_u,
        src_v, src_stride_v,
        g_rgbaData.data(), g_stride,
        g_width, g_height
        );
    makeCurrent(); // 确保当前 OpenGL 上下文激活
    glBindTexture(GL_TEXTURE_2D, textureId);

    if (g_width != lastWidth || g_height != lastHeight) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_width, g_height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, g_rgbaData.data());
        lastWidth = g_width;
        lastHeight = g_height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_width, g_height, GL_RGBA,
                        GL_UNSIGNED_BYTE, g_rgbaData.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    doneCurrent();
    update();
    av_frame_free(&frame);
}
#endif

// 优化第一版： 直接绘制到目标大小的图像，避免双重缩放
#ifndef use_gl_widget

void PlayerWidget::paintEvent(QPaintEvent *event) {
#ifdef use_old_paint
    if (yuvData == nullptr || rgbaData == nullptr) {
        return;
    }
#else
    if (g_rgbaData.empty()) {
        return;
    }
#endif
    QPainter painter(this);

    const QRect viewRect = rect();

    const QRect dstRect = scaleKeepAspectRatio(viewRect, g_width, g_height);
    QImage rgbImage = QImage(
        g_rgbaData.data(),
        g_width,
        g_height,
        g_stride,
        QImage::Format_ARGB32
        ).scaled(dstRect.size(), Qt::KeepAspectRatioByExpanding,
                 Qt::FastTransformation);

#ifdef use_old_paint
    const QRect dstRect = scaleKeepAspectRatio(viewRect, width_, height_);
    QImage rgbImage = QImage(
        rgbaData,
        width_,
        height_,
        QImage::Format_RGBA8888
        ).scaled(dstRect.size(), Qt::KeepAspectRatioByExpanding,
                 Qt::SmoothTransformation);
#endif
    painter.drawImage(dstRect, rgbImage);
}
#endif
#ifdef use_gl_widget
void PlayerWidget::initializeGL() {
    initializeOpenGLFunctions();

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    glClearColor(0.f, 0.f, 0.f, 1.f);
}

void PlayerWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D); // 如果 core profile 会无效，推荐用 shader pipeline
    glBindTexture(GL_TEXTURE_2D, textureId);

    glBegin(GL_TRIANGLE_STRIP); // 用 strip 简化矩形绘制
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(-1.0f, -1.0f); // 左下
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(1.0f, -1.0f); // 右下
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(-1.0f, 1.0f); // 左上
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(1.0f, 1.0f); // 右上
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
}
#endif


namespace {
using namespace std::chrono;
int cnt = 0;
system_clock::time_point begin;
system_clock::time_point after10s;
}

void PlayerWidget::onFrameChanged(VideoFrame frame) {
    // spdlog::warn("onFrameChanged no implement");

    // if (begin == system_clock::time_point{}) {
    //     begin = system_clock::now();
    //     after10s = begin + 3s;
    // }
    // if (after10s < system_clock::now()) {
    //     spdlog::warn("Fps is: {}", cnt / 3);
    // } else {
    //     cnt++;
    // }
    // spdlog::info("frame [{}] [{}] ", frame.width, frame.height);
    auto y = frame.y;
    auto u = frame.u;
    auto v = frame.v;
    width_ = frame.width;
    height_ = frame.height;
    int size = width_ * height_;
    if (yuvData == nullptr || rgbaData == nullptr) {
        const int size = width_ * height_ * 3 / 2;
        yuvData = new uint8_t[size];
        memset(yuvData, 0, size);

        const int rgbaSize = width_ * height_ * 4;
        rgbaData = new uint8_t[rgbaSize];
        memset(rgbaData, 0, rgbaSize);
    }
    memcpy(yuvData, y, size);
    memcpy(yuvData + size, v, size / 4);
    memcpy(yuvData + size * 5 / 4, u, size / 4);

    libyuv::I420ToABGR(yuvData, width_, yuvData + size, width_ / 2,
                       yuvData + size * 5 / 4, width_ / 2,
                       rgbaData, width_ * 4,
                       width_, height_);
    this->update();
}

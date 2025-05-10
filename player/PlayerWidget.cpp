#include "PlayerWidget.h"
#include <spdlog/spdlog.h>
#include "PlayerController.h"
#include <QPainter>
#include <spdlog/spdlog.h>
#include "libyuv.h"
PlayerWidget::PlayerWidget(QWidget *parent): QWidget(parent) {}
namespace {
uint8_t* yuvData = nullptr;
uint8_t* rgbaData = nullptr;

int width_ = 0;
int height_ = 0;
}
void PlayerWidget::paintEvent(QPaintEvent *event) {
    if (yuvData == nullptr || rgbaData == nullptr) {
        return;
    }

    QPainter painter(this);

    QImage rgbImage(rgbaData, width_, height_, QImage::Format_RGBA8888);

    const int window_with = width();
    const int window_height = height();

    float window_rate = (float)window_with / (float)window_height;
    float imag_rate = (float)width_ / (float)height_;

    int target_width;
    int target_height;
    int x;
    int y;

    if (window_rate > imag_rate) {
        target_height = window_height;
        target_width = target_height * imag_rate;
        x = (window_with - target_width) / 2;
        y = 0;
    } else {
        target_width = window_with;
        target_height = target_width / imag_rate;
        y = (window_height - target_height) / 2;
        x = 0;
    }
    QSize tszie(target_width, target_height);

    painter.drawImage(
        x, y, rgbImage.scaled(tszie, Qt::KeepAspectRatioByExpanding));
}


void PlayerWidget::onFrameChanged(VideoFrame frame) {
    // spdlog::warn("onFrameChanged no implement");


    spdlog::info("frame [{}] [{}] ", frame.width, frame.height);
    auto y = frame.y;
    auto u = frame.u;
    auto v = frame.v;
    width_ = frame.width;
    height_ = frame.height;
    int size = width_ * height_;
    if (yuvData == nullptr || rgbaData == nullptr) {
        const int size = width_ * height_ * 3 / 2;
        yuvData = new uint8_t[size];
        memset(yuvData, 5, size);

        const int rgbaSize = width_ * height_ * 4;
        rgbaData = new uint8_t[rgbaSize];
        memset(rgbaData, 5, rgbaSize);
    }
    memcpy(yuvData, y, size);
    memcpy(yuvData + size, v, size / 4);
    memcpy(yuvData + size * 5 / 4, u, size / 4);

    libyuv::I420ToARGB(yuvData, width_, yuvData + size, width_ / 2,
                       yuvData + size * 5 / 4, width_ / 2,
                       rgbaData, width_ * 4,
                       width_, height_);
    this->update();
}

// RendererBridge::RendererBridge(PlayerWidget *render, QObject *parent):
//     QObject(parent), mRender(render) {
//     if (render) {
//         connect(this, &RendererBridge::frameChanged, mRender,
//                 &PlayerWidget::onFrameChanged);
//     }
// }
//
// void RendererBridge::setRenderWidget(PlayerWidget *render) {
//     if (mRender) {
//         delete mRender;
//     }
//     mRender = render;
//     connect(this, &RendererBridge::frameChanged, mRender,
//             &PlayerWidget::onFrameChanged);
// }
//
// PlayerWidget *RendererBridge::renderWidget() {
//     return mRender;
// }
//
// void RendererBridge::setPlayerController(PlayerController *controller) {
//     if (mPlayerController) {
//         delete mPlayerController;
//     }
//     connect(controller, &PlayerController::VideoFrameReady, this,
//             &RendererBridge::frameChanged);
//     mPlayerController = controller;
// }
//
// PlayerController *RendererBridge::playerController() {
//     return mPlayerController;
// }

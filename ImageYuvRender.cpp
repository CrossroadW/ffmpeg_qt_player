#include "ImageYuvRender.h"
#include "libyuv.h"
#include <QPainter>
#include <QDebug>

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


// 优化第一版： 直接绘制到目标大小的图像，避免双重缩放
void ImageYuvRender::paintEvent(QPaintEvent *event) {
    if (yuvData == nullptr || rgbaData == nullptr) {
        return;
    }

    QPainter painter(this);
    const QRect viewRect = rect();

    // 直接创建目标大小的图像，避免双重缩放
    const QRect dstRect = scaleKeepAspectRatio(viewRect, width_, height_);
    QImage rgbImage = QImage(
        rgbaData,
        width_,
        height_,
        QImage::Format_RGBA8888
    ).scaled(dstRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    painter.drawImage(dstRect, rgbImage);
}

// 未优化版
// void ImageYuvRender::paintEvent(QPaintEvent *event) {
//     if (yuvData == nullptr || rgbaData == nullptr) {
//         return;
//     }
//
//     QPainter painter(this);
//
//     QImage rgbImage(rgbaData, width_, height_, QImage::Format_RGBA8888);
//
//     QRect dst = scaleKeepAspectRatio(rect(), width_, height_);
//     painter.drawImage(
//         dst, rgbImage.scaled(dst.size(), Qt::KeepAspectRatioByExpanding));
// }


void ImageYuvRender::initData(int w, int h, int stride_width) {
    width_ = w;
    height_ = h;
    stride_width_ = stride_width;

    allocData();

    qDebug() << "initData ok";
}


void ImageYuvRender::updateYuv(uint8_t *y, uint8_t *u, uint8_t *v) {
    int size = width_ * height_;
    memcpy(yuvData, y, size);
    memcpy(yuvData + size, v, size / 4);
    memcpy(yuvData + size * 5 / 4, u, size / 4);

    libyuv::I420ToARGB(yuvData, width_, yuvData + size, width_ / 2,
                       yuvData + size * 5 / 4, width_ / 2,
                       rgbaData, width_ * 4,
                       width_, height_);

    this->update();
}

void ImageYuvRender::Close() {
    if (yuvData) {
        delete[] yuvData;
        yuvData = nullptr;
    }
    if (rgbaData) {
        delete[] rgbaData;
        rgbaData = nullptr;
    }
    this->update();
}

void ImageYuvRender::allocData() {
    if (yuvData == NULL) {
        const int size = width_ * height_ * 3 / 2;
        yuvData = new uint8_t[size];
        memset(yuvData, 5, size);

        const int rgbaSize = width_ * height_ * 4;
        rgbaData = new uint8_t[rgbaSize];
        memset(rgbaData, 5, rgbaSize);
    }

    qDebug() << "initData ok";
}

#pragma once

#include <QWidget>
#include <array>
#include "Demuxer.h"

class PlayerController;

class PlayerWidget : public QWidget {
    Q_OBJECT

public:

    explicit PlayerWidget(QWidget *parent = nullptr);
    void paintEvent(QPaintEvent *event) override;

public Q_SLOTS:
    void onFrameChanged(VideoFrame);

private:
};
//
// class RendererBridge : public QObject {
//     Q_OBJECT
//
// public:
//     explicit RendererBridge(PlayerWidget *render = nullptr,QObject *parent = nullptr);
//     void setRenderWidget(PlayerWidget *render);
//     PlayerWidget* renderWidget();
//     void setPlayerController(PlayerController *controller);
//     PlayerController* playerController();
// Q_SIGNALS:
//     void frameChanged(VideoFrame const &);
// private:
//     PlayerWidget *mRender{};
//     PlayerController *mPlayerController{};
// };

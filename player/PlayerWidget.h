#pragma once

#include <QWidget>
#include <array>
#include "Demuxer.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
class PlayerController;
// #define use_gl_widget

class PlayerWidget :
#ifdef use_gl_widget
    public QOpenGLWidget,
protected QOpenGLFunctions
#else
    public QWidget
#endif
{
    Q_OBJECT

public:
    explicit PlayerWidget(QWidget *parent = nullptr);

#ifdef   use_gl_widget
    void initializeGL() override;
    void paintGL() override;
#else
    void paintEvent(QPaintEvent *event) override;
#endif

public Q_SLOTS:
    void onFrameChanged(VideoFrame);
    void onFrameChanged(VideoFrame2);

private:
};

#include "MainWindow.h"
#include <QAction>
#include <QDebug>
#include <QMenuBar>
#include "PlayerWidget.h"
#include "PlayerController.h"
#include <qcoreapplication.h>
#include <qevent.h>
#include <QFileDialog>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSlider>
#include <spdlog/spdlog.h>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    this->resize({700, 600});
    this->setMenuBar(new QMenuBar{});


    auto file = menuBar()->addMenu("File");
    auto onOpen = file->addAction("open file");
    connect(onOpen, &QAction::triggered, [=] {
        spdlog::info("open file");
        auto filePath = QFileDialog::getOpenFileName(this, "Open File",
            "",
            "Video Files (*.mp4)");
        try {
            mController->Open(filePath.toStdString());
            // mController->Play();
        } catch (const std::exception &e) {
            spdlog::error("open file error:{}", e.what());
        }
    });
    auto onUrl = file->addAction("open url");
    connect(onUrl, &QAction::triggered, [] {
        spdlog::info("open url");
    });
    auto widget = new QWidget{};
    setCentralWidget(widget);
    auto layout = new QVBoxLayout(widget);
    mRender = new PlayerWidget{};
    layout->addWidget(mRender);
    mController = new PlayerController{mRender};

    auto buttom = new QHBoxLayout{};

    auto playBtn = new QPushButton{"播放"};
    auto before = new QPushButton{"上"};
    auto after = new QPushButton{"下"};
    auto progressBar = new QSlider{Qt::Horizontal};
    progressBar->setRange(0, 1000);
    progressBar->setValue(50);
    buttom->addWidget(before);
    buttom->addWidget(playBtn);
    buttom->addWidget(after);
    buttom->addWidget(progressBar);
    layout->addLayout(buttom);
    connect(playBtn, &QPushButton::clicked, this, [this] {
        spdlog::info("playBtn");
        mController->Play();
    });
    connect(before, &QPushButton::clicked, this, [this] {
        spdlog::info("before");
        // mController->Before();
    });
    connect(after, &QPushButton::clicked, this, [this] {
        spdlog::info("after");
        // mController->After();
    });
    connect(progressBar, &QSlider::valueChanged, this,
            &MainWindow::OnSliderValueChanged);
    connect(progressBar, &QSlider::sliderPressed, this,
            &MainWindow::OnSliderPressed);
    connect(progressBar, &QSlider::sliderReleased, this,
            &MainWindow::OnSliderValueReleased);
    connect(mController, &PlayerController::StateChanged, this, [=] {
        spdlog::info("OnStateChanged");
        if (mController->state() == PlayerState::Playing) {
            playBtn->setText("暂停");
        } else if (mController->state() == PlayerState::Paused) {
            playBtn->setText("播放");
        }
    });
}

MainWindow::~MainWindow() {
    if (mRender) {
        delete mRender;
    }
    if (mController) {
        delete mController;
    }
}

void MainWindow::OnSliderValueChanged(int value) {
    // spdlog::info("OnSliderValueChanged: {}", value);
    mCurrentPos = value / 1000.0;
}

void MainWindow::OnSliderPressed() {
    spdlog::info("OnSliderPressed");
}

void MainWindow::OnSliderValueReleased() {
    spdlog::info("OnSliderValueReleased");
    spdlog::info("OnSliderPressed: {}", mCurrentPos);
    mController->SeekTo(mCurrentPos);
}

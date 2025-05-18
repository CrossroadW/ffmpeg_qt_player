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
#include <QStatusBar>
#include <spdlog/spdlog.h>
#include <QTimer>
#include <format>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    this->resize({700, 600});
    this->setMenuBar(new QMenuBar{});
    mProgressTimer = new QTimer(this);

    auto file = menuBar()->addMenu("File");
    auto onOpen = file->addAction("open file");
    connect(onOpen, &QAction::triggered, this, [this] {
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
    auto before = new QPushButton{"后退"};
    auto after = new QPushButton{"前进"};
    auto closeBnt = new QPushButton{"关闭"};
    mProgressBar = new QSlider{Qt::Horizontal};
    mProgressBar->setRange(0, 1000);
    mProgressBar->setValue(0);
    buttom->addWidget(before);
    buttom->addWidget(playBtn);
    buttom->addWidget(after);
    buttom->addWidget(mProgressBar);
    layout->addLayout(buttom);
    layout->addWidget(closeBnt);
    connect(closeBnt, &QPushButton::clicked, this, [this] {
        spdlog::info("close");
        delete mController;
        mController = new PlayerController{mRender};
        mProgressTimer->stop();
        mProgressBar->setValue(0);
    });
    connect(mProgressTimer, &QTimer::timeout, this, [this] {
        if (mController->state() == PlayerState::Playing) {
            auto [curr, total] = mController->CurrentPosition();
            mProgressBar->setValue(1.0 * curr / total * 1000.0);
            mCurrentPos = curr;
            mTotalPos = total;
            int curr_min = curr / 1000 / 60;
            int curr_sec = (curr / 1000) % 60;

            int total_min = total / 1000 / 60;
            int total_sec = (total / 1000) % 60;

            QString msg = QString::fromStdString(fmt::format(
                "{:02}:{:02} / {:02}:{:02}",
                curr_min, curr_sec,
                total_min, total_sec));

            if (auto statusBar = this->statusBar()) {
                statusBar->showMessage(msg);
            }
        }
    });
    connect(playBtn, &QPushButton::clicked, this, [this] {
        spdlog::info("playBtn");
        mController->Play();

        mProgressTimer->start(1000 / 30);
    });
    auto seekOffset = 5 * 1000;

    connect(before, &QPushButton::clicked, this, [this,seekOffset] {
        spdlog::info("before");
        // mController->Before();
        // before 1 second
        if (mCurrentPos - seekOffset < 0) {
            mController->SeekTo(0);
        } else {
            mController->SeekTo(mCurrentPos - seekOffset);
        }
    });
    connect(after, &QPushButton::clicked, this, [this,seekOffset] {
        spdlog::info("after curr:{},next:{}", mCurrentPos, mCurrentPos + 1000);
        // mController->After();
        // after 1 second
        if (mCurrentPos + seekOffset > mTotalPos) {
            mController->SeekTo(mTotalPos);
        } else {
            mController->SeekTo(mCurrentPos + seekOffset);
        }
    });
    connect(mProgressBar, &QSlider::valueChanged, this,
            &MainWindow::OnSliderValueChanged);
    connect(mProgressBar, &QSlider::sliderPressed, this,
            &MainWindow::OnSliderPressed);
    connect(mProgressBar, &QSlider::sliderReleased, this,
            &MainWindow::OnSliderValueReleased);
    connect(mController, &PlayerController::StateChanged, this, [this,playBtn] {
        spdlog::info("OnStateChanged");
        if (mController->state() == PlayerState::Playing) {
            playBtn->setText("暂停");
            if (!mProgressTimer->isActive()) {
                mProgressTimer->start();
                auto [curr, total] = mController->CurrentPosition();
                mProgressBar->setValue(1.0 * curr / total * 1000.0);
                mCurrentPos = curr;
                mTotalPos = total;
                int curr_min = curr / 1000 / 60;
                int curr_sec = (curr / 1000) % 60;

                int total_min = total / 1000 / 60;
                int total_sec = (total / 1000) % 60;

                QString msg = QString::fromStdString(fmt::format(
                    "{:02}:{:02} / {:02}:{:02}",
                    curr_min, curr_sec,
                    total_min, total_sec));

                if (auto statusBar = this->statusBar()) {
                    statusBar->showMessage(msg);
                }
            }
        } else if (mController->state() == PlayerState::Paused) {
            playBtn->setText("播放");
            if (mProgressTimer->isActive()) {
                mProgressTimer->stop();
            }
        }
    });
    // add  status bar
    auto statusBar = new QStatusBar{};
    setStatusBar(statusBar);
    statusBar->showMessage("Ready");
}

MainWindow::~MainWindow() {
    if (mRender) {
        delete mRender;
    }
    if (mController) {
        delete mController;
    }
    if (mProgressTimer) {
        delete mProgressTimer;
    }
}

void MainWindow::OnSliderValueChanged(int value) {
    // spdlog::info("OnSliderValueChanged: {}", value);
    // spdlog::warn("OnSliderValueChanged: {}", value);
}

void MainWindow::OnSliderPressed() {
    mProgressTimer->stop();
    // if (mController->state() == PlayerState::Playing) {
    //     mController->Play();
    // }
    spdlog::warn("OnSliderPressed");
}

void MainWindow::OnSliderValueReleased() {
    int value = mProgressBar->value();
    mController->SeekTo(value * 1.0 / 1000 * mTotalPos);
    spdlog::warn("OnSliderValueReleased");
    mProgressTimer->start();
    // if (mController->state() == PlayerState::Paused) {
    //     mController->Play();
    // }
}

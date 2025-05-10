#include "MyQtMainWindow.h"
#include <QVBoxLayout>
#include <QPushButton>
#include <QDebug>
#include "FileDecode.h"
#include <QTime>
#include <thread>
#include <iostream>
#include <string>
#include "ChooseUrlDialog.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
}

MyQtMainWindow::~MyQtMainWindow() {}

MyQtMainWindow::MyQtMainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    assert(this->menuBar() == ui->menubar);
    QMenu *fileMenu = ui->menubar->addMenu("File");
    QAction *openAction = fileMenu->addAction("OpenFile");
    QAction *openNetAction = fileMenu->addAction("OpenNetUrl");
    ui->menubar->setFixedHeight(40);

    QObject::connect(openAction, &QAction::triggered, this,
                     &MyQtMainWindow::OpenFileDialog);
    QObject::connect(openNetAction, &QAction::triggered, this,
                     &MyQtMainWindow::OpenNetUrlDialog);

    auto *bottom = new QWidget();
    bottom->setFixedHeight(50);

    auto *hlayout = new QHBoxLayout(bottom);
    stopButton = new QPushButton();
    stopButton->setText("stop");
    hlayout->addWidget(stopButton);
    QObject::connect(stopButton, &QPushButton::clicked, this,
                     &MyQtMainWindow::ClosePlayer);

    pushButton = new QPushButton();
    pushButton->setText("pause");
    hlayout->addWidget(pushButton);
    QObject::connect(pushButton, &QPushButton::clicked, this,
                     &MyQtMainWindow::ClickPlay);

    progressBar = new QSlider(Qt::Horizontal);
    hlayout->addWidget(progressBar);
    progressBar->setRange(0, 1000);
    progressBar->setValue(0);
    connect(progressBar, &QSlider::valueChanged, this,
            &MyQtMainWindow::OnSliderValueChanged);
    connect(progressBar, &QSlider::sliderPressed, this,
            &MyQtMainWindow::OnSliderPressed);
    connect(progressBar, &QSlider::sliderReleased, this,
            &MyQtMainWindow::OnSliderValueReleased);

    info_label = new QLabel();
    info_label->setText("10:34:40");
    hlayout->addWidget(info_label);

    ui->statusbar->setStyleSheet("background-color: gray;");
    bottom->setStyleSheet("background-color: white;");

    setStyleSheet("QMenuBar { background-color: gray; color: black; }");

    QVBoxLayout *layout2 = new QVBoxLayout(ui->centralwidget);
    render = new ImageYuvRender(ui->centralwidget);
    ui->centralwidget->setStyleSheet("background-color: black;");
    layout2->addWidget(render);
    layout2->addWidget(bottom);
}

void MyQtMainWindow::closeEvent(QCloseEvent *event) {
    ClosePlayer();
    ReleaseUI();

    event->accept();
}

void MyQtMainWindow::ClickPlay() {
    if (!fileDecode) {
        return;
    }

    if (playFlag) {
        fileDecode->PauseRender();
        pushButton->setText("resume");
        playFlag = false;
    } else {
        fileDecode->ResumeRender();
        pushButton->setText("pause");
        playFlag = true;
    }
}

std::string getCurrentTimeAsString() {
    auto now = std::chrono::system_clock::now();

    auto now_c = std::chrono::system_clock::to_time_t(now);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << " [" << std::put_time(std::localtime(&now_c), "%T")
        << '.' << std::setfill('0') << std::setw(3) << ms.count() << "]:";

    return ss.str();
}

void MyQtMainWindow::OnSliderPressed() {
    if (!fileDecode) {
        return;
    }

    fileDecode->PauseRead();
    fileDecode->ClearJitterBuf();

    ClickPlay();

    qDebug() << "OnSliderPressed" << ";" << getCurrentTimeAsString().c_str();
}


void MyQtMainWindow::OnSliderValueChanged(int value) {
    if (!fileDecode) {
        return;
    }

    int64_t file_len = fileDecode->GetFileLenMs();
    int64_t curr_len = value * file_len / 1000;;

    QTime time = QTime::fromMSecsSinceStartOfDay(curr_len);
    QTime time2 = QTime::fromMSecsSinceStartOfDay(file_len);

    info_label->setText(
        time.toString("hh:mm:ss") + "/" + time2.toString("hh:mm:ss"));
}

void MyQtMainWindow::OnSliderValueReleased() {
    if (!fileDecode) {
        return;
    }

    fileDecode->SetPosition(progressBar->value());
    fileDecode->ResumeRead();

    ClickPlay();
}

void MyQtMainWindow::OpenFileDialog() {
    QFileDialog fileDialog(this);
    fileDialog.setNameFilter("mp4 (*.mp4)");
    int ret = fileDialog.exec();
    if (ret == QDialog::Accepted) {
        QString fileName = fileDialog.selectedFiles().first();

        std::cout << "filename " << fileName.toStdString()<< std::endl;

        QObject::connect(&timer, &QTimer::timeout, this,
                         &MyQtMainWindow::UpdatePlayerInfo);
        timer.start(1000);

        fileDecode = new FileDecode();
        fileDecode->SetMyWindow(this);
        fileDecode->StartRead(fileName.toStdString());

        playFlag = true;
        pushButton->setText("pause");
    }
    fileDialog.close();
}

void MyQtMainWindow::OpenNetUrlDialog() {
    std::string url = "";
    ChooseUrlDialog urlDialog;
    if (urlDialog.exec() == QDialog::Accepted) {
        url = urlDialog.url;

        QObject::connect(&timer, &QTimer::timeout, this,
                         &MyQtMainWindow::UpdatePlayerInfo);
        timer.start(1000);

        fileDecode = new FileDecode();
        fileDecode->SetMyWindow(this);
        fileDecode->StartRead(url);

        playFlag = true;
        pushButton->setText("pause");
    } else {
        return;
    }
}

void MyQtMainWindow::test() {
    QObject::connect(&timer, &QTimer::timeout, this,
                     &MyQtMainWindow::UpdatePlayerInfo);
    timer.start(1000);

    fileDecode->SetMyWindow(this);
    fileDecode->StartRead("D:/test_file/long.mp4");
}

void MyQtMainWindow::UpdatePlayerInfo() {
    if (!playFlag) {
        return;
    }

    int64_t file_len = fileDecode->GetFileLenMs();
    int64_t curr_len = fileDecode->GetPlayingMs();

    QTime time = QTime::fromMSecsSinceStartOfDay(curr_len);
    QTime time2 = QTime::fromMSecsSinceStartOfDay(file_len);
    float rato = (float)curr_len * 1000 / (float)file_len;
    progressBar->setValue((int)(rato));
    info_label->setText(
        time.toString("hh:mm:ss") + "/" + time2.toString("hh:mm:ss"));
}

void MyQtMainWindow::initData(int w, int h, int stride_w) {
    render->initData(w, h, stride_w);
}


void MyQtMainWindow::updateYuv(uint8_t *y, uint8_t *u, uint8_t *v) {
    render->updateYuv(y, u, v);
}


void MyQtMainWindow::ReleaseUI() {
    delete pushButton;
    pushButton = nullptr;
}

void MyQtMainWindow::ClosePlayer() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    qDebug() << oss.str().c_str();

    timer.stop();

    if (fileDecode) {
        fileDecode->Close();

        delete fileDecode;
        fileDecode = nullptr;
    }

    render->Close();

    pushButton->setText("start");
    progressBar->setValue(0);
}

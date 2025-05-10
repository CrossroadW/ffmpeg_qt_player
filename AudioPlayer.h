#pragma once

#include <QApplication>
#include <QAudioOutput>
#include <QIODevice>
#include <QDebug>

class AudioPlayer {
public:
    AudioPlayer() : audioOutput(nullptr), outputDevice(nullptr) {}

    ~AudioPlayer() {
        Stop();  // 析构时也确保资源清理
    }

    void SetFormat(int dst_nb_samples, int rate, int sample_size, int nch) {
        Stop();  // 保证旧的 QAudioOutput 释放

        QAudioFormat format;
        format.setSampleRate(rate);
        format.setChannelCount(nch);
        format.setSampleSize(sample_size);
        format.setCodec("audio/pcm");
        format.setByteOrder(QAudioFormat::LittleEndian);
        format.setSampleType(QAudioFormat::SignedInt);

        audioOutput = new QAudioOutput(format);
        audioOutput->setVolume(1.0);
        outputDevice = audioOutput->start();
    }

    void writeData(const char *data, qint64 len) {
        if (outputDevice)
            outputDevice->write(data, len);
    }

    void Stop() {
        if (audioOutput) {
            audioOutput->stop();
        }
    }

private:
    QIODevice *outputDevice{};
    QAudioOutput *audioOutput{};
};

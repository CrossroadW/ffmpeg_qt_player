#pragma once
#include <string>
#include <memory>
#include <qobjectdefs.h>
#include "Demuxer.h"
#include <qobject.h>
#include <future>
#include <thread>

extern "C" {
#include <libavutil/frame.h>
}

class DecoderManager;
class SystemClock;

enum class PlayerState {
    Idle,
    Ready,
    Playing,
    Paused,
    Seeking,
    Error,
};

Q_DECLARE_METATYPE(VideoFrame);

Q_DECLARE_METATYPE(VideoFrame2);

class PlayerWidget;
class RendererBridge;

class PlayerController : public QObject {
    Q_OBJECT

public:
    PlayerController(PlayerWidget *rendererBridge);
    ~PlayerController() override;
    void Open(const std::string &url); // 支持本地/网络
    void Play();
    void Close();
    void SeekTo(int64_t seek_pos);
    std::pair<int64_t, int64_t> CurrentPosition() const;
Q_SIGNALS:
    void VideoFrameReady(VideoFrame2 frame);
    void VideoFrameReady(VideoFrame frame);
    void AudioFrameReady(AudioFrame frame);
    void ErrorOccurred(std::string msg);
    void StateChanged(PlayerState state);

public:
    PlayerState state() const {
        return mState;
    }

    const auto &url() const {
        return mUrl;
    }

private:
    PlayerState mState{PlayerState::Idle};
    std::string mUrl{};
    std::jthread mReadTask{};
    std::jthread mVideoTask{};
    std::jthread mAudioTask{};
};

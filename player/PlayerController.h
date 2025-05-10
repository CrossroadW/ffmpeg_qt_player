#pragma once
#include <string>
#include <memory>
#include <qobjectdefs.h>
#include "Demuxer.h"
#include <qobject.h>
#include <future>
#include <thread>
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

Q_DECLARE_METATYPE(VideoInfo);

class PlayerWidget;
class RendererBridge;

class PlayerController : public QObject {
    Q_OBJECT

public:
    PlayerController(PlayerWidget *rendererBridge);
    ~PlayerController()override;
    void Open(const std::string &url); // 支持本地/网络
    void Play();
    void Quit();
    void SeekTo(int64_t ms);

    void Tick(); // 驱动状态机每帧更新，可用线程或定时器驱动

Q_SIGNALS:
    void VideoFrameReady(VideoInfo const &frame);
    void AudioFrameReady(AudioFrame frame);
    void ErrorOccurred(std::string msg);
    void StateChanged(PlayerState state);

public:
    PlayerState state() const {
        return mState;
    }

private:
    PlayerState mState{PlayerState::Idle};
    // std::shared_ptr<Demuxer> mDemuxer{};
    // std::shared_ptr<DecoderManager> mDecoderManager{};
    // std::shared_ptr<SystemClock> mClock{}; // 主时钟
    std::string mUrl{};
    std::jthread mReadTask{};
    std::jthread mVideoTask{};
    std::jthread mAudioTask{};
};

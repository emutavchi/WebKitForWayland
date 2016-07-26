#ifndef MediaPlayerPrivateQt5WebRTC_h
#define MediaPlayerPrivateQt5WebRTC_h

#include "MediaPlayerPrivate.h"
#include "IntRect.h"
#include "FloatRect.h"

namespace WebCore {

class MediaPlayerPrivateQt5WebRTC : public MediaPlayerPrivateInterface
{
public:
    explicit MediaPlayerPrivateQt5WebRTC(MediaPlayer*);
    ~MediaPlayerPrivateQt5WebRTC();

    static void registerMediaEngine(MediaEngineRegistrar);

    bool hasVideo() const override { return true; }
    bool hasAudio() const override { return true; }

    void load(const String&) override { }
#if ENABLE(MEDIA_SOURCE)
    void load(const String&, MediaSourcePrivateClient*) override { }
#endif
#if ENABLE(MEDIA_STREAM)
    void load(MediaStreamPrivate&) override;
#endif
    void commitLoad() { }
    void cancelLoad() override { }

    void play() override;
    void pause() override { m_paused = true; }
    bool paused() const override { return m_paused; }
    bool seeking() const override { return false; }
    std::unique_ptr<PlatformTimeRanges> buffered() const override { return std::make_unique<PlatformTimeRanges>(); }
    bool didLoadingProgress() const override { return false; }
    void setVolume(float) override { }
    float volume() const override { return 0; }
    bool supportsMuting() const override { return true; }
    void setMuted(bool) override { }
    void setVisible(bool) override { }
    void setSize(const IntSize&) override;
    void paint(GraphicsContext&, const FloatRect&) override { }

    MediaPlayer::NetworkState networkState() const override {return MediaPlayer::Empty; }
    MediaPlayer::ReadyState readyState() const { return MediaPlayer::HaveEnoughData; }

    FloatSize naturalSize() const override;

    PlatformLayer* platformLayer() const override;
    bool supportsAcceleratedRendering() const override { return true; }

private:
    static void getSupportedTypes(HashSet<String, ASCIICaseInsensitiveHash>&);
    static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);

    MediaPlayer* m_player;
    IntSize m_size;
    MediaStreamPrivate* m_stream {nullptr};
    bool m_paused {true};
};

}
#endif

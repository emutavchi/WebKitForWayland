#ifndef _REALTIMEMEDIASOURCECENTERQT5WEBRTC_H_
#define _REALTIMEMEDIASOURCECENTERQT5WEBRTC_H_

#if ENABLE(MEDIA_STREAM)

#include "RealtimeMediaSourceCenter.h"
#include "RealtimeMediaSource.h"
#include "RealtimeMediaSourceCapabilities.h"

#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>
#include <wtf/PassRefPtr.h>
#include <wtf/text/WTFString.h>

#if USE(COORDINATED_GRAPHICS_THREADED)
#include "TextureMapperPlatformLayerProxy.h"
#endif

#include <wrtcint.h>

namespace WebCore {

WRTCInt::RTCMediaSourceCenter& getRTCMediaSourceCenter();

class RealtimeMediaSourceQt5WebRTC : public RealtimeMediaSource
{
public:
    RealtimeMediaSourceQt5WebRTC(const String& id, RealtimeMediaSource::Type type, const String& name)
        : RealtimeMediaSource(id, type, name) { }
    virtual ~RealtimeMediaSourceQt5WebRTC() { }

    virtual RefPtr<RealtimeMediaSourceCapabilities> capabilities() override { return m_capabilities; }
    virtual const RealtimeMediaSourceSettings& settings() override { return m_currentSettings; }
    virtual void startProducingData() override;
    virtual void stopProducingData() override;
    virtual bool isProducingData() const override { return m_isProducingData; }

    // helper
    void setRTCStream(std::shared_ptr<WRTCInt::RTCMediaStream> stream) { m_stream = stream; }
    WRTCInt::RTCMediaStream* rtcStream() { return m_stream.get(); }

protected:
    RefPtr<RealtimeMediaSourceCapabilities> m_capabilities;
    RealtimeMediaSourceSettings m_currentSettings;
    bool m_isProducingData { false };
    std::shared_ptr<WRTCInt::RTCMediaStream> m_stream;
};

class RealtimeAudioSourceQt5WebRTC final : public RealtimeMediaSourceQt5WebRTC
{
  public:
    RealtimeAudioSourceQt5WebRTC(const String& id, const String& name)
        : RealtimeMediaSourceQt5WebRTC(id, RealtimeMediaSource::Audio, name)
    { }
};

class RealtimeVideoSourceQt5WebRTC final : public RealtimeMediaSourceQt5WebRTC
    , public WRTCInt::RTCVideoRendererClient
#if USE(COORDINATED_GRAPHICS_THREADED)
    , public TextureMapperPlatformLayerProxyProvider
#endif
{
  public:
    RealtimeVideoSourceQt5WebRTC(const String& id, const String& name);
    void stopProducingData() override;

#if USE(COORDINATED_GRAPHICS_THREADED)
    PlatformLayer* platformLayer() const override { return const_cast<RealtimeVideoSourceQt5WebRTC*>(this); }
    RefPtr<TextureMapperPlatformLayerProxy> proxy() const override { return m_platformLayerProxy.copyRef(); }
    void swapBuffersIfNeeded() override { }
#endif

    // WRTCInt::VideoPlayerClient
    void startRenderer();
    void stopRenderer();
    void renderFrame(const unsigned char *data, int byteCount, int width, int height) override;

  private:
#if USE(COORDINATED_GRAPHICS_THREADED)
    void pushTextureToCompositor(RefPtr<Image> frame);
    IntSize m_size;
    RefPtr<TextureMapperPlatformLayerProxy> m_platformLayerProxy;
    RefPtr<GraphicsContext3D> m_context3D;
    Condition m_drawCondition;
    Lock m_drawMutex;
#endif
    std::unique_ptr<WRTCInt::RTCVideoRenderer> m_rtcRenderer;
};

typedef HashMap<String, RefPtr<RealtimeMediaSourceQt5WebRTC>> RealtimeMediaSourceQt5WebRTCMap;

class RealtimeMediaSourceCenterQt5WebRTC final : public RealtimeMediaSourceCenter {
private:
    friend NeverDestroyed<RealtimeMediaSourceCenterQt5WebRTC>;

    RealtimeMediaSourceCenterQt5WebRTC();

    void validateRequestConstraints(MediaStreamCreationClient*,
                                    RefPtr<MediaConstraints>& audioConstraints,
                                    RefPtr<MediaConstraints>& videoConstraints) override;

    void createMediaStream(PassRefPtr<MediaStreamCreationClient>,
                           PassRefPtr<MediaConstraints> audioConstraints,
                           PassRefPtr<MediaConstraints> videoConstraints) override;

    bool getMediaStreamTrackSources(PassRefPtr<MediaStreamTrackSourcesRequestClient>) override;

    void createMediaStream(MediaStreamCreationClient*,
                           const String& audioDeviceID,
                           const String& videoDeviceID) override;

    RefPtr<TrackSourceInfo> sourceWithUID(const String&, RealtimeMediaSource::Type, MediaConstraints*) override;

    RefPtr<RealtimeMediaSourceQt5WebRTC> findSource(const String&, RealtimeMediaSource::Type);
    RealtimeMediaSourceQt5WebRTCMap enumerateSources(bool needsAudio, bool needsVideo);

    RealtimeMediaSourceQt5WebRTCMap m_sourceMap;
};

}

#endif


#endif  // _REALTIMEMEDIASOURCECENTERQT5WEBRTC_H_

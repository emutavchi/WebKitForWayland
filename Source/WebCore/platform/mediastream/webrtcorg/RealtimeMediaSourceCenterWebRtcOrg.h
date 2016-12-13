#ifndef _REALTIMEMEDIASOURCECENTERWEBRTCORG_H_
#define _REALTIMEMEDIASOURCECENTERWEBRTCORG_H_

#if ENABLE(MEDIA_STREAM)

#include "RealtimeMediaSource.h"
#include "RealtimeMediaSourceCapabilities.h"
#include "RealtimeMediaSourceCenter.h"

#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/api/peerconnectioninterface.h"

#include <wtf/HashMap.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
using namespace webrtc;

class RealtimeMediaSourceCapabilities;

class RealtimeMediaSourceWebRtcOrg : public RealtimeMediaSource {
public:
    RealtimeMediaSourceWebRtcOrg(const String& id, RealtimeMediaSource::Type type, const String& name)
        : RealtimeMediaSource(id, type, name)
    {
    }
    virtual ~RealtimeMediaSourceWebRtcOrg() {}

    virtual RefPtr<RealtimeMediaSourceCapabilities> capabilities() override { return m_capabilities; }
    virtual const RealtimeMediaSourceSettings& settings() override { return m_currentSettings; }
    virtual void startProducingData() override;
    virtual void stopProducingData() override;
    virtual bool isProducingData() const override { return m_isProducingData; }

    // helper
    void setRTCStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream)
    {
        m_stream = stream;
    }
    webrtc::MediaStreamInterface* rtcStream() { return m_stream.get(); }

protected:
    RefPtr<RealtimeMediaSourceCapabilities> m_capabilities;
    RealtimeMediaSourceSettings m_currentSettings;
    bool m_isProducingData{ false };
    rtc::scoped_refptr<webrtc::MediaStreamInterface> m_stream;
};

class RealtimeAudioSourceWebRtcOrg final : public RealtimeMediaSourceWebRtcOrg {
public:
    RealtimeAudioSourceWebRtcOrg(const String& id, const String& name)
        : RealtimeMediaSourceWebRtcOrg(id, RealtimeMediaSource::Audio, name)
    {
    }
};

class RealtimeVideoSourceWebRtcOrg final : public RealtimeMediaSourceWebRtcOrg {
public:
    RealtimeVideoSourceWebRtcOrg(const String& id, const String& name);
};

typedef HashMap<String, RefPtr<RealtimeMediaSourceWebRtcOrg> > RealtimeMediaSourceWebRtcOrgMap;

class RealtimeMediaSourceCenterWebRtcOrg final : public RealtimeMediaSourceCenter {
public:
    RealtimeMediaSourceCenterWebRtcOrg();

private:
    friend NeverDestroyed<RealtimeMediaSourceCenterWebRtcOrg>;
    void ensurePeerConnectionFactory();

    void validateRequestConstraints(MediaStreamCreationClient*,
        RefPtr<MediaConstraints>& audioConstraints,
        RefPtr<MediaConstraints>& videoConstraints) override;

    rtc::scoped_refptr<webrtc::MediaStreamInterface> createMediaStream(const std::string& audioDeviceID, const std::string& videoDeviceID);

    void createMediaStream(PassRefPtr<MediaStreamCreationClient>,
        PassRefPtr<MediaConstraints> audioConstraints,
        PassRefPtr<MediaConstraints> videoConstraints) override;

    bool getMediaStreamTrackSources(PassRefPtr<MediaStreamTrackSourcesRequestClient>) override;

    void createMediaStream(MediaStreamCreationClient*,
        const String& audioDeviceID,
        const String& videoDeviceID) override;

    RefPtr<TrackSourceInfo> sourceWithUID(const String&, RealtimeMediaSource::Type, MediaConstraints*) override;

    RefPtr<RealtimeMediaSourceWebRtcOrg> findSource(const String&, RealtimeMediaSource::Type);
    RealtimeMediaSourceWebRtcOrgMap enumerateSources(bool needsAudio, bool needsVideo);

    RealtimeMediaSourceWebRtcOrgMap m_sourceMap;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
};
}
#endif
#endif // _REALTIMEMEDIASOURCECENTERWEBRTCORG_H_

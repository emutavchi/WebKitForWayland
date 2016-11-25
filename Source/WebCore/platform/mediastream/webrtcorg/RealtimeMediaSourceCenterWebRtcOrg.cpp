#include "config.h"
#include "RealtimeMediaSourceCenterWebRtcOrg.h"
#include "Logging.h"
#include "WebRtcOrgUtils.h"
#include "webrtc/common_types.h"

#include "webrtc/base/common.h"
#include "webrtc/base/nullsocketserver.h"
#include "webrtc/base/refcount.h"
#include "webrtc/base/scoped_ref_ptr.h"
#include "webrtc/base/ssladapter.h"

#include "webrtc/api/datachannelinterface.h"
#include "webrtc/api/mediaconstraintsinterface.h"
#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/api/peerconnectionfactory.h"

#include "webrtc/media/base/videocapturerfactory.h"
#include "webrtc/media/engine/webrtcvideocapturer.h"
#include "webrtc/media/engine/webrtcvideocapturerfactory.h"
#include "webrtc/modules/audio_device/include/audio_device_defines.h"
#include "webrtc/modules/video_capture/video_capture_factory.h"
#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_hardware.h"

#if ENABLE(MEDIA_STREAM)

#include "MediaStream.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackSourcesRequestClient.h"
#include "UUID.h"

#include <NotImplemented.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>

#include "FloatRect.h"
#include "PeerConnectionBackendWebRtcOrg.h"

#include <cairo.h>

namespace WebCore {

void enableWebRtcOrgPeerConnectionBackend();

// TODO: Make it possible to overload/override device specific hooks
cricket::WebRtcVideoEncoderFactory* CreateWebRtcVideoEncoderFactory()
{
    return nullptr;
}
cricket::WebRtcVideoDecoderFactory* CreateWebRtcVideoDecoderFactory()
{
    return nullptr;
}
cricket::VideoDeviceCapturerFactory* CreateVideoDeviceCapturerFactory()
{
    return nullptr;
}

webrtc::AudioDeviceModule* CreateAudioDeviceModule(int32_t)
{
    return nullptr;
}

void enumerateDevices(RealtimeMediaSource::Type type, std::vector<std::string>& devices)
{
    if (RealtimeMediaSource::Audio == type) {
        webrtc::VoiceEngine* voe = webrtc::VoiceEngine::Create();
        if (!voe) {
            LOG(Media, "Failed to create VoiceEngine");
            return;
        }
        webrtc::VoEBase* base = webrtc::VoEBase::GetInterface(voe);
        webrtc::AudioDeviceModule* externalADM = CreateAudioDeviceModule(0);
        if (base->Init(externalADM) != 0) {
            LOG(Media, "Failed to init VoEBase");
            base->Release();
            webrtc::VoiceEngine::Delete(voe);
            return;
        }
        webrtc::VoEHardware* hardware = webrtc::VoEHardware::GetInterface(voe);
        if (!hardware) {
            LOG(Media, "Failed to get interface to VoEHardware");
            base->Terminate();
            base->Release();
            webrtc::VoiceEngine::Delete(voe);
            return;
        }
        int numOfRecordingDevices;
        if (hardware->GetNumOfRecordingDevices(numOfRecordingDevices) != -1) {
            for (int i = 0; i < numOfRecordingDevices; ++i) {
                char name[webrtc::kAdmMaxDeviceNameSize];
                char guid[webrtc::kAdmMaxGuidSize];
                if (hardware->GetRecordingDeviceName(i, name, guid) != -1) {
                    devices.push_back(name);
                }
            }
        } else {
            LOG(Media, "Failed to get number of recording devices");
        }
        base->Terminate();
        base->Release();
        hardware->Release();
        webrtc::VoiceEngine::Delete(voe);
        return;
    }
    ASSERT(RealtimeMediaSource::Video == type);
    std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
        webrtc::VideoCaptureFactory::CreateDeviceInfo(0));
    if (!info) {
        LOG(Media, "Failed to get video capture device info");
        return;
    }
    std::unique_ptr<cricket::VideoDeviceCapturerFactory> factory(
        CreateVideoDeviceCapturerFactory());
    if (!factory) {
        factory.reset(new cricket::WebRtcVideoDeviceCapturerFactory);
    }

    int numOfDevices = info->NumberOfDevices();
    for (int i = 0; i < numOfDevices; ++i) {
        const uint32_t kSize = 256;
        char name[kSize] = { 0 };
        char id[kSize] = { 0 };
        if (info->GetDeviceName(i, name, kSize, id, kSize) != -1) {
            devices.push_back(name);
            break;
        } else {
            LOG(Media, "Failed to create capturer for: %s", name);
        }
    }
}

void RealtimeMediaSourceWebRtcOrg::startProducingData()
{
    if (m_stream) {
        m_isProducingData = true;
    }
}

void RealtimeMediaSourceWebRtcOrg::stopProducingData()
{
    if (m_isProducingData) {
        m_isProducingData = false;
        m_stream.release();
    }
}

RealtimeVideoSourceWebRtcOrg::RealtimeVideoSourceWebRtcOrg(const String& id, const String& name)
    : RealtimeMediaSourceWebRtcOrg(id, RealtimeMediaSource::Video, name)
{
    // TODO: obtain settings from the device
    m_currentSettings.setWidth(320);
    m_currentSettings.setHeight(240);
}

RealtimeMediaSourceCenter& RealtimeMediaSourceCenter::platformCenter()
{
    ASSERT(isMainThread());
    static NeverDestroyed<RealtimeMediaSourceCenterWebRtcOrg> center;
    return center;
}

RealtimeMediaSourceCenterWebRtcOrg::RealtimeMediaSourceCenterWebRtcOrg()
{
    WebRtcOrgUtils::getInstance()->initializeWebRtcOrg();
    enableWebRtcOrgPeerConnectionBackend();

    m_supportedConstraints.setSupportsWidth(true);
    m_supportedConstraints.setSupportsHeight(true);
}

void RealtimeMediaSourceCenterWebRtcOrg::validateRequestConstraints(MediaStreamCreationClient* client, RefPtr<MediaConstraints>& audioConstraints, RefPtr<MediaConstraints>& videoConstraints)
{
    ASSERT(client);

    bool needsAudio = !!audioConstraints;
    bool needsVideo = !!videoConstraints;

    m_sourceMap = enumerateSources(needsAudio, needsVideo);

    Vector<RefPtr<RealtimeMediaSource> > audioSources;
    Vector<RefPtr<RealtimeMediaSource> > videoSources;
    for (auto& source : m_sourceMap.values()) {
        if (needsAudio && source->type() == RealtimeMediaSource::Type::Audio)
            audioSources.append(source);
        if (needsVideo && source->type() == RealtimeMediaSource::Type::Video)
            videoSources.append(source);
    }
    client->constraintsValidated(audioSources, videoSources);
}

rtc::scoped_refptr<webrtc::MediaStreamInterface> RealtimeMediaSourceCenterWebRtcOrg::createMediaStream(const std::string& audioDeviceID, const std::string& videoDeviceID)
{

    ensurePeerConnectionFactory();
    const std::string streamLabel = createCanonicalUUIDString().utf8().data();
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream = m_peerConnectionFactory->CreateLocalMediaStream(streamLabel);
    if (!audioDeviceID.empty()) {
        const std::string audioLabel = createCanonicalUUIDString().utf8().data();
        rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
            m_peerConnectionFactory->CreateAudioTrack(
                audioLabel, m_peerConnectionFactory->CreateAudioSource(nullptr)));
        stream->AddTrack(audio_track);
    }
    if (!videoDeviceID.empty()) {
        const std::string videoLabel = createCanonicalUUIDString().utf8().data();
        cricket::WebRtcVideoDeviceCapturerFactory factory;
        cricket::VideoCapturer* videoCapturer = factory.Create(cricket::Device(videoDeviceID, videoDeviceID));
        rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
            m_peerConnectionFactory->CreateVideoTrack(
                videoLabel, m_peerConnectionFactory->CreateVideoSource(videoCapturer, nullptr)));
        stream->AddTrack(video_track);
    }
    return stream;
}

void RealtimeMediaSourceCenterWebRtcOrg::createMediaStream(PassRefPtr<MediaStreamCreationClient> /*prpQueryClient*/,
    PassRefPtr<MediaConstraints> /*audioConstraints*/,
    PassRefPtr<MediaConstraints> /*videoConstraints*/)
{
    notImplemented();
}

void RealtimeMediaSourceCenterWebRtcOrg::ensurePeerConnectionFactory()
{
    if (m_peerConnectionFactory != nullptr) {
        return;
    }
    WebRtcOrgUtils* instance = WebRtcOrgUtils::getInstance();
    m_peerConnectionFactory = instance->getPeerConnectionFactory();
    ASSERT(m_peerConnectionFactory);
}
void RealtimeMediaSourceCenterWebRtcOrg::createMediaStream(
    MediaStreamCreationClient* client,
    const String& audioDeviceID,
    const String& videoDeviceID)
{
    ASSERT(client);
    RefPtr<RealtimeMediaSourceWebRtcOrg> audioSource = findSource(audioDeviceID, RealtimeMediaSource::Audio);
    RefPtr<RealtimeMediaSourceWebRtcOrg> videoSource = findSource(videoDeviceID, RealtimeMediaSource::Video);

    m_sourceMap.clear();

    if (!audioSource && !videoSource) {
        client->failedToCreateStreamWithPermissionError();
        return;
    }
    String audioSourceName = audioSource ? audioSource->name() : String();
    String videoSourceName = videoSource ? videoSource->name() : String();

    rtc::scoped_refptr<webrtc::MediaStreamInterface> rtcStream = createMediaStream(audioSourceName.utf8().data(), videoSourceName.utf8().data());
    Vector<RefPtr<RealtimeMediaSource> > audioSources;
    Vector<RefPtr<RealtimeMediaSource> > videoSources;

    if (audioSource) {
        audioSource->setRTCStream(rtcStream);
        audioSources.append(audioSource.release());
    }

    if (videoSource) {
        videoSource->setRTCStream(rtcStream);
        videoSources.append(videoSource.release());
    }
    String id = rtcStream->label().c_str();
    client->didCreateStream(MediaStreamPrivate::create(id, audioSources, videoSources));
}

bool RealtimeMediaSourceCenterWebRtcOrg::getMediaStreamTrackSources(PassRefPtr<MediaStreamTrackSourcesRequestClient> prpClient)
{
    RefPtr<MediaStreamTrackSourcesRequestClient> requestClient = prpClient;

    RealtimeMediaSourceWebRtcOrgMap sourceMap = enumerateSources(true, true);
    TrackSourceInfoVector sources;
    for (auto& source : sourceMap.values()) {
        RefPtr<TrackSourceInfo> info = TrackSourceInfo::create(
            source->persistentID(),
            source->id(),
            source->type() == RealtimeMediaSource::Type::Video ? TrackSourceInfo::SourceKind::Video : TrackSourceInfo::SourceKind::Audio,
            source->name());
        sources.append(info);
    }

    callOnMainThread([this, requestClient, sources] {
        requestClient->didCompleteTrackSourceInfoRequest(sources);
    });

    return true;
}

RefPtr<TrackSourceInfo> RealtimeMediaSourceCenterWebRtcOrg::sourceWithUID(const String& /*UID*/, RealtimeMediaSource::Type, MediaConstraints* /*constraints*/)
{
    notImplemented();
    return nullptr;
}

RefPtr<RealtimeMediaSourceWebRtcOrg> RealtimeMediaSourceCenterWebRtcOrg::findSource(const String& id, RealtimeMediaSource::Type type)
{
    if (!id.isEmpty()) {
        auto sourceIterator = m_sourceMap.find(id);
        if (sourceIterator != m_sourceMap.end()) {
            RefPtr<RealtimeMediaSourceWebRtcOrg> source = sourceIterator->value;
            if (source->type() == type)
                return source.release();
        }
    }
    return nullptr;
}

RealtimeMediaSourceWebRtcOrgMap RealtimeMediaSourceCenterWebRtcOrg::enumerateSources(bool needsAudio, bool needsVideo)
{
    RealtimeMediaSourceWebRtcOrgMap sourceMap;

    if (needsAudio) {
        std::vector<std::string> audioDevices;
        enumerateDevices(RealtimeMediaSource::Audio, audioDevices);
        for (auto& device : audioDevices) {
            String name(device.c_str());
            String id(createCanonicalUUIDString());
            LOG(Media, "audio device id='%s', name='%s'", id.utf8().data(), name.utf8().data());
            RefPtr<RealtimeMediaSourceWebRtcOrg> audioSource = adoptRef(new RealtimeAudioSourceWebRtcOrg(id, name));
            sourceMap.add(id, audioSource.release());
        }
    }

    if (needsVideo) {
        std::vector<std::string> videoDevices;
        enumerateDevices(RealtimeMediaSource::Video, videoDevices);
        for (auto& device : videoDevices) {
            String name(device.c_str());
            String id(createCanonicalUUIDString());
            LOG(Media, "video device id='%s', name='%s'", id.utf8().data(), name.utf8().data());
            RefPtr<RealtimeMediaSourceWebRtcOrg> videoSource = adoptRef(new RealtimeVideoSourceWebRtcOrg(id, name));
            sourceMap.add(id, videoSource.release());
        }
    }
    return WTFMove(sourceMap);
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

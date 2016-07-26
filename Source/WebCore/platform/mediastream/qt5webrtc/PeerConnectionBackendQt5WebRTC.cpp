#include "config.h"
#include "PeerConnectionBackendQt5WebRTC.h"

#include "DOMError.h"
#include "JSDOMError.h"
#include "JSRTCSessionDescription.h"
#include "RTCConfiguration.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCSessionDescription.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCRtpSender.h"

#include "MediaStream.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackSourcesRequestClient.h"
#include "ScriptExecutionContext.h"
#include "UUID.h"

namespace WebCore {

using namespace PeerConnection;

static std::unique_ptr<PeerConnectionBackend> createPeerConnectionBackendQt5WebRTC(PeerConnectionBackendClient* client)
{
    return std::unique_ptr<PeerConnectionBackend>(new PeerConnectionBackendQt5WebRTC(client));
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createPeerConnectionBackendQt5WebRTC;

void initPeerConnectionBackendQt5WebRTC()
{
    PeerConnectionBackend::create = createPeerConnectionBackendQt5WebRTC;
}

PeerConnectionBackendQt5WebRTC::PeerConnectionBackendQt5WebRTC(PeerConnectionBackendClient* client)
    : m_client(client)
{
    m_rtcConnection.reset(getRTCMediaSourceCenter().createPeerConnection(this));
}

void PeerConnectionBackendQt5WebRTC::createOffer(RTCOfferOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    WRTCInt::RTCOfferAnswerOptions rtcOptions;
    rtcOptions[WRTCInt::kOfferToReceiveAudio] = !!options.offerToReceiveAudio();
    rtcOptions[WRTCInt::kOfferToReceiveVideo] = !!options.offerToReceiveVideo();
    rtcOptions[WRTCInt::kIceRestart] = options.iceRestart();
    rtcOptions[WRTCInt::kVoiceActivityDetection] = options.voiceActivityDetection();

    int id = m_rtcConnection->createOffer(rtcOptions);
    if (id > 0) {
        m_sessionDescriptionRequestId = id;
        m_sessionDescriptionPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to create offer"));
    }
}

void PeerConnectionBackendQt5WebRTC::createAnswer(RTCAnswerOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    WRTCInt::RTCOfferAnswerOptions rtcOptions;
    rtcOptions[WRTCInt::kVoiceActivityDetection] = options.voiceActivityDetection();

    int id = m_rtcConnection->createAnswer(rtcOptions);
    if (id > 0) {
        m_sessionDescriptionRequestId = id;
        m_sessionDescriptionPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to create answer"));
    }
}

void PeerConnectionBackendQt5WebRTC::setLocalDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    WRTCInt::RTCSessionDescription localDesc;
    localDesc.type = desc.type().utf8().data();
    localDesc.sdp = desc.sdp().utf8().data();

    int id = m_rtcConnection->setLocalDescription(localDesc);
    if (id > 0) {
        m_voidRequestId = id;
        m_voidPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to parse local description"));
    }
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::localDescription() const
{
    // TODO: pendingLocalDescription/currentLocalDescription
    WRTCInt::RTCSessionDescription localDesc;
    m_rtcConnection->localDescription(localDesc);
    String type = localDesc.type.c_str();
    String sdp = localDesc.sdp.c_str();
    return RTCSessionDescription::create(type, sdp);
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::currentLocalDescription() const
{
    return localDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::pendingLocalDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendQt5WebRTC::setRemoteDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    WRTCInt::RTCSessionDescription remoteDesc;
    remoteDesc.type = desc.type().utf8().data();
    remoteDesc.sdp = desc.sdp().utf8().data();

    int id = m_rtcConnection->setRemoteDescription(remoteDesc);
    if (id > 0) {
        m_voidRequestId = id;
        m_voidPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to parse remote description"));
    }
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::remoteDescription() const
{
    // TODO: pendingRemoteDescription/currentRemoteDescription
    WRTCInt::RTCSessionDescription remoteDesc;
    m_rtcConnection->remoteDescription(remoteDesc);
    String type = remoteDesc.type.c_str();
    String sdp = remoteDesc.sdp.c_str();
    return RTCSessionDescription::create(type, sdp);
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::currentRemoteDescription() const
{
    return remoteDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendQt5WebRTC::pendingRemoteDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendQt5WebRTC::setConfiguration(RTCConfiguration& config)
{
    WRTCInt::RTCConfiguration wrtcConfig;
    for(auto& server : config.iceServers()) {
        WRTCInt::RTCIceServer wrtcICEServer;
        wrtcICEServer.credential = server->credential().utf8().data();
        wrtcICEServer.username = server->username().utf8().data();
        for(auto& url : server->urls()) {
            wrtcICEServer.urls.push_back(url.utf8().data());
        }
        wrtcConfig.iceServers.push_back(wrtcICEServer);
    }
    m_rtcConnection->setConfiguration(wrtcConfig);
}

void PeerConnectionBackendQt5WebRTC::addIceCandidate(RTCIceCandidate& candidate, PeerConnection::VoidPromise&& promise)
{
    WRTCInt::RTCIceCandidate iceCandidate;
    iceCandidate.sdp = candidate.candidate().utf8().data();
    iceCandidate.sdpMid = candidate.sdpMid().utf8().data();
    iceCandidate.sdpMLineIndex = candidate.sdpMLineIndex().valueOr(0);
    bool rc = m_rtcConnection->addIceCandidate(iceCandidate);
    if (rc) {
        promise.resolve(nullptr);
    } else {
        promise.reject(DOMError::create("Failed to add ICECandidate"));
    }
}

void PeerConnectionBackendQt5WebRTC::getStats(MediaStreamTrack*, PeerConnection::StatsPromise&&)
{
    notImplemented();
}

void PeerConnectionBackendQt5WebRTC::replaceTrack(RTCRtpSender&, MediaStreamTrack&, PeerConnection::VoidPromise&& promise)
{
    notImplemented();
    promise.reject(DOMError::create("NotSupportedError"));
}

void PeerConnectionBackendQt5WebRTC::stop()
{
    m_rtcConnection->stop();
}

bool PeerConnectionBackendQt5WebRTC::isNegotiationNeeded() const
{
    return m_isNegotiationNeeded;
}

void PeerConnectionBackendQt5WebRTC::markAsNeedingNegotiation()
{
    Vector<RefPtr<RTCRtpSender>> senders = m_client->getSenders();
    for(auto &sender : senders) {
        RealtimeMediaSource& source = sender->track().source();
        WRTCInt::RTCMediaStream* stream = static_cast<RealtimeMediaSourceQt5WebRTC&>(source).rtcStream();
        if (stream) {
            m_rtcConnection->addStream(stream);
            break;
        }
    }
}

void PeerConnectionBackendQt5WebRTC::clearNegotiationNeededState()
{
    m_isNegotiationNeeded = false;
}

// ===========  WRTCInt::RTCPeerConnectionClient ==========

void PeerConnectionBackendQt5WebRTC::requestSucceeded(int id, const WRTCInt::RTCSessionDescription& desc)
{
    ASSERT(id == m_sessionDescriptionRequestId);
    ASSERT(m_sessionDescriptionPromise);

    String type = desc.type.c_str();
    String sdp = desc.sdp.c_str();

    RefPtr<RTCSessionDescription> sessionDesc(RTCSessionDescription::create(type, sdp));
    m_sessionDescriptionPromise->resolve(sessionDesc);

    m_sessionDescriptionRequestId = WRTCInt::InvalidRequestId;
    m_sessionDescriptionPromise = WTF::Nullopt;
}

void PeerConnectionBackendQt5WebRTC::requestSucceeded(int id)
{
    ASSERT(id == m_voidRequestId);
    ASSERT(m_voidPromise);

    m_voidPromise->resolve(nullptr);

    m_voidRequestId = WRTCInt::InvalidRequestId;
    m_voidPromise = WTF::Nullopt;
}

void PeerConnectionBackendQt5WebRTC::requestFailed(int id, const std::string& error)
{
    if (id == m_voidRequestId) {
        ASSERT(m_voidPromise);
        ASSERT(!m_sessionDescriptionPromise);
        m_voidPromise->reject(DOMError::create(error.c_str()));
        m_voidPromise = WTF::Nullopt;
        m_voidRequestId = WRTCInt::InvalidRequestId;
    } else if (id == m_sessionDescriptionRequestId) {
        ASSERT(m_sessionDescriptionPromise);
        ASSERT(!m_voidPromise);
        m_sessionDescriptionPromise->reject(DOMError::create(error.c_str()));
        m_sessionDescriptionPromise = WTF::Nullopt;
        m_sessionDescriptionRequestId = WRTCInt::InvalidRequestId;
    } else {
        ASSERT_NOT_REACHED();
    }
}

void PeerConnectionBackendQt5WebRTC::negotiationNeeded()
{
    m_isNegotiationNeeded = true;
    m_client->scheduleNegotiationNeededEvent();
}

void PeerConnectionBackendQt5WebRTC::didAddRemoteStream(
    WRTCInt::RTCMediaStream *stream,
    const std::vector<std::string> &audioDevices,
    const std::vector<std::string> &videoDevices)
{
    ASSERT(m_client);
    printf("XXXXXXXXXXXXXX didAddRemoteStream: %p, audio=%ld, video=%ld\n",
           stream, audioDevices.size(), videoDevices.size());

    std::shared_ptr<WRTCInt::RTCMediaStream> rtcStream;
    rtcStream.reset(stream);

    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;

    for (auto& device : audioDevices) {
        String name(device.c_str());
        String id(createCanonicalUUIDString());
        printf("audio device id='%s', name='%s'\n", id.utf8().data(), name.utf8().data());
        RefPtr<RealtimeMediaSourceQt5WebRTC> audioSource = adoptRef(new RealtimeAudioSourceQt5WebRTC(id, name));
        audioSource->setRTCStream(rtcStream);
        audioSources.append(audioSource.release());
    }
    for (auto& device : videoDevices) {
        String name(device.c_str());
        String id(createCanonicalUUIDString());
        printf("video device id='%s', name='%s'\n", id.utf8().data(), name.utf8().data());
        RefPtr<RealtimeMediaSourceQt5WebRTC> videoSource = adoptRef(new RealtimeVideoSourceQt5WebRTC(id, name));
        videoSource->setRTCStream(rtcStream);
        videoSources.append(videoSource.release());
    }
    RefPtr<MediaStreamPrivate> privateStream = MediaStreamPrivate::create(audioSources, videoSources);
    RefPtr<MediaStream> mediaStream = MediaStream::create(*m_client->scriptExecutionContext(), privateStream.copyRef());
    privateStream->startProducingData();
    m_client->addRemoteStream(mediaStream);
}

void PeerConnectionBackendQt5WebRTC::didGenerateIceCandidate(const WRTCInt::RTCIceCandidate& iceCandidate)
{
    ASSERT(m_client);
    String sdp = iceCandidate.sdp.c_str();
    String sdpMid = iceCandidate.sdpMid.c_str();
    Optional<unsigned short> sdpMLineIndex = iceCandidate.sdpMLineIndex;
    RefPtr<RTCIceCandidate> candidate = RTCIceCandidate::create(sdp, sdpMid, sdpMLineIndex);
    m_client->scriptExecutionContext()->postTask([this, candidate] (ScriptExecutionContext&) {
        m_client->fireEvent(RTCIceCandidateEvent::create(false, false, candidate.copyRef()));
    });
}

void PeerConnectionBackendQt5WebRTC::didChangeSignalingState(WRTCInt::SignalingState state)
{
    ASSERT(m_client);
    PeerConnectionStates::SignalingState signalingState = PeerConnectionStates::SignalingState::Stable;
    switch(state)
    {
        case WRTCInt::Stable:
            signalingState = PeerConnectionStates::SignalingState::Stable;
            break;
        case WRTCInt::HaveLocalOffer:
            signalingState = PeerConnectionStates::SignalingState::HaveLocalOffer;
            break;
        case WRTCInt::HaveRemoteOffer:
            signalingState = PeerConnectionStates::SignalingState::HaveRemoteOffer;
            break;
        case WRTCInt::HaveLocalPrAnswer:
            signalingState = PeerConnectionStates::SignalingState::HaveLocalPrAnswer;
            break;
        case WRTCInt::HaveRemotePrAnswer:
            signalingState = PeerConnectionStates::SignalingState::HaveRemotePrAnswer;
            break;
        case WRTCInt::Closed:
            signalingState = PeerConnectionStates::SignalingState::Closed;
            break;
        default:
            return;
    }
    m_client->setSignalingState(signalingState);
}

void PeerConnectionBackendQt5WebRTC::didChangeIceGatheringState(WRTCInt::IceGatheringState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceGatheringState iceGatheringState = PeerConnectionStates::IceGatheringState::New;
    switch(state)
    {
        case WRTCInt::IceGatheringNew:
            iceGatheringState = PeerConnectionStates::IceGatheringState::New;
            break;
        case WRTCInt::IceGatheringGathering:
            iceGatheringState = PeerConnectionStates::IceGatheringState::Gathering;
            break;
        case WRTCInt::IceGatheringComplete:
            iceGatheringState = PeerConnectionStates::IceGatheringState::Complete;
            break;
        default:
            return;
    }
    m_client->updateIceGatheringState(iceGatheringState);
}

void PeerConnectionBackendQt5WebRTC::didChangeIceConnectionState(WRTCInt::IceConnectionState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceConnectionState iceConnectionState = PeerConnectionStates::IceConnectionState::New;
    switch(state)
    {
        case WRTCInt::IceConnectionNew:
            iceConnectionState = PeerConnectionStates::IceConnectionState::New;
            break;
        case WRTCInt::IceConnectionChecking:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Checking;
            break;
        case WRTCInt::IceConnectionConnected:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Connected;
            break;
        case WRTCInt::IceConnectionCompleted:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Completed;
            break;
        case WRTCInt::IceConnectionFailed:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Failed;
            break;
        case WRTCInt::IceConnectionDisconnected:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Disconnected;
            break;
        case WRTCInt::IceConnectionClosed:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Closed;
            break;
        default:
            return;
    }
    m_client->updateIceConnectionState(iceConnectionState);
}


}

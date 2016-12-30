#include "config.h"

#include "PeerConnectionBackendWebRtcOrg.h"

#include "DOMError.h"
#include "EventNames.h"
#include "JSDOMError.h"
#include "JSRTCSessionDescription.h"
#include "JSRTCStatsResponse.h"
#include "Logging.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamEvent.h"
#include "MediaStream.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackSourcesRequestClient.h"
#include "RTCConfiguration.h"
#include "RTCDataChannelEvent.h"
#include "RTCDataChannelHandlerClient.h"
#include "RTCDataChannelHandler.h"
#include "RTCIceCandidateEvent.h"
#include "RTCIceCandidate.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCRtpTransceiver.h"
#include "RTCSessionDescription.h"
#include "RTCStatsReport.h"
#include "RTCStatsResponse.h"
#include "ScriptExecutionContext.h"
#include "UUID.h"
#include "WebRtcOrgUtils.h"

#include <webrtc/api/datachannelinterface.h>
#include <webrtc/api/jsep.h>
#include <webrtc/api/peerconnectioninterface.h>
#include <webrtc/api/statstypes.h>
#include <webrtc/media/base/videocapturerfactory.h>

namespace WebCore {

static int generateNextId()
{
    static int gRequestId = 0;
    if (gRequestId == std::numeric_limits<int>::max()) {
        gRequestId = 0;
    }
    return ++gRequestId;
}

static bool parseSdpTypeString(const std::string& string, RTCSessionDescription::SdpType& outType)
{
    if (string == "offer")
        outType = RTCSessionDescription::SdpType::Offer;
    else if (string == "pranswer")
        outType = RTCSessionDescription::SdpType::Pranswer;
    else if (string == "answer")
        outType = RTCSessionDescription::SdpType::Answer;
    else if (string == "rollback")
        outType = RTCSessionDescription::SdpType::Rollback;
    else {
        ASSERT_NOT_REACHED();
        return false;
    }
    return true;
}

static std::string sdpTypeToString(const RTCSessionDescription::SdpType& type)
{
    switch (type) {
    case RTCSessionDescription::SdpType::Offer:
        return "offer";
    case RTCSessionDescription::SdpType::Pranswer:
        return "pranswer";
    case RTCSessionDescription::SdpType::Answer:
        return "answer";
    case RTCSessionDescription::SdpType::Rollback:
        return "rollback";
    }
    ASSERT_NOT_REACHED();
    return "";
}

static std::unique_ptr<PeerConnectionBackend> createPeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient* client)
{
    return std::unique_ptr<PeerConnectionBackend>(new PeerConnectionBackendWebRtcOrg(client));
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createPeerConnectionBackendWebRtcOrg;

void enableWebRtcOrgPeerConnectionBackend()
{
    WebRtcOrgUtils::instance();
    PeerConnectionBackend::create = createPeerConnectionBackendWebRtcOrg;
}

PeerConnectionBackendWebRtcOrg::PeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient* client)
    : m_client(client)
{
    m_peerConnectionFactory = WebRtcOrgUtils::instance().getPeerConnectionFactory();
}

class WRTCSetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver
{
    PeerConnectionBackendWebRtcOrg* m_backend;
    int m_requestId;
public:
    WRTCSetSessionDescriptionObserver(PeerConnectionBackendWebRtcOrg* backend, int id)
        : m_backend(backend)
        , m_requestId(id)
    { }
    void OnSuccess() override
    {
        m_backend->requestSucceeded(m_requestId);
    }
    void OnFailure(const std::string& error) override
    {
        RELEASE_LOG_ERROR("%s", error.c_str());
        m_backend->requestFailed(m_requestId, error);
    }
};

class WRTCCreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver
{
    PeerConnectionBackendWebRtcOrg* m_backend;
    int m_requestId;
public:
    WRTCCreateSessionDescriptionObserver(PeerConnectionBackendWebRtcOrg* backend, int id)
        : m_backend(backend)
        , m_requestId(id)
    { }
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override
    {
        std::unique_ptr<webrtc::SessionDescriptionInterface> holder(desc);
        std::string descStr;
        RTCSessionDescription::SdpType sdpType;
        if (!desc->ToString(&descStr) || !parseSdpTypeString(desc->type(), sdpType)) {
            std::string error = "Failed to get session description string";
            OnFailure(error);
            return;
        }
        RefPtr<RTCSessionDescription> sessionDescription =
            RTCSessionDescription::create(sdpType, descStr.c_str());
        m_backend->requestSucceeded(m_requestId, sessionDescription.releaseNonNull());
    }
    void OnFailure(const std::string& error) override
    {
        RELEASE_LOG_ERROR("%s", error.c_str());
        m_backend->requestFailed(m_requestId, error);
    }
};

void PeerConnectionBackendWebRtcOrg::createOffer(RTCOfferOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions wrtcOptions;
    wrtcOptions.offer_to_receive_video = options.offerToReceiveVideo();
    wrtcOptions.offer_to_receive_audio = options.offerToReceiveAudio();
    wrtcOptions.voice_activity_detection = options.voiceActivityDetection();
    wrtcOptions.ice_restart = options.iceRestart();

    int requestId = generateNextId();
    m_sessionDescriptionRequestId = requestId;
    m_sessionDescriptionPromise = WTFMove(promise);

    webrtc::CreateSessionDescriptionObserver* observer =
        new rtc::RefCountedObject<WRTCCreateSessionDescriptionObserver>(this, requestId);
    m_peerConnection->CreateOffer(observer, wrtcOptions);
}

void PeerConnectionBackendWebRtcOrg::createAnswer(RTCAnswerOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions wrtcOptions;
    wrtcOptions.voice_activity_detection = options.voiceActivityDetection();

    int requestId = generateNextId();
    m_sessionDescriptionRequestId = requestId;
    m_sessionDescriptionPromise = WTFMove(promise);

    webrtc::CreateSessionDescriptionObserver* observer =
        new rtc::RefCountedObject<WRTCCreateSessionDescriptionObserver>(this, requestId);
    m_peerConnection->CreateAnswer(observer, wrtcOptions);
}

void PeerConnectionBackendWebRtcOrg::setLocalDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    webrtc::SdpParseError error;
    webrtc::SessionDescriptionInterface* wrtcDesc = webrtc::CreateSessionDescription(
        sdpTypeToString(desc.type()),
        desc.sdp().utf8().data(),
        &error);
    if (!wrtcDesc) {
        RELEASE_LOG_ERROR("Failed to create session description, error=%s line=%s",
                          error.description.c_str(), error.line.c_str());
        m_voidRequestId = InvalidRequestId;
        return;
    }

    int requestId = generateNextId();
    m_voidRequestId = requestId;
    m_voidPromise = WTFMove(promise);

    webrtc::SetSessionDescriptionObserver* observer =
        new rtc::RefCountedObject<WRTCSetSessionDescriptionObserver>(this, requestId);
    m_peerConnection->SetLocalDescription(observer, wrtcDesc);
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::localDescription() const
{
    std::string sdpString;
    RTCSessionDescription::SdpType sdpType;
    const webrtc::SessionDescriptionInterface* localDesc = m_peerConnection->local_description();
    if (localDesc != nullptr &&
        localDesc->ToString(&sdpString) &&
        parseSdpTypeString(localDesc->type(), sdpType)) {
        return RTCSessionDescription::create(sdpType, String(sdpString.c_str()));
    }
    RELEASE_LOG_ERROR("Failed to get local description string");
    return nullptr;
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::currentLocalDescription() const
{
    return localDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::pendingLocalDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendWebRtcOrg::setRemoteDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    webrtc::SdpParseError error;
    webrtc::SessionDescriptionInterface* wrtcDesc = webrtc::CreateSessionDescription(
        sdpTypeToString(desc.type()),
        desc.sdp().utf8().data(),
        &error);
    if (!wrtcDesc) {
        RELEASE_LOG_ERROR("Failed to create session description, error=%s line=%s",
                          error.description.c_str(), error.line.c_str());
        m_voidRequestId = InvalidRequestId;
        return;
    }

    int requestId = generateNextId();
    m_voidRequestId = requestId;
    m_voidPromise = WTFMove(promise);

    webrtc::SetSessionDescriptionObserver* observer =
        new rtc::RefCountedObject<WRTCSetSessionDescriptionObserver>(this, requestId);
    m_peerConnection->SetRemoteDescription(observer, wrtcDesc);
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::remoteDescription() const
{
    std::string sdpString;
    RTCSessionDescription::SdpType sdpType;
    const webrtc::SessionDescriptionInterface* remoteDesc = m_peerConnection->remote_description();
    if (remoteDesc != nullptr &&
        remoteDesc->ToString(&sdpString) &&
        parseSdpTypeString(remoteDesc->type(), sdpType)) {
        return RTCSessionDescription::create(sdpType, String(sdpString.c_str()));
    }
    RELEASE_LOG_ERROR("Failed to get remote description");
    return nullptr;
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::currentRemoteDescription() const
{
    return remoteDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::pendingRemoteDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendWebRtcOrg::setConfiguration(RTCConfiguration& rtcConfig)
{
    webrtc::PeerConnectionInterface::RTCConfiguration configuration;
    for (auto& server : rtcConfig.iceServers()) {
        webrtc::PeerConnectionInterface::IceServer iceServer;
        iceServer.username = server->credential().utf8().data();
        iceServer.password = server->username().utf8().data();
        for (auto& url : server->urls()) {
            iceServer.urls.push_back(url.utf8().data());
        }
        configuration.servers.push_back(iceServer);
    }

    if (m_peerConnection == nullptr) {
        m_peerConnection = m_peerConnectionFactory->CreatePeerConnection(
            configuration, nullptr, nullptr, this);
        if (m_peerConnection == nullptr) {
            RELEASE_LOG_ERROR("Failed to create new peer connection");
        }
    } else if (!m_peerConnection->SetConfiguration(configuration)) {
        RELEASE_LOG_ERROR("Failed to update peer connection configuration");
    }
}

void PeerConnectionBackendWebRtcOrg::addIceCandidate(RTCIceCandidate& candidate, PeerConnection::VoidPromise&& promise)
{
    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> iceCandidate(
        webrtc::CreateIceCandidate(
            candidate.sdpMid().utf8().data(),
            candidate.sdpMLineIndex().valueOr(0),
            candidate.candidate().utf8().data(),
            &error));
    bool res = iceCandidate != nullptr && m_peerConnection->AddIceCandidate(iceCandidate.get());
    if (res) {
        promise.resolve(nullptr);
    } else {
        RELEASE_LOG_ERROR("Failed to add ICE candidate, error=%s line=%s",
                          error.description.c_str(), error.line.c_str());
        promise.reject(DOMError::create("Failed to add ICECandidate"));
    }
}

class WRTCStatsObserver : public webrtc::StatsObserver
{
    PeerConnectionBackendWebRtcOrg* m_backend;
    unsigned int m_requestId;
public:
    WRTCStatsObserver(PeerConnectionBackendWebRtcOrg* backend, unsigned int id)
        : m_backend(backend)
        , m_requestId(id)
    {  }
    void OnComplete(const webrtc::StatsReports& reports) override
    {
        m_backend->requestSucceeded(m_requestId, reports);
    }
};

void PeerConnectionBackendWebRtcOrg::getStats(MediaStreamTrack* track, PeerConnection::StatsPromise&& promise)
{
    UNUSED_PARAM(track);
    int requestId = generateNextId();
    rtc::scoped_refptr<WRTCStatsObserver> observer(
        new rtc::RefCountedObject<WRTCStatsObserver>(this, requestId));
    webrtc::PeerConnectionInterface::StatsOutputLevel level =
        webrtc::PeerConnectionInterface::kStatsOutputLevelStandard;
    bool res = m_peerConnection->GetStats(observer, nullptr, level);
    if (res) {
        m_statsPromises.add(requestId, WTFMove(promise));
    } else {
        promise.reject(DOMError::create("Failed to get stats"));
    }
}

Vector<RefPtr<MediaStream>> PeerConnectionBackendWebRtcOrg::getRemoteStreams() const
{
    return m_remoteStreams;
}

RefPtr<RTCRtpReceiver> PeerConnectionBackendWebRtcOrg::createReceiver(const String&, const String&, const String&)
{
    return nullptr;
}

void PeerConnectionBackendWebRtcOrg::replaceTrack(RTCRtpSender&, RefPtr<MediaStreamTrack>&&, PeerConnection::VoidPromise&& promise)
{
    notImplemented();
    promise.reject(DOMError::create("NotSupportedError"));
}

void PeerConnectionBackendWebRtcOrg::stop()
{
    m_peerConnection->Close();
}

bool PeerConnectionBackendWebRtcOrg::isNegotiationNeeded() const
{
    return m_isNegotiationNeeded;
}

void PeerConnectionBackendWebRtcOrg::markAsNeedingNegotiation()
{
    // TODO: add only once
    const Vector<RefPtr<RTCRtpTransceiver>>& transceivers = m_client->getTransceivers();
    for(auto &transceiver : transceivers) {
        if (!transceiver)
            continue;
        RTCRtpSender* sender = transceiver->sender();
        if (!sender || !sender->track())
            continue;
        RealtimeMediaSource& source = sender->track()->source();
        webrtc::MediaStreamInterface* stream =
            static_cast<RealtimeMediaSourceWebRtcOrg&>(source).rtcStream();
        if (stream) {
            m_peerConnection->AddStream(stream);
            break;
        }
    }
}

void PeerConnectionBackendWebRtcOrg::clearNegotiationNeededState()
{
    m_isNegotiationNeeded = false;
}

std::unique_ptr<RTCDataChannelHandler> PeerConnectionBackendWebRtcOrg::createDataChannel(const String& label, const Dictionary& options)
{
    webrtc::DataChannelInit initData;
    String maxRetransmitsStr;
    String maxRetransmitTimeStr;
    String protocolStr;
    options.get("ordered", initData.ordered);
    options.get("negotiated", initData.negotiated);
    options.get("id", initData.id);
    options.get("maxRetransmits", maxRetransmitsStr);
    options.get("maxRetransmitTime", maxRetransmitTimeStr);
    options.get("protocol", protocolStr);
    initData.protocol = protocolStr.utf8().data();
    bool maxRetransmitsConversion;
    bool maxRetransmitTimeConversion;
    initData.maxRetransmits = maxRetransmitsStr.toUIntStrict(&maxRetransmitsConversion);
    initData.maxRetransmitTime = maxRetransmitTimeStr.toUIntStrict(&maxRetransmitTimeConversion);
    if (maxRetransmitsConversion && maxRetransmitTimeConversion) {
        return nullptr;
    }
    rtc::scoped_refptr<webrtc::DataChannelInterface> channel = m_peerConnection->CreateDataChannel(label.utf8().data(), &initData);
    if (channel != nullptr) {
        return std::make_unique<RTCDataChannelHandlerWebRtcOrg>(channel.get());
    }
    return nullptr;
}

void PeerConnectionBackendWebRtcOrg::OnAddStream(webrtc::MediaStreamInterface* stream)
{
    ASSERT(m_client);
    ASSERT(stream);

    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;

    for (const auto& track : stream->GetAudioTracks()) {
        String name(track->id().c_str());
        String id(createCanonicalUUIDString());
        RefPtr<RealtimeMediaSourceWebRtcOrg> audioSource = adoptRef(new RealtimeAudioSourceWebRtcOrg(id, name));
        audioSource->setRTCStream(stream);
        audioSources.append(audioSource.release());
    }
    for (const auto& track : stream->GetVideoTracks()) {
        String name(track->id().c_str());
        String id(createCanonicalUUIDString());
        RefPtr<RealtimeMediaSourceWebRtcOrg> videoSource = adoptRef(new RealtimeVideoSourceWebRtcOrg(id, name));
        videoSource->setRTCStream(stream);
        videoSources.append(videoSource.release());
    }
    String id = stream->label().c_str();
    RefPtr<MediaStreamPrivate> privateStream = MediaStreamPrivate::create(id, audioSources, videoSources);
    RefPtr<MediaStream> mediaStream = MediaStream::create(*m_client->scriptExecutionContext(), privateStream.copyRef());
    privateStream->startProducingData();

    m_remoteStreams.append(mediaStream);
    m_client->scriptExecutionContext()->postTask([=](ScriptExecutionContext&) {
        m_client->fireEvent(
            MediaStreamEvent::create(
                eventNames().addstreamEvent, false, false, mediaStream.copyRef()));
    });
}

void PeerConnectionBackendWebRtcOrg::OnRemoveStream(webrtc::MediaStreamInterface* /*stream*/)
{
    notImplemented();
}

void PeerConnectionBackendWebRtcOrg::OnRenegotiationNeeded()
{
    m_isNegotiationNeeded = true;
    m_client->scheduleNegotiationNeededEvent();
}

void PeerConnectionBackendWebRtcOrg::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceConnectionState iceConnectionState = PeerConnectionStates::IceConnectionState::New;
    switch (state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
        iceConnectionState = PeerConnectionStates::IceConnectionState::New;
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
        iceConnectionState = PeerConnectionStates::IceConnectionState::Checking;
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
        iceConnectionState = PeerConnectionStates::IceConnectionState::Connected;
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
        iceConnectionState = PeerConnectionStates::IceConnectionState::Completed;
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
        iceConnectionState = PeerConnectionStates::IceConnectionState::Failed;
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
        iceConnectionState = PeerConnectionStates::IceConnectionState::Disconnected;
        break;
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
        iceConnectionState = PeerConnectionStates::IceConnectionState::Closed;
        break;
    default:
        return;
    }
    m_client->updateIceConnectionState(iceConnectionState);
}

void PeerConnectionBackendWebRtcOrg::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceGatheringState iceGatheringState = PeerConnectionStates::IceGatheringState::New;
    switch (state) {
    case webrtc::PeerConnectionInterface::kIceGatheringNew:
        iceGatheringState = PeerConnectionStates::IceGatheringState::New;
        break;
    case webrtc::PeerConnectionInterface::kIceGatheringGathering:
        iceGatheringState = PeerConnectionStates::IceGatheringState::Gathering;
        break;
    case webrtc::PeerConnectionInterface::kIceGatheringComplete:
        iceGatheringState = PeerConnectionStates::IceGatheringState::Complete;
        break;
    default:
        return;
    }
    m_client->updateIceGatheringState(iceGatheringState);
}

void PeerConnectionBackendWebRtcOrg::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState state)
{
    ASSERT(m_client);
    PeerConnectionStates::SignalingState signalingState = PeerConnectionStates::SignalingState::Stable;
    switch (state) {
    case webrtc::PeerConnectionInterface::kStable:
        signalingState = PeerConnectionStates::SignalingState::Stable;
        break;
    case webrtc::PeerConnectionInterface::kHaveLocalOffer:
        signalingState = PeerConnectionStates::SignalingState::HaveLocalOffer;
        break;
    case webrtc::PeerConnectionInterface::kHaveRemoteOffer:
        signalingState = PeerConnectionStates::SignalingState::HaveRemoteOffer;
        break;
    case webrtc::PeerConnectionInterface::kHaveLocalPrAnswer:
        signalingState = PeerConnectionStates::SignalingState::HaveLocalPrAnswer;
        break;
    case webrtc::PeerConnectionInterface::kHaveRemotePrAnswer:
        signalingState = PeerConnectionStates::SignalingState::HaveRemotePrAnswer;
        break;
    case webrtc::PeerConnectionInterface::kClosed:
        signalingState = PeerConnectionStates::SignalingState::Closed;
        break;
    default:
        return;
    }
    m_client->setSignalingState(signalingState);
}

void PeerConnectionBackendWebRtcOrg::OnIceCandidate(const webrtc::IceCandidateInterface* iceCandidate)
{
    ASSERT(m_client);
    std::string sdpString;
    if (iceCandidate == nullptr || !iceCandidate->ToString(&sdpString)) {
        RELEASE_LOG_ERROR("Failed to get ICE candidate string");
        return;
    }

    String sdp = sdpString.c_str();
    String sdpMid = iceCandidate->sdp_mid().c_str();
    int sdpMLineIndex = iceCandidate->sdp_mline_index();
    RefPtr<RTCIceCandidate> candidate = RTCIceCandidate::create(sdp, sdpMid, sdpMLineIndex);
    m_client->scriptExecutionContext()->postTask([this, candidate](ScriptExecutionContext&) {
        m_client->fireEvent(RTCIceCandidateEvent::create(false, false, candidate.copyRef()));
    });
}

void PeerConnectionBackendWebRtcOrg::OnDataChannel(webrtc::DataChannelInterface* channel)
{
    ASSERT(m_client);
    std::unique_ptr<RTCDataChannelHandler> handler =
        std::make_unique<RTCDataChannelHandlerWebRtcOrg>(channel);
    RefPtr<RTCDataChannel> dataChannel = RTCDataChannel::create(m_client->scriptExecutionContext(), WTFMove(handler));
    if (!dataChannel) {
        RELEASE_LOG_ERROR("Failed to create RTCDataChannel");
        return;
    }
    m_remoteDataChannels.append(dataChannel);
    m_client->scriptExecutionContext()->postTask([=](ScriptExecutionContext&) {
        m_client->fireEvent(RTCDataChannelEvent::create(eventNames().datachannelEvent, false, false, *dataChannel.get()));
    });
}

void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id, Ref<RTCSessionDescription>&& desc)
{
    ASSERT(id == m_sessionDescriptionRequestId);
    ASSERT(m_sessionDescriptionPromise);
    m_sessionDescriptionPromise->resolve(WTFMove(desc));
    m_sessionDescriptionPromise = WTF::Nullopt;
    m_sessionDescriptionRequestId = InvalidRequestId;
}

void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id, const webrtc::StatsReports& reports)
{
    Optional<PeerConnection::StatsPromise> statsPromise = m_statsPromises.take(id);
    if (!statsPromise) {
        RELEASE_LOG_ERROR("Couldn't find promise for stats request: %d", id);
        return;
    }

    Ref<RTCStatsResponse> response = RTCStatsResponse::create();
    for (auto& r : reports) {
        String id = r->id()->ToString().c_str();
        String type = r->TypeToString();
        double timestamp = r->timestamp();
        size_t idx = response->addReport(id, type, timestamp);
        for (auto& v : r->values()) {
            response->addStatistic(idx, r->FindValue(v.first)->display_name(), v.second.get()->ToString().c_str());
        }
    }

    statsPromise->resolve(WTFMove(response));
}

void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id)
{
    ASSERT(id == m_voidRequestId);
    ASSERT(m_voidPromise);

    m_voidPromise->resolve(nullptr);

    m_voidRequestId = WebCore::InvalidRequestId;
    m_voidPromise = WTF::Nullopt;
}

void PeerConnectionBackendWebRtcOrg::requestFailed(int id, const std::string& error)
{
    if (id == m_voidRequestId) {
        ASSERT(m_voidPromise);
        ASSERT(!m_sessionDescriptionPromise);
        m_voidPromise->reject(DOMError::create(error.c_str()));
        m_voidPromise = WTF::Nullopt;
        m_voidRequestId = WebCore::InvalidRequestId;
    } else if (id == m_sessionDescriptionRequestId) {
        ASSERT(m_sessionDescriptionPromise);
        ASSERT(!m_voidPromise);
        m_sessionDescriptionPromise->reject(DOMError::create(error.c_str()));
        m_sessionDescriptionPromise = WTF::Nullopt;
        m_sessionDescriptionRequestId = WebCore::InvalidRequestId;
    } else {
        ASSERT_NOT_REACHED();
    }
}

RTCDataChannelHandlerWebRtcOrg::RTCDataChannelHandlerWebRtcOrg(webrtc::DataChannelInterface* dataChannel)
    : m_client(nullptr)
    , m_rtcDataChannel(dataChannel)
    , m_observer(new rtc::RefCountedObject<RTCDataChannelObserver>(this))
{
}

RTCDataChannelHandlerWebRtcOrg::~RTCDataChannelHandlerWebRtcOrg()
{
    closeDataChannel();
}

void RTCDataChannelHandlerWebRtcOrg::setClient(RTCDataChannelHandlerClient* client)
{
    if (m_client == client)
        return;

    m_client = client;

    if (m_client != nullptr) {
        m_rtcDataChannel->RegisterObserver(m_observer.get());
    } else {
        m_rtcDataChannel->UnregisterObserver();
    }
}

void RTCDataChannelHandlerWebRtcOrg::onStateChange()
{
    ASSERT(m_client);
    webrtc::DataChannelInterface::DataState state = m_rtcDataChannel->state();
    LOG(Media, "DataChannel id=%d state=%s",
        m_rtcDataChannel->id(), webrtc::DataChannelInterface::DataStateString(state));

    RTCDataChannelHandlerClient::ReadyState readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
    switch (state) {
    case webrtc::DataChannelInterface::kConnecting:
        readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
        break;
    case webrtc::DataChannelInterface::kOpen:
        readyState = RTCDataChannelHandlerClient::ReadyStateOpen;
        break;
    case webrtc::DataChannelInterface::kClosing:
        readyState = RTCDataChannelHandlerClient::ReadyStateClosing;
        break;
    case webrtc::DataChannelInterface::kClosed:
        readyState = RTCDataChannelHandlerClient::ReadyStateClosed;
        break;
    default:
        break;
    };
    m_client->didChangeReadyState(readyState);
}

void RTCDataChannelHandlerWebRtcOrg::onMessage(const webrtc::DataBuffer& buffer)
{
    ASSERT(m_client);
    const char* data = buffer.data.data<char>();
    size_t sz = buffer.data.size();
    if (buffer.binary) {
        m_client->didReceiveRawData(data, sz);
    } else {
        m_client->didReceiveStringData(std::string(data, sz).c_str());
    }
}

void RTCDataChannelHandlerWebRtcOrg::closeDataChannel()
{
    if (m_rtcDataChannel) {
        m_rtcDataChannel->UnregisterObserver();
        m_rtcDataChannel->Close();
        m_rtcDataChannel = nullptr;
    }
}

String RTCDataChannelHandlerWebRtcOrg::label()
{
    return m_rtcDataChannel->label().c_str();
}

bool RTCDataChannelHandlerWebRtcOrg::ordered()
{
    return m_rtcDataChannel->ordered();
}

unsigned short RTCDataChannelHandlerWebRtcOrg::maxRetransmitTime()
{
    return m_rtcDataChannel->maxRetransmitTime();
}

unsigned short RTCDataChannelHandlerWebRtcOrg::maxRetransmits()
{
    return m_rtcDataChannel->maxRetransmits();
}

String RTCDataChannelHandlerWebRtcOrg::protocol()
{
    return m_rtcDataChannel->protocol().c_str();
}

bool RTCDataChannelHandlerWebRtcOrg::negotiated()
{
    return m_rtcDataChannel->negotiated();
}

unsigned short RTCDataChannelHandlerWebRtcOrg::id()
{
    return m_rtcDataChannel->id();
}

unsigned long RTCDataChannelHandlerWebRtcOrg::bufferedAmount()
{
    return m_rtcDataChannel->buffered_amount();
}

bool RTCDataChannelHandlerWebRtcOrg::sendStringData(const String& str)
{
    rtc::CopyOnWriteBuffer buffer(str.utf8().data(), str.length());
    return m_rtcDataChannel->Send(webrtc::DataBuffer(buffer, false));
}

bool RTCDataChannelHandlerWebRtcOrg::sendRawData(const char* data, size_t size)
{
    rtc::CopyOnWriteBuffer buffer(data, size);
    return m_rtcDataChannel->Send(webrtc::DataBuffer(buffer, true));
}

void RTCDataChannelHandlerWebRtcOrg::close()
{
    m_rtcDataChannel->Close();
}

void RTCDataChannelHandlerWebRtcOrg::didReceiveStringData(const std::string& str)
{
    m_client->didReceiveStringData(str.c_str());
}

void RTCDataChannelHandlerWebRtcOrg::didReceiveRawData(const char* data, size_t sz)
{
    m_client->didReceiveRawData(data, sz);
}

}  // namespace WebCore

#include "config.h"
#include "PeerConnectionBackendWebRtcOrg.h"
#include "Logging.h"

#include "DOMError.h"
#include "JSDOMError.h"
#include "JSRTCSessionDescription.h"
#include "JSRTCStatsResponse.h"
#include "MediaStream.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackSourcesRequestClient.h"
#include "RTCConfiguration.h"
#include "RTCDataChannelHandlerClient.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpSender.h"
#include "RTCSessionDescription.h"
#include "RTCStatsReport.h"
#include "RTCStatsResponse.h"
#include "ScriptExecutionContext.h"
#include "UUID.h"

#include "webrtc/api/datachannelinterface.h"
#include "webrtc/api/jsep.h"
#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/api/statstypes.h"

#include "webrtc/media/base/videocapturerfactory.h"

#include "WebRtcOrgUtils.h"

namespace WebCore {

using namespace PeerConnection;

int generateNextId()
{
    static int gRequestId = 0;
    if (gRequestId == std::numeric_limits<int>::max()) {
        gRequestId = 0;
    }
    return ++gRequestId;
}

static std::unique_ptr<PeerConnectionBackend> createPeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient* client)
{
    WebRtcOrgUtils::getInstance()->initializeWebRtcOrg();
    return std::unique_ptr<PeerConnectionBackend>(new PeerConnectionBackendWebRtcOrg(client));
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createPeerConnectionBackendWebRtcOrg;

void enableWebRtcOrgPeerConnectionBackend()
{
    PeerConnectionBackend::create = createPeerConnectionBackendWebRtcOrg;
}

PeerConnectionBackendWebRtcOrg::PeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient* client)
    : m_client(client)
{
    WebRtcOrgUtils* instance = WebRtcOrgUtils::getInstance();
    m_peerConnectionFactory = instance->getPeerConnectionFactory();
}

class WRTCSetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
    PeerConnectionBackendWebRtcOrg* m_backend;
    int m_requestId;

public:
    WRTCSetSessionDescriptionObserver(PeerConnectionBackendWebRtcOrg* backend, int id)
        : m_backend(backend)
        , m_requestId(id)
    {
    }

    void OnSuccess() override
    {
        m_backend->requestSucceeded(m_requestId);
    }

    void OnFailure(const std::string& error) override
    {
        LOG(Media, "%s", error.c_str());
        m_backend->requestFailed(m_requestId, error);
    }
};

class WRTCCreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver {
    PeerConnectionBackendWebRtcOrg* m_backend;
    int m_requestId;

public:
    WRTCCreateSessionDescriptionObserver(PeerConnectionBackendWebRtcOrg* backend, int id)
        : m_backend(backend)
        , m_requestId(id)
    {
    }

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override
    {
        std::unique_ptr<webrtc::SessionDescriptionInterface> holder(desc);
        std::string descStr;
        if (!desc->ToString(&descStr)) {
            std::string error = "Failed to get session description string";
            OnFailure(error);
            return;
        }
        RefPtr<RTCSessionDescription> sessionDescription = RTCSessionDescription::create(desc->type().c_str(), descStr.c_str());

        m_backend->requestSucceeded(m_requestId, sessionDescription);
    }
    void OnFailure(const std::string& error) override
    {
        LOG(Media, "%s", error.c_str());
        m_backend->requestFailed(m_requestId, error);
    }
};

/*
 * Implementation of webrtc::MediaConstraintsInterface
 */
class WRTCMediaConstraints : public webrtc::MediaConstraintsInterface {
    webrtc::MediaConstraintsInterface::Constraints m_mandatory;
    webrtc::MediaConstraintsInterface::Constraints m_optional;

public:
    explicit WRTCMediaConstraints(const MediaConstraints& constraints)
    {
        Vector<MediaConstraint> mediaConstraints;
        constraints.getMandatoryConstraints(mediaConstraints);

        for (auto& c : mediaConstraints) {
            std::string name = c.m_name.utf8().data();
            std::string value = c.m_value.utf8().data();
            m_mandatory.push_back(webrtc::MediaConstraintsInterface::Constraint(name, value));
        }

        mediaConstraints.clear();
        constraints.getOptionalConstraints(mediaConstraints);
        for (auto& c : mediaConstraints) {
            std::string name = c.m_name.utf8().data();
            std::string value = c.m_value.utf8().data();
            m_optional.push_back(webrtc::MediaConstraintsInterface::Constraint(name, value));
        }
    }

    const MediaConstraintsInterface::Constraints& GetMandatory() const override
    {
        return m_mandatory;
    }

    const MediaConstraintsInterface::Constraints& GetOptional() const override
    {
        return m_optional;
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

    int id = generateNextId();

    webrtc::CreateSessionDescriptionObserver* observer = new rtc::RefCountedObject<WRTCCreateSessionDescriptionObserver>(this, id);
    m_peerConnection->CreateOffer(observer, wrtcOptions);
    m_sessionDescriptionRequestId = id;
    m_sessionDescriptionPromise = WTFMove(promise);
}

void PeerConnectionBackendWebRtcOrg::createAnswer(RTCAnswerOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);
    int requestId = generateNextId();

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions wrtcOptions;
    wrtcOptions.voice_activity_detection = options.voiceActivityDetection();
    webrtc::CreateSessionDescriptionObserver* observer = new rtc::RefCountedObject<WRTCCreateSessionDescriptionObserver>(this, requestId);

    m_peerConnection->CreateAnswer(observer, wrtcOptions);

    m_sessionDescriptionRequestId = requestId;

    m_sessionDescriptionRequestId = requestId;
    m_sessionDescriptionPromise = WTFMove(promise);
}

void PeerConnectionBackendWebRtcOrg::setLocalDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    UNUSED_PARAM(promise);
    ASSERT(InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);
    int requestId = generateNextId();
    webrtc::SdpParseError error;
    webrtc::SetSessionDescriptionObserver* observer = new rtc::RefCountedObject<WRTCSetSessionDescriptionObserver>(this, requestId);

    webrtc::SessionDescriptionInterface* wrtcDesc = webrtc::CreateSessionDescription(
        desc.type().utf8().data(),
        desc.sdp().utf8().data(),
        &error);
    if (!wrtcDesc) {
        LOG(Media, "Failed to create session description, error=%s line=%s", error.description.c_str(), error.line.c_str());
        m_voidRequestId = InvalidRequestId;
        return;
    }

    m_peerConnection->SetLocalDescription(observer, wrtcDesc);
    m_voidRequestId = requestId;
    m_voidPromise = WTFMove(promise);
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::localDescription() const
{
    // TODO: pendingLocalDescription/currentLocalDescription
    const webrtc::SessionDescriptionInterface* wrtcDesc = m_peerConnection->local_description();
    std::string sdp;
    if (!wrtcDesc->ToString(&sdp)) {
        LOG(Media, "Failed to get local description string");
        return nullptr;
    }
    String type = wrtcDesc->type().c_str();
    String sdpS = sdp.c_str();
    return RTCSessionDescription::create(type, sdpS);
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
    int requestId = generateNextId();
    webrtc::SdpParseError error;
    webrtc::SetSessionDescriptionObserver* observer = new rtc::RefCountedObject<WRTCSetSessionDescriptionObserver>(this, requestId);

    webrtc::SessionDescriptionInterface* wrtcDesc = webrtc::CreateSessionDescription(
        desc.type().utf8().data(),
        desc.sdp().utf8().data(),
        &error);
    if (!wrtcDesc) {
        LOG(Media, "Failed to create session description, error=%s line=%s", error.description.c_str(), error.line.c_str());
        m_voidRequestId = InvalidRequestId;
        return;
    }

    m_peerConnection->SetRemoteDescription(observer, wrtcDesc);
    m_voidRequestId = requestId;
    m_voidPromise = WTFMove(promise);
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::remoteDescription() const
{
    // TODO: pendingRemoteDescription/currentRemoteDescription
    const webrtc::SessionDescriptionInterface* wrtcDesc = m_peerConnection->remote_description();
    std::string sdp;
    if (!wrtcDesc->ToString(&sdp)) {
        LOG(Media, "Failed to get remote description");
        return nullptr;
    }
    String type = wrtcDesc->type().c_str();
    String sdpS = sdp.c_str();
    return RTCSessionDescription::create(type, sdpS);
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::currentRemoteDescription() const
{
    return remoteDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::pendingRemoteDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendWebRtcOrg::setConfiguration(RTCConfiguration& rtcConfig, const MediaConstraints& constraints)
{
    webrtc::PeerConnectionInterface::IceServers iceServers;
    for (auto& server : rtcConfig.iceServers()) {
        webrtc::PeerConnectionInterface::IceServer wrtcServer;
        wrtcServer.username = server->credential().utf8().data();
        wrtcServer.password = server->username().utf8().data();
        for (auto& url : server->urls()) {
            wrtcServer.urls.push_back(url.utf8().data());
        }
        iceServers.push_back(wrtcServer);
    }
    std::unique_ptr<WRTCMediaConstraints> cts(new WRTCMediaConstraints(constraints));
    webrtc::PeerConnectionInterface::RTCConfiguration configuration;
    configuration.servers = iceServers;
    if (m_peerConnection == nullptr) {
        webrtc::PeerConnectionInterface::RTCConfiguration configuration;
        configuration.servers = iceServers;
        m_peerConnection = m_peerConnectionFactory->CreatePeerConnection(
            configuration, cts.get(), nullptr, nullptr, this);
        if (m_peerConnection == nullptr) {
            LOG(Media, "Failed in PeerConnectionInterface::CreatePeerConnection");
            return;
        }
        if (!m_peerConnection->SetConfiguration(configuration)) {
            LOG(Media, "Failed in PeerConnectionInterface::SetConfiguration");
            return;
        }
    } else if (!m_peerConnection->UpdateIce(iceServers, cts.get())) {
        LOG(Media, "Failed in PeerConnectionInterface::UpdateIce");
    }
}

void PeerConnectionBackendWebRtcOrg::addIceCandidate(RTCIceCandidate& candidate, PeerConnection::VoidPromise&& promise)
{
    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> iceCandidate(
        webrtc::CreateIceCandidate(candidate.sdpMid().utf8().data(),
            candidate.sdpMLineIndex().valueOr(0),
            candidate.candidate().utf8().data(),
            &error));
    if (iceCandidate == nullptr) {
        LOG(Media, "Failed to add ICE candidate, error=%s line=%s", error.description.c_str(), error.line.c_str());
        return;
    }

    bool rc = m_peerConnection->AddIceCandidate(iceCandidate.get());
    if (rc) {
        promise.resolve(nullptr);
    } else {
        promise.reject(DOMError::create("Failed to add ICECandidate"));
    }
}

class WRTCStatsObserver : public webrtc::StatsObserver {
    PeerConnectionBackendWebRtcOrg* m_backend;
    unsigned int m_requestId;

public:
    WRTCStatsObserver(PeerConnectionBackendWebRtcOrg* backend, unsigned int id)
        : m_backend(backend)
        , m_requestId(id)
    {
    }

    void OnComplete(const webrtc::StatsReports& reports) override
    {
        m_backend->requestSucceeded(m_requestId, reports);
    }
};

void PeerConnectionBackendWebRtcOrg::getStats(MediaStreamTrack* track, PeerConnection::StatsPromise&& promise)
{
    UNUSED_PARAM(track);
    rtc::scoped_refptr<WRTCStatsObserver> observer(
        new rtc::RefCountedObject<WRTCStatsObserver>(this, generateNextId()));
    webrtc::PeerConnectionInterface::StatsOutputLevel level = webrtc::PeerConnectionInterface::kStatsOutputLevelStandard;

    bool id = m_peerConnection->GetStats(observer, nullptr, level);
    if (id) {
        m_statsPromises.add(id, WTFMove(promise));
    } else {
        promise.reject(DOMError::create("Failed to get stats"));
    }
}

void PeerConnectionBackendWebRtcOrg::replaceTrack(RTCRtpSender&, MediaStreamTrack&, PeerConnection::VoidPromise&& promise)
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
    Vector<RefPtr<RTCRtpSender> > senders = m_client->getSenders();
    for (auto& sender : senders) {
        RealtimeMediaSource& source = sender->track().source();
        webrtc::MediaStreamInterface* stream = static_cast<RealtimeMediaSourceWebRtcOrg&>(source).rtcStream();
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
    std::vector<std::string> audioDevices;
    for (const auto& track : stream->GetAudioTracks()) {
        audioDevices.push_back(track->id());
    }
    std::vector<std::string> videoDevices;
    for (const auto& track : stream->GetVideoTracks()) {
        videoDevices.push_back(track->id());
    }

    ASSERT(m_client);

    Vector<RefPtr<RealtimeMediaSource> > audioSources;
    Vector<RefPtr<RealtimeMediaSource> > videoSources;

    for (auto& device : audioDevices) {
        String name(device.c_str());
        String id(createCanonicalUUIDString());
        RefPtr<RealtimeMediaSourceWebRtcOrg> audioSource = adoptRef(new RealtimeAudioSourceWebRtcOrg(id, name));
        audioSource->setRTCStream(stream);
        audioSources.append(audioSource.release());
    }
    for (auto& device : videoDevices) {
        String name(device.c_str());
        String id(createCanonicalUUIDString());
        RefPtr<RealtimeMediaSourceWebRtcOrg> videoSource = adoptRef(new RealtimeVideoSourceWebRtcOrg(id, name));
        videoSource->setRTCStream(stream);
        videoSources.append(videoSource.release());
    }
    String id = stream->label().c_str();
    RefPtr<MediaStreamPrivate> privateStream = MediaStreamPrivate::create(id, audioSources, videoSources);
    RefPtr<MediaStream> mediaStream = MediaStream::create(*m_client->scriptExecutionContext(), privateStream.copyRef());
    privateStream->startProducingData();
    m_client->addRemoteStream(WTFMove(mediaStream));
}

void PeerConnectionBackendWebRtcOrg::OnRemoveStream(webrtc::MediaStreamInterface* /*stream*/)
{
    LOG(Media, "Not Implemented");
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
    std::string str;
    if (!iceCandidate->ToString(&str)) {
        LOG(Media, "Failed to get ICE candidate string");
        return;
    }

    String sdp = str.c_str();
    String sdpMid = iceCandidate->sdp_mid().c_str();
    int sdpMLineIndex = iceCandidate->sdp_mline_index();
    RefPtr<RTCIceCandidate> candidate = RTCIceCandidate::create(sdp, sdpMid, sdpMLineIndex);
    m_client->scriptExecutionContext()->postTask([this, candidate](ScriptExecutionContext&) {
        m_client->fireEvent(RTCIceCandidateEvent::create(false, false, candidate.copyRef()));
    });
}

void PeerConnectionBackendWebRtcOrg::OnDataChannel(webrtc::DataChannelInterface* channel)
{
    std::unique_ptr<RTCDataChannelHandler> handler = std::make_unique<RTCDataChannelHandlerWebRtcOrg>(channel);
    m_client->addRemoteDataChannel(WTFMove(handler));
}

void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id, const RefPtr<RTCSessionDescription> desc)
{
    ASSERT(id == m_sessionDescriptionRequestId);
    ASSERT(m_sessionDescriptionPromise);

    String type = desc->type();
    String sdp = desc->sdp();

    RefPtr<RTCSessionDescription> sessionDesc(RTCSessionDescription::create(type, sdp));
    m_sessionDescriptionPromise->resolve(sessionDesc);

    m_sessionDescriptionRequestId = InvalidRequestId;
    m_sessionDescriptionPromise = WTF::Nullopt;
}
void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id, const webrtc::StatsReports& reports)
{
    Optional<PeerConnection::StatsPromise> statsPromise = m_statsPromises.take(id);
    if (!statsPromise) {
        LOG(Media, "***Error: couldn't find promise for stats request: %d", id);
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
    : m_rtcDataChannel(dataChannel)
    , m_client(nullptr)
    , m_observer(new rtc::RefCountedObject<RTCDataChannelObserver>(this))
{
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
    LOG(Media, "DataChannel id=%d state=%s", m_rtcDataChannel->id(), webrtc::DataChannelInterface::DataStateString(state));

    RTCDataChannelHandlerClient::ReadyState readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
    switch (state) {
    case DataChannelInterface::kConnecting:
        readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
        break;
    case DataChannelInterface::kOpen:
        readyState = RTCDataChannelHandlerClient::ReadyStateOpen;
        break;
    case DataChannelInterface::kClosing:
        readyState = RTCDataChannelHandlerClient::ReadyStateClosing;
        break;
    case DataChannelInterface::kClosed:
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
}

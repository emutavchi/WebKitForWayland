#include "wrtcint.h"

#include <glib.h>

#include <condition_variable>
#include <limits>
#include <mutex>
#include <unordered_map>

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

#define NOTREACHED() ASSERT(false)

// TODO: Make it possible to overload/override device specific hooks
webrtc::AudioDeviceModule* CreateAudioDeviceModule(int32_t) {
    return nullptr;
}
cricket::WebRtcVideoEncoderFactory* CreateWebRtcVideoEncoderFactory() {
    return nullptr;
}
cricket::WebRtcVideoDecoderFactory* CreateWebRtcVideoDecoderFactory() {
    return nullptr;
}
cricket::VideoDeviceCapturerFactory* CreateVideoDeviceCapturerFactory() {
    return nullptr;
}

namespace {

GMainLoop *gMainLoop = nullptr;

int generateNextId()
{
    static int gRequestId = 0;
    if (gRequestId == std::numeric_limits<int>::max()) {
        gRequestId = 0;
    }
    return ++gRequestId;
}

class MockMediaConstraints : public webrtc::MediaConstraintsInterface
{
    webrtc::MediaConstraintsInterface::Constraints m_mandatory;
    webrtc::MediaConstraintsInterface::Constraints m_optional;
public:
    explicit MockMediaConstraints(const WRTCInt::RTCOfferAnswerOptions &options)
    {
        for(const auto& c: options) {
            std::string key = c.first;
            std::string value = c.second ? std::string("true") : std::string("false");
            m_mandatory.push_back(webrtc::MediaConstraintsInterface::Constraint(key, value));
        }
    }

    explicit MockMediaConstraints(const WRTCInt::RTCMediaConstraints& constraints)
    {
        for(const auto& c: constraints) {
            m_optional.push_back(webrtc::MediaConstraintsInterface::Constraint(c.first, c.second));
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

class MockMediaStream : public WRTCInt::RTCMediaStream
{
    rtc::scoped_refptr<webrtc::MediaStreamInterface> m_stream;
    std::string m_label;
public:
    MockMediaStream(webrtc::MediaStreamInterface* stream, std::string label)
        : m_stream(stream)
        , m_label(std::move(label))
    {  }

    ~MockMediaStream() override
    {
        if (m_stream) {
            webrtc::AudioTrackVector audioTracks = m_stream->GetAudioTracks();
            for (auto &track : audioTracks) {
                track->set_enabled(false);
                m_stream->RemoveTrack(track);
            }
            webrtc::VideoTrackVector videoTracks = m_stream->GetVideoTracks();
            for (auto &track : videoTracks) {
                track->set_enabled(false);
                if (track->GetSource()) {
                    track->GetSource()->Stop();
                }
                m_stream->RemoveTrack(track);
            }
        }
    }

    webrtc::MediaStreamInterface* stream() const
    {
        return m_stream.get();
    }

    std::string id() const override
    {
        return m_label;
    }
};

// TODO: Implement a hole puncher "renderer"
class MockVideoRenderer : public WRTCInt::RTCVideoRenderer
                        , public rtc::VideoSinkInterface<cricket::VideoFrame>

{
    WRTCInt::RTCVideoRendererClient* m_client;
    rtc::scoped_refptr<webrtc::VideoTrackInterface> m_renderedTrack;
    std::unique_ptr<uint8_t[]> m_imageBuffer;
    int m_width {0};
    int m_height {0};
public:
    MockVideoRenderer(MockMediaStream* stream, WRTCInt::RTCVideoRendererClient* client)
        : m_client(client)
    {
        ASSERT(stream != nullptr);
        ASSERT(stream->stream() != nullptr);
        if (!stream->stream()->GetVideoTracks().empty()) {
            m_renderedTrack = stream->stream()->GetVideoTracks().at(0);
        }
        if (m_renderedTrack) {
            m_renderedTrack->AddOrUpdateSink(this, rtc::VideoSinkWants());
        }
    }

    ~MockVideoRenderer() override
    {
        if (m_renderedTrack) {
            m_renderedTrack->RemoveSink(this);
            m_renderedTrack = nullptr;
        }
    }

    void OnFrame(const cricket::VideoFrame& frame) override
    {
        if (m_client == nullptr) {
            return;
        }
        if (frame.width() <= 0 || frame.height() <= 0) {
            m_width = m_height = 0;
            m_imageBuffer.reset();
            return;
        }
        if (frame.width() != m_width || frame.height() != m_height) {
            m_width = frame.width();
            m_height = frame.height();
            m_imageBuffer.reset(new uint8_t[m_width * m_width * 4]);
        }
        size_t size = m_width * m_width * 4;
        size_t converted_size = frame.ConvertToRgbBuffer(
            cricket::FOURCC_ARGB, m_imageBuffer.get(), size, m_width * 4);
        if (converted_size != 0 && size >= converted_size) {
            m_client->renderFrame(m_imageBuffer.get(), size, m_width, m_height);
        } else {
            LOG(LS_ERROR) << "Failed to convert to RGB buffer";
        }
    }

    void setVideoRectangle(int, int, int, int) override
    { }
};

class MockCreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver
{
    WRTCInt::RTCPeerConnection *m_backend;
    int m_requestId;
public:
    MockCreateSessionDescriptionObserver(WRTCInt::RTCPeerConnection *backend, int id)
        : m_backend(backend)
        , m_requestId(id)
    { }

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override
    {
        std::unique_ptr<webrtc::SessionDescriptionInterface> holder(desc);
        std::string descStr;
        if (!desc->ToString(&descStr)) {
            std::string error = "Failed to get session description string";
            OnFailure(error);
            return;
        }
        WRTCInt::RTCSessionDescription sessionDescription;
        sessionDescription.type = desc->type();
        sessionDescription.sdp = descStr;
        m_backend->client()->requestSucceeded(m_requestId, sessionDescription);
    }

    void OnFailure(const std::string& error) override
    {
        LOG(LS_ERROR) << error;
        m_backend->client()->requestFailed(m_requestId, error);
    }
};

class MockSetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver
{
    WRTCInt::RTCPeerConnection *m_backend;
    int m_requestId;
public:
    MockSetSessionDescriptionObserver(WRTCInt::RTCPeerConnection *backend, int id)
        : m_backend(backend)
        , m_requestId(id)
    { }

    void OnSuccess() override
    {
        m_backend->client()->requestSucceeded(m_requestId);
    }

    void OnFailure(const std::string& error) override
    {
        LOG(LS_ERROR) << error;
        m_backend->client()->requestFailed(m_requestId, error);
    }
};

class MockRTCDataChannel : public WRTCInt::RTCDataChannel
{
    class MockDataChannelObserver: public webrtc::DataChannelObserver, public rtc::RefCountInterface
    {
        MockRTCDataChannel* m_channel;
    public:
        explicit MockDataChannelObserver(MockRTCDataChannel* channel)
            : m_channel(channel)
        { }
        void OnStateChange() override
        {
            m_channel->onStateChange();
        }
        void OnMessage(const webrtc::DataBuffer& buffer) override
        {
            m_channel->onMessage(buffer);
        }
    };

    WRTCInt::RTCDataChannelClient* m_client;
    rtc::scoped_refptr<webrtc::DataChannelInterface> m_dataChannel;
    rtc::scoped_refptr<MockDataChannelObserver> m_observer;
public:
    MockRTCDataChannel()
        : m_client(nullptr)
        , m_dataChannel()
        , m_observer(new rtc::RefCountedObject<MockDataChannelObserver>(this))
    {
    }

    ~MockRTCDataChannel() override
    {
        closeDataChannel();
    }

    // WRTCInt::RTCDataChannel
    std::string label() const override
    {
        return m_dataChannel->label();
    }
    bool ordered() const override
    {
        return m_dataChannel->ordered();
    }
    unsigned short maxRetransmitTime() const override
    {
        return m_dataChannel->maxRetransmitTime();
    }
    unsigned short maxRetransmits() const override
    {
        return m_dataChannel->maxRetransmits();
    }
    std::string protocol() const override
    {
        return m_dataChannel->protocol();
    }
    bool negotiated() const override
    {
        return m_dataChannel->negotiated();
    }
    unsigned short id() override
    {
        return m_dataChannel->id();
    }
    unsigned long bufferedAmount() override
    {
        return m_dataChannel->buffered_amount();
    }
    bool sendStringData(const std::string& str) override
    {
        rtc::CopyOnWriteBuffer buffer(str.c_str(), str.size());
        return m_dataChannel->Send(webrtc::DataBuffer(buffer, false));
    }
    bool sendRawData(const char* data, size_t sz) override
    {
        rtc::CopyOnWriteBuffer buffer(data, sz);
        return m_dataChannel->Send(webrtc::DataBuffer(buffer, true));
    }
    void close() override
    {
        closeDataChannel();
    }
    void setClient(WRTCInt::RTCDataChannelClient* client) override
    {
        m_client = client;
        if (m_client != nullptr) {
            m_dataChannel->RegisterObserver(m_observer.get());
        } else {
            m_dataChannel->UnregisterObserver();
        }
    }

    // Helpers
    void setDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel)
    {
        m_dataChannel = dataChannel;
    }
    void onStateChange()
    {
        ASSERT(m_client);
        webrtc::DataChannelInterface::DataState state = m_dataChannel->state();
        LOG(LS_INFO) << "DataChannel id=" << m_dataChannel->id() << " state=" << webrtc::DataChannelInterface::DataStateString(state);
        m_client->didChangeReadyState(static_cast<WRTCInt::DataChannelState>(state));
    }
    void onMessage(const webrtc::DataBuffer& buffer)
    {
        ASSERT(m_client);
        const char* data = buffer.data.data<char>();
        size_t sz = buffer.data.size();
        if (buffer.binary) {
            m_client->didReceiveRawData(data, sz);
        } else {
            m_client->didReceiveStringData(std::string(data, sz));
        }
    }
    void closeDataChannel()
    {
        if (m_dataChannel) {
            m_dataChannel->UnregisterObserver();
            m_dataChannel->Close();
            m_dataChannel = nullptr;
        }
    }
};

class MockRTCStatsReport : public WRTCInt::RTCStatsReport
{
    const webrtc::StatsReport* m_report;
public:
    explicit MockRTCStatsReport(const webrtc::StatsReport* report)
        : m_report(report)
    {}

    double timestamp() const override
    {
        return m_report->timestamp();
    }

    std::string id() const override
    {
        return m_report->id()->ToString();
    }

    std::string type() const override
    {
        return m_report->TypeToString();
    }

    std::vector<WRTCInt::RTCStatsReport::Value> values() const override
    {
        std::vector<WRTCInt::RTCStatsReport::Value> ret;
        ret.reserve(m_report->values().size());
        for(const auto& p : m_report->values()) {
            const webrtc::StatsReport::ValuePtr& valPtr = p.second;
            std::string name = valPtr->display_name();
            std::string valueStr = valPtr->ToString();
            ret.emplace_back(name, valueStr);
        }
        return ret;
    }
};

class MockStatsObserver : public webrtc::StatsObserver
{
    WRTCInt::RTCPeerConnection *m_backend;
    unsigned int m_requestId;
public:
    MockStatsObserver(WRTCInt::RTCPeerConnection *backend, unsigned int id)
        : m_backend(backend)
        , m_requestId(id)
    { }

    void OnComplete(const webrtc::StatsReports& reports) override
    {
        std::vector<std::unique_ptr<WRTCInt::RTCStatsReport>> rtcReports;
        rtcReports.reserve(reports.size());
        for(auto& r : reports) {
            rtcReports.emplace_back(new MockRTCStatsReport(r));
        }
        m_backend->client()->requestSucceeded(m_requestId, rtcReports);
    }
};

class MockRTCPeerConnection : public WRTCInt::RTCPeerConnection, public webrtc::PeerConnectionObserver
{
    WRTCInt::RTCPeerConnectionClient* m_client;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> m_peerConnection;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
    std::set<std::uintptr_t> m_addedStreams;

    void OnAddStream(webrtc::MediaStreamInterface* stream) override
    {
        std::vector<std::string> audioSources;
        for (const auto& track: stream->GetAudioTracks()) {
            audioSources.push_back(track->id());
        }
        std::vector<std::string> videoSources;
        for (const auto& track: stream->GetVideoTracks()) {
            videoSources.push_back(track->id());
        }
        m_client->didAddRemoteStream(new MockMediaStream(stream, stream->label()), audioSources, videoSources);
    }

    void OnRemoveStream(webrtc::MediaStreamInterface* /*stream*/) override
    {
        LOG(LS_WARNING) << "Not Implemented";
    }

    void OnRenegotiationNeeded() override
    {
        m_client->negotiationNeeded();
    }

    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state) override
    {
        m_client->didChangeIceConnectionState(static_cast<WRTCInt::IceConnectionState>(state));
    }

    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState state) override
    {
        m_client->didChangeIceGatheringState(static_cast<WRTCInt::IceGatheringState>(state));
    }

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState state) override
    {
        m_client->didChangeSignalingState(static_cast<WRTCInt::SignalingState>(state));
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override
    {
        std::string sdpStr;
        if (candidate->ToString(&sdpStr)) {
            WRTCInt::RTCIceCandidate rtcCandidate;
            rtcCandidate.sdp = sdpStr;
            rtcCandidate.sdpMid = candidate->sdp_mid();
            rtcCandidate.sdpMLineIndex = candidate->sdp_mline_index();
            m_client->didGenerateIceCandidate(rtcCandidate);
        }
    }

    void OnDataChannel(webrtc::DataChannelInterface* remoteDataChannel) override
    {
        auto rtcChannel = new MockRTCDataChannel;
        rtcChannel->setDataChannel(remoteDataChannel);
        m_client->didAddRemoteDataChannel(rtcChannel);
    }

public:
    MockRTCPeerConnection(WRTCInt::RTCPeerConnectionClient* client, webrtc::PeerConnectionFactoryInterface* factory)
        : m_client(client)
        , m_peerConnectionFactory(factory)
    { }

    bool setConfiguration(const WRTCInt::RTCConfiguration& rtcConfig, const WRTCInt::RTCMediaConstraints& rtcConstraints) override
    {
        webrtc::PeerConnectionInterface::IceServers servers;
        for (const auto& iceServer : rtcConfig.iceServers) {
            for (const auto& url : iceServer.urls) {
                webrtc::PeerConnectionInterface::IceServer server;
                server.uri = url;
                server.username = iceServer.username;
                server.password = iceServer.credential;
                servers.push_back(server);
            }
        }
        std::unique_ptr<MockMediaConstraints> constraints(new MockMediaConstraints(rtcConstraints));
        if (!m_peerConnection) {
            webrtc::PeerConnectionInterface::RTCConfiguration configuration;
            configuration.servers = servers;
            m_peerConnection = m_peerConnectionFactory->CreatePeerConnection(
                configuration, constraints.get(), nullptr, nullptr, this);
        } else {
            m_peerConnection->UpdateIce(servers, constraints.get());
        }
        return !!m_peerConnection;
    }

    int createOffer(const WRTCInt::RTCOfferAnswerOptions &options) override
    {
        int requestId = generateNextId();
        std::unique_ptr<MockMediaConstraints> constrains(new MockMediaConstraints(options));
        webrtc::CreateSessionDescriptionObserver* observer =
            new rtc::RefCountedObject<MockCreateSessionDescriptionObserver>(this, requestId);
        m_peerConnection->CreateOffer(observer, constrains.get());
        return requestId;
    }

    int createAnswer(const WRTCInt::RTCOfferAnswerOptions &options) override
    {
        int requestId = generateNextId();
        std::unique_ptr<MockMediaConstraints> constrains(new MockMediaConstraints(options));
        webrtc::CreateSessionDescriptionObserver* observer =
            new rtc::RefCountedObject<MockCreateSessionDescriptionObserver>(this, requestId);
        m_peerConnection->CreateAnswer(observer, constrains.get());
        return requestId;
    }

    bool localDescription(WRTCInt::RTCSessionDescription& desc) override
    {
        const webrtc::SessionDescriptionInterface* sessionDescription =
            m_peerConnection->local_description();
        if (sessionDescription && sessionDescription->ToString(&desc.sdp)) {
            desc.sdp = sessionDescription->type();
            return true;
        }
        return false;
    }

    int setLocalDescription(const WRTCInt::RTCSessionDescription& desc) override
    {
        int requestId = generateNextId();
        webrtc::SdpParseError error;
        webrtc::SessionDescriptionInterface* sessionDescription =
            webrtc::CreateSessionDescription(desc.type, desc.sdp, &error);
        if (!sessionDescription) {
            LOG(LS_ERROR) << "Failed to create session description, error=" << error.description << " line=" << error.line;
            return WRTCInt::InvalidRequestId;
        }
        webrtc::SetSessionDescriptionObserver* observer =
            new rtc::RefCountedObject<MockSetSessionDescriptionObserver>(this, requestId);
        m_peerConnection->SetLocalDescription(observer, sessionDescription);
        return requestId;
    }

    bool remoteDescription(WRTCInt::RTCSessionDescription& desc) override
    {
        const webrtc::SessionDescriptionInterface* sessionDescription =
            m_peerConnection->remote_description();
        if (sessionDescription && sessionDescription->ToString(&desc.sdp)) {
            desc.sdp = sessionDescription->type();
            return true;
        }
        return false;
    }

    int setRemoteDescription(const WRTCInt::RTCSessionDescription& desc) override
    {
        int requestId = generateNextId();
        webrtc::SdpParseError error;
        webrtc::SessionDescriptionInterface* sessionDescription =
            webrtc::CreateSessionDescription(desc.type, desc.sdp, &error);
        if (!sessionDescription) {
            LOG(LS_ERROR) << "Failed to create session description, error=" << error.description << " line=" << error.line;
            return WRTCInt::InvalidRequestId;
        }
        webrtc::SetSessionDescriptionObserver* observer =
            new rtc::RefCountedObject<MockSetSessionDescriptionObserver>(this, requestId);
        m_peerConnection->SetRemoteDescription(observer, sessionDescription);
        return requestId;
    }

    bool addIceCandidate(const WRTCInt::RTCIceCandidate& rtcCandidate) override
    {
        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(
            webrtc::CreateIceCandidate(rtcCandidate.sdpMid,
                                       rtcCandidate.sdpMLineIndex,
                                       rtcCandidate.sdp,
                                       &error));
        if (!candidate) {
            LOG(LS_ERROR) << "Failed to add ICE candidate, error=" << error.description << " line=" << error.line;
            return false;
        }
        return m_peerConnection->AddIceCandidate(candidate.get());
    }

    bool addStream(WRTCInt::RTCMediaStream* rtcStream) override
    {
        if (rtcStream == nullptr) {
            return false;
        }
        uintptr_t streamPtr = reinterpret_cast<std::uintptr_t>(rtcStream);
        if(m_addedStreams.end() != m_addedStreams.find(streamPtr)) {
            return true;
        }
        bool ret = m_peerConnection->AddStream(static_cast<MockMediaStream*>(rtcStream)->stream());
        if (ret) {
            m_addedStreams.insert(streamPtr);
        }
        return ret;
    }

    bool removeStream(WRTCInt::RTCMediaStream* rtcStream) override
    {
        if (rtcStream == nullptr) {
            return false;
        }
        m_addedStreams.erase(reinterpret_cast<std::uintptr_t>(rtcStream));
        m_peerConnection->RemoveStream(static_cast<MockMediaStream*>(rtcStream)->stream());
        return true;
    }

    int getStats() override
    {
        int requestId = generateNextId();
        rtc::scoped_refptr<MockStatsObserver> observer(
            new rtc::RefCountedObject<MockStatsObserver>(this, requestId));
        webrtc::PeerConnectionInterface::StatsOutputLevel level =
            webrtc::PeerConnectionInterface::kStatsOutputLevelStandard;
        bool rc = m_peerConnection->GetStats(observer, nullptr, level);
        if (rc) {
            return requestId;
        }
        return WRTCInt::InvalidRequestId;
    }

    void stop() override
    {
        m_peerConnection = nullptr;
    }

    WRTCInt::RTCPeerConnectionClient* client() override
    {
        return m_client;
    }

    WRTCInt::RTCDataChannel* createDataChannel(const std::string &label, const WRTCInt::DataChannelInit& initData) override
    {
        webrtc::DataChannelInit config;
        config.id = initData.id;
        config.ordered = initData.ordered;
        config.negotiated = initData.negotiated;
        if (initData.maxRetransmits > 0) {
            config.maxRetransmits = initData.maxRetransmits;
        } else {
            config.maxRetransmitTime = initData.maxRetransmitTime;
        }
        config.protocol = initData.protocol;
        rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel =
            m_peerConnection->CreateDataChannel(label, &config);
        if (dataChannel) {
            auto channel = new MockRTCDataChannel;
            channel->setDataChannel(dataChannel);
            return channel;
        }
        return nullptr;
    }
};

struct DummySocketServer : public rtc::NullSocketServer
{
    bool Wait(int, bool) override { return true; }
    void WakeUp() override { }
};

class SignalingThreadWrapper : public rtc::Thread
{
    using MessagesQueue = std::unordered_map<uint32_t, rtc::Message>;

    std::mutex m_mutex;
    std::condition_variable m_sendCondition;
    MessagesQueue m_pendingMessages;
    uint32_t m_lastTaskId {0};

    uint32_t addPendingMessage(rtc::MessageHandler* handler,
                               uint32_t message_id, rtc::MessageData* data)
    {
        rtc::Message message;
        message.phandler = handler;
        message.message_id = message_id;
        message.pdata = data;
        std::lock_guard<std::mutex> lock(m_mutex);
        uint32_t id = ++m_lastTaskId;
        m_pendingMessages[id] = message;
        return id;
    }

    void invokeMessageHandler(rtc::Message message)
    {
        ASSERT(IsCurrent());
        if (message.message_id == rtc::MQID_DISPOSE) {
            ASSERT(message.pdata != nullptr);
            delete message.pdata;
        } else {
            ASSERT(message.phandler != nullptr);
            message.phandler->OnMessage(&message);
        }
    }

    void handlePendingMessage(uint32_t id)
    {
        rtc::Message message;
        bool haveMessage = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_pendingMessages.find(id);
            if (it != m_pendingMessages.end()) {
                message = it->second;
                haveMessage = true;
                m_pendingMessages.erase(it);
            }
        }
        if (haveMessage) {
            invokeMessageHandler(message);
        }
    }

    void handleSend(uint32_t id)
    {
        handlePendingMessage(id);
        m_sendCondition.notify_one();
    }

    void postInternal(int delay_ms, rtc::MessageHandler* handler,
                      uint32_t message_id, rtc::MessageData* data)
    {
        uint32_t id = addPendingMessage(handler, message_id, data);
        // TODO: Replace with integration to WTF's main RunLoop
        using TaskInfo = std::pair<SignalingThreadWrapper*, uint32_t>;
        auto info = new TaskInfo(this, id);
        g_timeout_add(delay_ms, [](gpointer data) -> gboolean {
            std::unique_ptr<TaskInfo> info(reinterpret_cast<TaskInfo*>(data));
            info->first->handlePendingMessage(info->second);
            return G_SOURCE_REMOVE;
        }, info);
    }

    void sendInternal(rtc::MessageHandler* handler, uint32_t message_id,
                      rtc::MessageData* data)
    {
        if (IsCurrent()) {
            rtc::Message message;
            message.phandler = handler;
            message.message_id = message_id;
            message.pdata = data;
            invokeMessageHandler(message);
            return;
        }
        uint32_t id = addPendingMessage(handler, message_id, data);
        // TODO: Replace with integration to WTF's main RunLoop
        using TaskInfo = std::pair<SignalingThreadWrapper*, uint32_t>;
        auto info = new TaskInfo(this, id);
        g_timeout_add(0, [](gpointer data) -> gboolean {
            std::unique_ptr<TaskInfo> info(reinterpret_cast<TaskInfo*>(data));
            info->first->handleSend(info->second);
            return G_SOURCE_REMOVE;
        }, info);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sendCondition.wait(lock);
    }

public:
    SignalingThreadWrapper()
        : rtc::Thread(new DummySocketServer())
    {
        if (!rtc::MessageQueueManager::IsInitialized())  {
            rtc::MessageQueueManager::Add(this);
        }
        rtc::ThreadManager::Instance()->SetCurrentThread(this);
    }

    void Post(rtc::MessageHandler* handler, uint32_t message_id,
              rtc::MessageData* data, bool /*time_sensitive*/) override
    {
        postInternal(0, handler, message_id, data);
    }

    void PostDelayed(int delay_ms, rtc::MessageHandler* handler,
                     uint32_t message_id, rtc::MessageData* data) override
    {
        postInternal(delay_ms, handler, message_id, data);
    }

    void Send(rtc::MessageHandler *handler, uint32_t id, rtc::MessageData *data) override
    {
        sendInternal(handler, id, data);
    }

    void Clear(rtc::MessageHandler* handler, uint32_t id, rtc::MessageList* removed) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_pendingMessages.begin(); it != m_pendingMessages.end();) {
            const rtc::Message& message = it->second;
            if (message.Match(handler, id)) {
                if (removed != nullptr) {
                    removed->push_back(message);
                } else {
                    delete message.pdata;
                }
                it = m_pendingMessages.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Quit() override
    {
        NOTREACHED();
    }

    bool IsQuitting() override
    {
        NOTREACHED();
        return false;
    }

    void Restart() override
    {
        NOTREACHED();
    }

    bool Get(rtc::Message*, int, bool) override
    {
        NOTREACHED();
        return false;
    }

    bool Peek(rtc::Message*, int) override
    {
        NOTREACHED();
        return false;
    }

    void PostAt(uint32_t, rtc::MessageHandler*, uint32_t, rtc::MessageData*) override
    {
        NOTREACHED();
    }

    void Dispatch(rtc::Message*) override
    {
        NOTREACHED();
    }

    void ReceiveSends() override
    {
        // NOTE: it is called by the worker thread, but it shouldn't do anything
    }

    int GetDelay() override
    {
        NOTREACHED();
        return 0;
    }

    void Stop() override
    {
        NOTREACHED();
    }

    void Run() override
    {
        NOTREACHED();
    }
};

class MockRealtimeMediaSourceCenter : public WRTCInt::RTCMediaSourceCenter
{
    std::unique_ptr<rtc::Thread> m_workerThread;
    std::unique_ptr<rtc::Thread> m_signalingThread;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
public:
    WRTCInt::RTCMediaStream* createMediaStream(const std::string& audioDeviceID, const std::string& videoDeviceID) override
    {
        ensurePeerConnectionFactory();
        // TODO: generate unique ids using WebCore's UUID
        const std::string streamLabel = "ToDo-Generate-Stream-UUID";
        rtc::scoped_refptr<webrtc::MediaStreamInterface> stream =
            m_peerConnectionFactory->CreateLocalMediaStream(streamLabel);
        if (!audioDeviceID.empty()) {
            const std::string audioLabel = "ToDo-Generate-Audio-UUID";
            rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
                m_peerConnectionFactory->CreateAudioTrack(
                    audioLabel, m_peerConnectionFactory->CreateAudioSource(nullptr)));
            stream->AddTrack(audio_track);
        }
        if (!videoDeviceID.empty()) {
            const std::string videoLabel = "ToDo-Generate-Video-UUID";
            cricket::WebRtcVideoDeviceCapturerFactory factory;
            cricket::VideoCapturer* videoCapturer =
                factory.Create(cricket::Device(videoDeviceID, videoDeviceID));
            rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
                m_peerConnectionFactory->CreateVideoTrack(
                    videoLabel, m_peerConnectionFactory->CreateVideoSource(videoCapturer, nullptr)));
            stream->AddTrack(video_track);
        }
        return new MockMediaStream(stream.get(), stream->label());
    }

    WRTCInt::RTCVideoRenderer* createVideoRenderer(WRTCInt::RTCMediaStream* stream, WRTCInt::RTCVideoRendererClient* client) override
    {
        return new MockVideoRenderer(static_cast<MockMediaStream*>(stream), client);
    }

    WRTCInt::RTCPeerConnection* createPeerConnection(WRTCInt::RTCPeerConnectionClient* client) override
    {
        ensurePeerConnectionFactory();
        return new MockRTCPeerConnection(client, m_peerConnectionFactory.get());
    }

    void ensurePeerConnectionFactory()
    {
        if (m_peerConnectionFactory != nullptr) {
            return;
        }

        m_workerThread.reset(new rtc::Thread);
        m_workerThread->SetName("rtc-worker", this);
        m_workerThread->Start();

        m_signalingThread.reset(new SignalingThreadWrapper);

        m_peerConnectionFactory = webrtc::CreatePeerConnectionFactory(
            m_workerThread.get(),
            m_signalingThread.get(),
            CreateAudioDeviceModule(0),
            CreateWebRtcVideoEncoderFactory(),
            CreateWebRtcVideoDecoderFactory());

        ASSERT(m_peerConnectionFactory);
    }
};

};  // namespace

void WRTCInt::enumerateDevices(DeviceType type, std::vector<std::string>& devices)
{
    if (WRTCInt::AUDIO == type) {
        webrtc::VoiceEngine* voe = webrtc::VoiceEngine::Create();
        if (!voe) {
            LOG(LS_ERROR) << "Failed to create VoiceEngine";
            return;
        }
        webrtc::VoEBase* base = webrtc::VoEBase::GetInterface(voe);
        webrtc::AudioDeviceModule* externalADM = CreateAudioDeviceModule(0);
        if (base->Init(externalADM) != 0) {
            LOG(LS_ERROR) << "Failed to init VoEBase";
            base->Release();
            webrtc::VoiceEngine::Delete(voe);
            return;
        }
        webrtc::VoEHardware* hardware = webrtc::VoEHardware::GetInterface(voe);
        if (!hardware) {
            LOG(LS_ERROR) << "Failed to get interface to VoEHardware";
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
            LOG(LS_ERROR) << "Failed to get number of recording devices";
        }
        base->Terminate();
        base->Release();
        hardware->Release();
        webrtc::VoiceEngine::Delete(voe);
        return;
    }
    ASSERT(WRTCInt::VIDEO == type);
    std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
        webrtc::VideoCaptureFactory::CreateDeviceInfo(0));
    if (!info) {
        LOG(LS_ERROR) << "Failed to get video capture device info";
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
        char name[kSize] = {0};
        char id[kSize] = {0};
        if (info->GetDeviceName(i, name, kSize, id, kSize) != -1) {
            devices.push_back(name);
            break;
        } else {
            LOG(LS_ERROR) << "Failed to create capturer for: '" << name << "'";
        }
    }
}

WRTCInt::RTCMediaSourceCenter* WRTCInt::createRTCMediaSourceCenter()
{
    return new MockRealtimeMediaSourceCenter;
}

void WRTCInt::init()
{
    // TODO: Route RTC logs to WebCore's logger
    const char* logConfig = getenv("WRTC_LOG");
    if (logConfig == nullptr) {
        logConfig = "tstamp info debug";
    }
    rtc::LogMessage::ConfigureLogging(logConfig);
    rtc::InitializeSSL();
    rtc::ThreadManager::Instance();
}

void WRTCInt::shutdown()
{
    rtc::CleanupSSL();
}

int WRTCInt_TestSupport::exec()
{
    if (nullptr == gMainLoop) {
        gMainLoop = g_main_loop_new(nullptr, FALSE);
        g_main_loop_run(gMainLoop);
    }
    return 0;
}

void WRTCInt_TestSupport::quit()
{
    if (nullptr != gMainLoop) {
        g_main_loop_quit(gMainLoop);
        g_main_loop_unref(gMainLoop);
        gMainLoop = nullptr;
    }
}

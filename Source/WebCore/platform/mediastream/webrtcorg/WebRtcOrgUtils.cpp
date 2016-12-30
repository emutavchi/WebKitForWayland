#include "config.h"

#include "WebRtcOrgUtils.h"
#include "Logging.h"
#include "UUID.h"

#include <webrtc/api/datachannelinterface.h>
#include <webrtc/api/mediaconstraintsinterface.h>
#include <webrtc/api/mediastreaminterface.h>
#include <webrtc/api/peerconnectionfactory.h>
#include <webrtc/base/common.h>
#include <webrtc/base/nullsocketserver.h>
#include <webrtc/base/refcount.h>
#include <webrtc/base/scoped_ref_ptr.h>
#include <webrtc/base/ssladapter.h>
#include <webrtc/common_types.h>
#include <webrtc/media/base/videocapturerfactory.h>
#include <webrtc/media/engine/webrtcvideocapturerfactory.h>
#include <webrtc/media/engine/webrtcvideocapturer.h>
#include <webrtc/media/engine/webrtcvideodecoderfactory.h>
#include <webrtc/media/engine/webrtcvideoencoderfactory.h>
#include <webrtc/modules/audio_device/include/audio_device_defines.h>
#include <webrtc/modules/audio_device/include/audio_device.h>
#include <webrtc/modules/video_capture/video_capture_factory.h>
#include <webrtc/voice_engine/include/voe_base.h>
#include <webrtc/voice_engine/include/voe_hardware.h>

#include <condition_variable>
#include <unordered_map>
#include <mutex>

#include <glib.h>

// TODO(em):
cricket::WebRtcVideoEncoderFactory* CreateWebRtcVideoEncoderFactory() { return nullptr; }
cricket::WebRtcVideoDecoderFactory* CreateWebRtcVideoDecoderFactory() { return nullptr; }
cricket::VideoDeviceCapturerFactory* CreateVideoDeviceCapturerFactory() { return nullptr; }
webrtc::AudioDeviceModule* CreateAudioDeviceModule(int32_t) { return nullptr; }

namespace WebCore {

struct DummySocketServer : public rtc::NullSocketServer
{
    bool Wait(int, bool) override { return true; }
    void WakeUp() override {}
};

class SignalingThreadWrapper : public rtc::Thread
{
    using MessagesQueue = std::unordered_map<uint32_t, rtc::Message>;

    std::mutex m_mutex;
    std::condition_variable m_sendCondition;
    MessagesQueue m_pendingMessages;
    uint32_t m_lastTaskId{ 0 };

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
        if (!rtc::MessageQueueManager::IsInitialized()) {
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

    void Send(rtc::MessageHandler* handler, uint32_t id, rtc::MessageData* data) override
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
        ASSERT_NOT_REACHED();
    }

    bool IsQuitting() override
    {
        ASSERT_NOT_REACHED();
        return false;
    }

    void Restart() override
    {
        ASSERT_NOT_REACHED();
    }

    bool Peek(rtc::Message*, int) override
    {
        ASSERT_NOT_REACHED();
        return false;
    }

    void PostAt(uint32_t, rtc::MessageHandler*, uint32_t, rtc::MessageData*) override
    {
        ASSERT_NOT_REACHED();
    }

    void Dispatch(rtc::Message*) override
    {
        ASSERT_NOT_REACHED();
    }

    void ReceiveSends() override
    {
        // NOTE: it is called by the worker thread, but it shouldn't do anything
    }

    int GetDelay() override
    {
        ASSERT_NOT_REACHED();
        return 0;
    }

    void Stop() override
    {
        ASSERT_NOT_REACHED();
    }

    void Run() override
    {
        ASSERT_NOT_REACHED();
    }
};

WebRtcOrgUtils& WebRtcOrgUtils::instance()
{
    ASSERT(isMainThread());
    static NeverDestroyed<WebRtcOrgUtils> instance;
    return instance;
}

WebRtcOrgUtils::WebRtcOrgUtils()
{
    initializeWebRtcOrg();
    ensurePeerConnectionFactory();
}

WebRtcOrgUtils::~WebRtcOrgUtils()
{
    shutdownWebRtcOrg();
}

void WebRtcOrgUtils::ensurePeerConnectionFactory()
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

void WebRtcOrgUtils::initializeWebRtcOrg()
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

void WebRtcOrgUtils::shutdownWebRtcOrg()
{
    rtc::CleanupSSL();
}

void WebRtcOrgUtils::enumerateDevices(
    RealtimeMediaSource::Type type, std::vector<std::string>& devices)
{
    if (RealtimeMediaSource::Audio == type) {
        webrtc::VoiceEngine* voe = webrtc::VoiceEngine::Create();
        if (!voe) {
            RELEASE_LOG_ERROR("Failed to create VoiceEngine");
            return;
        }
        webrtc::VoEBase* base = webrtc::VoEBase::GetInterface(voe);
        webrtc::AudioDeviceModule* externalADM = CreateAudioDeviceModule(0);
        if (base->Init(externalADM) != 0) {
            RELEASE_LOG_ERROR("Failed to init VoEBase");
            base->Release();
            webrtc::VoiceEngine::Delete(voe);
            return;
        }
        webrtc::VoEHardware* hardware = webrtc::VoEHardware::GetInterface(voe);
        if (!hardware) {
            RELEASE_LOG_ERROR("Failed to get interface to VoEHardware");
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
            RELEASE_LOG_ERROR("Failed to get number of recording devices");
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
        RELEASE_LOG_ERROR("Failed to get video capture device info");
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
            RELEASE_LOG_ERROR("Failed to create capturer for: %s", name);
        }
    }
}

rtc::scoped_refptr<webrtc::MediaStreamInterface> WebRtcOrgUtils::createMediaStream(const std::string& audioDeviceID, const std::string& videoDeviceID)
{
    const std::string streamLabel = createCanonicalUUIDString().utf8().data();

    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream
        = m_peerConnectionFactory->CreateLocalMediaStream(streamLabel);

    if (!audioDeviceID.empty()) {
        const std::string audioLabel = createCanonicalUUIDString().utf8().data();
        rtc::scoped_refptr<webrtc::AudioTrackInterface> audioTrack(
            m_peerConnectionFactory->CreateAudioTrack(
                audioLabel, m_peerConnectionFactory->CreateAudioSource(nullptr)));
        stream->AddTrack(audioTrack);
    }

    if (!videoDeviceID.empty()) {
        const std::string videoLabel = createCanonicalUUIDString().utf8().data();
        std::unique_ptr<cricket::VideoDeviceCapturerFactory> factory(
            CreateVideoDeviceCapturerFactory());
        if (!factory) {
            factory.reset(new cricket::WebRtcVideoDeviceCapturerFactory);
        }
        cricket::VideoCapturer* videoCapturer =
            factory->Create(cricket::Device(videoDeviceID, videoLabel));
        rtc::scoped_refptr<webrtc::VideoTrackInterface> videoTrack(
            m_peerConnectionFactory->CreateVideoTrack(
                videoLabel, m_peerConnectionFactory->CreateVideoSource(videoCapturer, nullptr)));
        stream->AddTrack(videoTrack);
    }

    return stream;
}

}

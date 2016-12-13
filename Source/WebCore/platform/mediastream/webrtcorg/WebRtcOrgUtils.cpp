#include "config.h"

#include <condition_variable>
#include <unordered_map>

#include "WebRtcOrgUtils.h"
#include "webrtc/base/nullsocketserver.h"
#include "webrtc/base/ssladapter.h"

namespace WebCore {

WebRtcOrgUtils* WebRtcOrgUtils::instance = NULL;

#define NOTREACHED() ASSERT(false)

struct DummySocketServer : public rtc::NullSocketServer {
    bool Wait(int, bool) override { return true; }
    void WakeUp() override {}
};

class SignalingThreadWrapper : public rtc::Thread {
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
        },
            info);
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
        },
            info);
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

WebRtcOrgUtils* WebRtcOrgUtils::getInstance()
{
    if (instance == NULL) {
        instance = new WebRtcOrgUtils();
    }
    return instance;
}

WebRtcOrgUtils::WebRtcOrgUtils()
{
    ensurePeerConnectionFactory();
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
        nullptr,
        nullptr,
        nullptr);

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
}

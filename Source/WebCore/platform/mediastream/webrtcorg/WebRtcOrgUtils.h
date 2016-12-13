#ifndef _WEBRTCORGUTILS_H_
#define _WEBRTCORGUTILS_H_

#include "webrtc/api/peerconnectionfactory.h"
#include "webrtc/base/thread.h"
#include <glib.h>
#include <mutex>

namespace WebCore {

class WebRtcOrgUtils {
public:
    static WebRtcOrgUtils* getInstance();
    ~WebRtcOrgUtils()
    {
        if (instance) {
            delete instance;
        }
    }
    void initializeWebRtcOrg();
    void shutdownWebRtcOrg();
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> getPeerConnectionFactory()
    {
        return m_peerConnectionFactory;
    }
    int exec()
    {
        if (nullptr == gMainLoop) {
            gMainLoop = g_main_loop_new(nullptr, FALSE);
            g_main_loop_run(gMainLoop);
        }
        return 0;
    }
    void quit()
    {
        if (nullptr != gMainLoop) {
            g_main_loop_quit(gMainLoop);
            g_main_loop_unref(gMainLoop);
            gMainLoop = nullptr;
        }
    }

private:
    WebRtcOrgUtils();
    void ensurePeerConnectionFactory();
    static WebRtcOrgUtils* instance;
    std::unique_ptr<rtc::Thread> m_workerThread;
    std::unique_ptr<rtc::Thread> m_signalingThread;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
    GMainLoop* gMainLoop = nullptr;
};
}
#endif

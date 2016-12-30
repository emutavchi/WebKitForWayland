#ifndef _WEBRTCORGUTILS_H_
#define _WEBRTCORGUTILS_H_

#include "RealtimeMediaSource.h"

#include <webrtc/api/mediastreaminterface.h>
#include <webrtc/api/peerconnectionfactory.h>
#include <webrtc/api/peerconnectioninterface.h>
#include <webrtc/base/thread.h>

#include <wtf/NeverDestroyed.h>
#include <wtf/MainThread.h>

#include <vector>
#include <string>

namespace WebCore {

class WebRtcOrgUtils final {
public:
    friend NeverDestroyed<WebRtcOrgUtils>;

    static WebRtcOrgUtils& instance();

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> getPeerConnectionFactory() const
    {
        return m_peerConnectionFactory;
    }

    rtc::scoped_refptr<webrtc::MediaStreamInterface> createMediaStream(
        const std::string& audioDeviceID, const std::string& videoDeviceID);

    void enumerateDevices(
        RealtimeMediaSource::Type type, std::vector<std::string>& devices);

private:
    WebRtcOrgUtils();
    ~WebRtcOrgUtils();

    void initializeWebRtcOrg();
    void shutdownWebRtcOrg();
    void ensurePeerConnectionFactory();

    std::unique_ptr<rtc::Thread> m_workerThread;
    std::unique_ptr<rtc::Thread> m_signalingThread;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
};

}

#endif

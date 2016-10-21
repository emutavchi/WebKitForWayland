#ifndef _PEERCONNECTIONBACKENDWEBRTCORG_H_
#define _PEERCONNECTIONBACKENDWEBRTCORG_H_

#include "NotImplemented.h"
#include "PeerConnectionBackend.h"

#include "RealtimeMediaSourceCenterWebRtcOrg.h"

#include "RTCDataChannel.h"
#include "RTCDataChannelHandler.h"
#include "RTCDataChannelHandlerClient.h"
#include "RTCDataChannelHandlerClient.h"
#include "RTCPeerConnection.h"
#include "RTCStatsReport.h"

#include "webrtc/api/datachannelinterface.h"
#include "webrtc/api/mediaconstraintsinterface.h"
#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/api/peerconnectionfactory.h"
#include "webrtc/api/peerconnectioninterface.h"

#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>

namespace WebCore {

enum {
    InvalidRequestId = -1
};

class PeerConnectionBackendClient;

class PeerConnectionBackendWebRtcOrg : public PeerConnectionBackend,
                                       public webrtc::PeerConnectionObserver {
public:
    PeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient*);

    virtual void createOffer(RTCOfferOptions&, PeerConnection::SessionDescriptionPromise&&) override;
    virtual void createAnswer(RTCAnswerOptions&, PeerConnection::SessionDescriptionPromise&&) override;

    virtual void setLocalDescription(RTCSessionDescription&, PeerConnection::VoidPromise&&) override;
    virtual RefPtr<RTCSessionDescription> localDescription() const override;
    virtual RefPtr<RTCSessionDescription> currentLocalDescription() const override;
    virtual RefPtr<RTCSessionDescription> pendingLocalDescription() const override;

    virtual void setRemoteDescription(RTCSessionDescription&, PeerConnection::VoidPromise&&) override;
    virtual RefPtr<RTCSessionDescription> remoteDescription() const override;
    virtual RefPtr<RTCSessionDescription> currentRemoteDescription() const override;
    virtual RefPtr<RTCSessionDescription> pendingRemoteDescription() const override;

    virtual void setConfiguration(RTCConfiguration&, const MediaConstraints&) override;
    virtual void addIceCandidate(RTCIceCandidate&, PeerConnection::VoidPromise&&) override;

    virtual void getStats(MediaStreamTrack*, PeerConnection::StatsPromise&&) override;

    virtual void replaceTrack(RTCRtpSender&, MediaStreamTrack&, PeerConnection::VoidPromise&&) override;

    virtual void stop() override;

    virtual bool isNegotiationNeeded() const override;
    virtual void markAsNeedingNegotiation() override;
    virtual void clearNegotiationNeededState() override;

    virtual std::unique_ptr<RTCDataChannelHandler> createDataChannel(const String&, const Dictionary&);

    virtual void requestSucceeded(int id, const RefPtr<RTCSessionDescription> desc);
    virtual void requestSucceeded(int id, const webrtc::StatsReports& reports);
    virtual void requestSucceeded(int id);
    virtual void requestFailed(int id, const std::string& error);

    //PeerConnectionObserver
    // Triggered when the SignalingState changed.
    virtual void OnSignalingChange(
        webrtc::PeerConnectionInterface::SignalingState new_state) override;

    // Triggered when media is received on a new stream from remote peer.
    virtual void OnAddStream(webrtc::MediaStreamInterface* stream) override;

    // Triggered when a remote peer close a stream.
    virtual void OnRemoveStream(webrtc::MediaStreamInterface* stream) override;

    // Triggered when a remote peer open a data channel.
    virtual void OnDataChannel(webrtc::DataChannelInterface* data_channel) override;

    // Triggered when renegotiation is needed, for example the ICE has restarted.
    virtual void OnRenegotiationNeeded() override;

    // Called any time the IceConnectionState changes
    virtual void OnIceConnectionChange(
        webrtc::PeerConnectionInterface::IceConnectionState new_state) override;

    // Called any time the IceGatheringState changes
    virtual void OnIceGatheringChange(
        webrtc::PeerConnectionInterface::IceGatheringState new_state) override;

    // New Ice candidate have been found.
    virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;

private:
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> m_peerConnection;

    PeerConnectionBackendClient* m_client;

    bool m_isNegotiationNeeded{ false };
    int m_sessionDescriptionRequestId{ InvalidRequestId };
    Optional<PeerConnection::SessionDescriptionPromise> m_sessionDescriptionPromise;
    int m_voidRequestId{ InvalidRequestId };
    Optional<PeerConnection::VoidPromise> m_voidPromise;
    HashMap<int, Optional<PeerConnection::StatsPromise> > m_statsPromises;
};

class RTCDataChannelHandlerWebRtcOrg
    : public RTCDataChannelHandler {
    class RTCDataChannelObserver : public webrtc::DataChannelObserver,
                                   public rtc::RefCountInterface {
        RTCDataChannelHandlerWebRtcOrg* m_channel;

    public:
        explicit RTCDataChannelObserver(RTCDataChannelHandlerWebRtcOrg* channel)
            : m_channel(channel)
        {
        }
        void OnStateChange() override
        {
            m_channel->onStateChange();
        }
        void OnMessage(const webrtc::DataBuffer& buffer) override
        {
            m_channel->onMessage(buffer);
        }
    };

public:
    RTCDataChannelHandlerWebRtcOrg(webrtc::DataChannelInterface* channel);
    ~RTCDataChannelHandlerWebRtcOrg()
    {
        closeDataChannel();
    }
    // RTCDataChannelHandler
    void setClient(RTCDataChannelHandlerClient*) override;
    String label() override;
    bool ordered() override;
    unsigned short maxRetransmitTime() override;
    unsigned short maxRetransmits() override;
    String protocol() override;
    bool negotiated() override;
    unsigned short id() override;
    unsigned long bufferedAmount() override;
    bool sendStringData(const String&) override;
    bool sendRawData(const char*, size_t) override;
    void close() override;
    void onStateChange();
    void onMessage(const webrtc::DataBuffer& buffer);
    void didReceiveStringData(const std::string& str);
    void didReceiveRawData(const char* data, size_t sz);
    void didDetectError();

private:
    void closeDataChannel();

    rtc::scoped_refptr<webrtc::DataChannelInterface> m_rtcDataChannel;
    RTCDataChannelHandlerClient* m_client;
    rtc::scoped_refptr<RTCDataChannelObserver> m_observer;
};
}
#endif // _PEERCONNECTIONBACKENDWEBRTCORG_H_

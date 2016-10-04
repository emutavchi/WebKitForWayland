#ifndef _PEERCONNECTIONBACKENDWEBRTCORG_H_
#define _PEERCONNECTIONBACKENDWEBRTCORG_H_

#include "PeerConnectionBackend.h"
#include "NotImplemented.h"

#include "RealtimeMediaSourceCenterWebRtcOrg.h"
#include "RTCDataChannelHandler.h"

#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>

namespace WebCore {

class PeerConnectionBackendClient;

class PeerConnectionBackendWebRtcOrg : public PeerConnectionBackend, public WRTCInt::RTCPeerConnectionClient {
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

    // WRTCInt::RTCPeerConnectionClient
    virtual void requestSucceeded(int id, const WRTCInt::RTCSessionDescription& desc) override;
    virtual void requestSucceeded(int id, const std::vector<std::unique_ptr<WRTCInt::RTCStatsReport>>& reports) override;
    virtual void requestSucceeded(int id) override;
    virtual void requestFailed(int id, const std::string& error) override;
    virtual void negotiationNeeded() override;
    virtual void didAddRemoteStream(WRTCInt::RTCMediaStream *stream,
                                    const std::vector<std::string> &audioSources,
                                    const std::vector<std::string> &videoSources) override;
    virtual void didGenerateIceCandidate(const WRTCInt::RTCIceCandidate& candidate) override;

    virtual void didChangeSignalingState(WRTCInt::SignalingState state) override;
    virtual void didChangeIceGatheringState(WRTCInt::IceGatheringState state) override;
    virtual void didChangeIceConnectionState(WRTCInt::IceConnectionState state) override;
    virtual void didAddRemoteDataChannel(WRTCInt::RTCDataChannel* channel) override;

private:
    PeerConnectionBackendClient* m_client;
    std::unique_ptr<WRTCInt::RTCPeerConnection> m_rtcConnection;

    bool m_isNegotiationNeeded { false };
    int m_sessionDescriptionRequestId { WRTCInt::InvalidRequestId };
    Optional<PeerConnection::SessionDescriptionPromise> m_sessionDescriptionPromise;
    int m_voidRequestId { WRTCInt::InvalidRequestId };
    Optional<PeerConnection::VoidPromise> m_voidPromise;
    HashMap<int, Optional<PeerConnection::StatsPromise>> m_statsPromises;
};

class RTCDataChannelHandlerWebRtcOrg
    : public RTCDataChannelHandler
    , public WRTCInt::RTCDataChannelClient
{
public:
    RTCDataChannelHandlerWebRtcOrg(WRTCInt::RTCDataChannel* dataChannel);

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

    // WRTCInt::RTCDataChannelClient
    void didChangeReadyState(WRTCInt::DataChannelState state) override;
    void didReceiveStringData(const std::string& str) override;
    void didReceiveRawData(const char* data, size_t sz) override;

private:
    std::unique_ptr<WRTCInt::RTCDataChannel> m_rtcDataChannel;
    RTCDataChannelHandlerClient* m_client;
};

}
#endif  // _PEERCONNECTIONBACKENDWEBRTCORG_H_

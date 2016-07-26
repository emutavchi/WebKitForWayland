#ifndef _PEERCONNECTIONBACKENDQT5WEBRTC_H_
#define _PEERCONNECTIONBACKENDQT5WEBRTC_H_

#include "PeerConnectionBackend.h"
#include "NotImplemented.h"

#include "RealtimeMediaSourceCenterQt5WebRTC.h"

#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>

namespace WebCore {

class PeerConnectionBackendClient;

class PeerConnectionBackendQt5WebRTC : public PeerConnectionBackend, public WRTCInt::RTCPeerConnectionClient {
public:
    PeerConnectionBackendQt5WebRTC(PeerConnectionBackendClient*);

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

    virtual void setConfiguration(RTCConfiguration&) override;
    virtual void addIceCandidate(RTCIceCandidate&, PeerConnection::VoidPromise&&) override;

    virtual void getStats(MediaStreamTrack*, PeerConnection::StatsPromise&&) override;

    virtual void replaceTrack(RTCRtpSender&, MediaStreamTrack&, PeerConnection::VoidPromise&&) override;

    virtual void stop() override;

    virtual bool isNegotiationNeeded() const override;
    virtual void markAsNeedingNegotiation() override;
    virtual void clearNegotiationNeededState() override;

    // WRTCInt::RTCPeerConnectionClient
    virtual void requestSucceeded(int id, const WRTCInt::RTCSessionDescription& desc) override;
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

private:
    PeerConnectionBackendClient* m_client;
    std::unique_ptr<WRTCInt::RTCPeerConnection> m_rtcConnection;

    bool m_isNegotiationNeeded { false };
    int m_sessionDescriptionRequestId { WRTCInt::InvalidRequestId };
    Optional<PeerConnection::SessionDescriptionPromise> m_sessionDescriptionPromise;
    int m_voidRequestId { WRTCInt::InvalidRequestId };
    Optional<PeerConnection::VoidPromise> m_voidPromise;
};

}
#endif  // _PEERCONNECTIONBACKENDQT5WEBRTC_H_

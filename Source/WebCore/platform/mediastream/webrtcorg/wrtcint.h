#ifndef _WRTCINT_H_
#define _WRTCINT_H_

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace WRTCInt
{

// peerconnectioninterface.h
enum SignalingState {
    Stable,
    HaveLocalOffer,
    HaveLocalPrAnswer,
    HaveRemoteOffer,
    HaveRemotePrAnswer,
    Closed,
};
enum IceGatheringState {
    IceGatheringNew,
    IceGatheringGathering,
    IceGatheringComplete
};
enum IceConnectionState {
    IceConnectionNew,
    IceConnectionChecking,
    IceConnectionConnected,
    IceConnectionCompleted,
    IceConnectionFailed,
    IceConnectionDisconnected,
    IceConnectionClosed,
};

// mediaconstraintsinterface.h
const char* const kOfferToReceiveAudio = "OfferToReceiveAudio";
const char* const kOfferToReceiveVideo = "OfferToReceiveVideo";
const char* const kVoiceActivityDetection = "VoiceActivityDetection";
const char* const kIceRestart = "IceRestart";

// datachannelinterface.h
enum DataChannelState {
    DataChannelConnecting,
    DataChannelOpen,  // The DataChannel is ready to send data.
    DataChannelClosing,
    DataChannelClosed
};

// glue
enum DeviceType {
    AUDIO,
    VIDEO
};

enum {
    InvalidRequestId = -1
};

typedef std::map<std::string, bool> RTCOfferAnswerOptions;
typedef std::map<std::string, std::string> RTCMediaConstraints;

struct RTCSessionDescription
{
    std::string type;
    std::string sdp;
};

struct RTCIceServer
{
    std::vector<std::string> urls;
    std::string credential;
    std::string username;
};

struct RTCConfiguration
{
    std::vector<RTCIceServer> iceServers;
};

struct RTCIceCandidate
{
    std::string sdp;
    std::string sdpMid;
    unsigned short sdpMLineIndex {0};
};

struct DataChannelInit
{
    bool ordered {true};
    int maxRetransmitTime {-1};
    int maxRetransmits {-1};
    std::string protocol;
    bool negotiated {false};
    int id {-1};
};

class  RTCVideoRendererClient
{
public:
    virtual ~RTCVideoRendererClient() = default;
    virtual void renderFrame(const unsigned char *data, int byteCount, int width, int height) = 0;
    virtual void punchHole(int width, int height) = 0;
};

class  RTCVideoRenderer
{
public:
    virtual ~RTCVideoRenderer() = default;
    virtual void setVideoRectangle(int x, int y, int w, int h) = 0;
};

class  RTCMediaStream
{
public:
    virtual ~RTCMediaStream() = default;
    virtual std::string id() const = 0;
};

class  RTCStatsReport
{
public:
    typedef std::pair<std::string, std::string> Value;
    virtual ~RTCStatsReport() = default;
    virtual double timestamp() const = 0;
    virtual std::string id() const = 0;
    virtual std::string type() const = 0;
    virtual std::vector<Value> values() const = 0;
};

class  RTCDataChannelClient
{
public:
    virtual ~RTCDataChannelClient() = default;
    virtual void didChangeReadyState(DataChannelState) = 0;
    virtual void didReceiveStringData(const std::string&) = 0;
    virtual void didReceiveRawData(const char*, size_t) = 0;
};

class  RTCDataChannel
{
public:
    virtual ~RTCDataChannel() = default;
    virtual std::string label() const = 0;
    virtual bool ordered() const = 0;
    virtual unsigned short maxRetransmitTime() const = 0;
    virtual unsigned short maxRetransmits() const  = 0;
    virtual std::string protocol() const = 0;
    virtual bool negotiated() const = 0;
    virtual unsigned short id() = 0;
    virtual unsigned long bufferedAmount() = 0;
    virtual bool sendStringData(const std::string&) = 0;
    virtual bool sendRawData(const char*, size_t) = 0;
    virtual void close() = 0;
    virtual void setClient(WRTCInt::RTCDataChannelClient* client) = 0;
};

class  RTCPeerConnectionClient
{
public:
    virtual ~RTCPeerConnectionClient() = default;
    virtual void requestSucceeded(int id, const RTCSessionDescription& desc) = 0;
    virtual void requestSucceeded(int id, const std::vector<std::unique_ptr<WRTCInt::RTCStatsReport>>& reports) = 0;
    virtual void requestSucceeded(int id) = 0;
    virtual void requestFailed(int id, const std::string& error) = 0;
    virtual void negotiationNeeded() = 0;
    virtual void didAddRemoteStream(RTCMediaStream *stream,
                                    const std::vector<std::string> &audioSources,
                                    const std::vector<std::string> &videoSources) = 0;
    virtual void didGenerateIceCandidate(const RTCIceCandidate& candidate) = 0;
    virtual void didChangeSignalingState(SignalingState state) = 0;
    virtual void didChangeIceGatheringState(IceGatheringState state) = 0;
    virtual void didChangeIceConnectionState(IceConnectionState state) = 0;
    virtual void didAddRemoteDataChannel(RTCDataChannel* channel) = 0;
};

class  RTCPeerConnection
{
public:
    virtual ~RTCPeerConnection() = default;

    virtual bool setConfiguration(const RTCConfiguration &config, const RTCMediaConstraints& constraints) = 0;

    virtual int createOffer(const RTCOfferAnswerOptions &options) = 0;
    virtual int createAnswer(const RTCOfferAnswerOptions &options) = 0;

    virtual bool localDescription(RTCSessionDescription& desc) = 0;
    virtual int setLocalDescription(const RTCSessionDescription& desc) = 0;

    virtual bool remoteDescription(RTCSessionDescription& desc) = 0;
    virtual int setRemoteDescription(const RTCSessionDescription& desc) = 0;

    virtual bool addIceCandidate(const RTCIceCandidate& candidate) = 0;
    virtual bool addStream(RTCMediaStream* stream) = 0;
    virtual bool removeStream(RTCMediaStream* stream) = 0;

    virtual int getStats() = 0;

    virtual RTCDataChannel* createDataChannel(const std::string &label, const DataChannelInit& initData) = 0;

    virtual void stop() = 0;

    virtual RTCPeerConnectionClient* client() = 0;
};

class  RTCMediaSourceCenter
{
public:
    virtual ~RTCMediaSourceCenter() = default;
    virtual RTCMediaStream* createMediaStream(const std::string& audioDeviceID, const std::string& videoDeviceID) = 0;
    virtual RTCVideoRenderer* createVideoRenderer(RTCMediaStream* stream, RTCVideoRendererClient* client) = 0;
    virtual RTCPeerConnection* createPeerConnection(RTCPeerConnectionClient* client) = 0;
};

void init();
void shutdown();
void enumerateDevices(DeviceType type, std::vector<std::string>& devices);
RTCMediaSourceCenter* createRTCMediaSourceCenter();

}  // namespace WRTCInt

namespace WRTCInt_TestSupport
{

int exec();
void quit();

}  // namespace WRTCInt_TestSupport

#endif  // _WRTCINT_H_

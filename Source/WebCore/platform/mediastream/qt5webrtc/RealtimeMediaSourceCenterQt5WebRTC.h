/*
* Copyright (c) 2016, Comcast
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
*  * Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR OR; PROFITS BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
* ANY OF THEORY LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _REALTIMEMEDIASOURCECENTERQT5WEBRTC_H_
#define _REALTIMEMEDIASOURCECENTERQT5WEBRTC_H_

#if ENABLE(MEDIA_STREAM)

#include "RealtimeMediaSourceCenter.h"
#include "RealtimeMediaSource.h"
#include "RealtimeMediaSourceCapabilities.h"

#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>
#include <wtf/PassRefPtr.h>
#include <wtf/text/WTFString.h>

#include <wrtcint.h>

namespace WebCore {

WRTCInt::RTCMediaSourceCenter& getRTCMediaSourceCenter();

class RealtimeMediaSourceQt5WebRTC : public RealtimeMediaSource
{
public:
    RealtimeMediaSourceQt5WebRTC(const String& id, RealtimeMediaSource::Type type, const String& name)
        : RealtimeMediaSource(id, type, name) { }
    virtual ~RealtimeMediaSourceQt5WebRTC() { }

    virtual RefPtr<RealtimeMediaSourceCapabilities> capabilities() override { return m_capabilities; }
    virtual const RealtimeMediaSourceSettings& settings() override { return m_currentSettings; }
    virtual void startProducingData() override;
    virtual void stopProducingData() override;
    virtual bool isProducingData() const override { return m_isProducingData; }

    // helper
    void setRTCStream(std::shared_ptr<WRTCInt::RTCMediaStream> stream) { m_stream = stream; }
    WRTCInt::RTCMediaStream* rtcStream() { return m_stream.get(); }

protected:
    RefPtr<RealtimeMediaSourceCapabilities> m_capabilities;
    RealtimeMediaSourceSettings m_currentSettings;
    bool m_isProducingData { false };
    std::shared_ptr<WRTCInt::RTCMediaStream> m_stream;
};

class RealtimeAudioSourceQt5WebRTC final : public RealtimeMediaSourceQt5WebRTC
{
  public:
    RealtimeAudioSourceQt5WebRTC(const String& id, const String& name)
        : RealtimeMediaSourceQt5WebRTC(id, RealtimeMediaSource::Audio, name)
    { }
};

class RealtimeVideoSourceQt5WebRTC final : public RealtimeMediaSourceQt5WebRTC
{
  public:
    RealtimeVideoSourceQt5WebRTC(const String& id, const String& name);
};

typedef HashMap<String, RefPtr<RealtimeMediaSourceQt5WebRTC>> RealtimeMediaSourceQt5WebRTCMap;

class RealtimeMediaSourceCenterQt5WebRTC final : public RealtimeMediaSourceCenter {
private:
    friend NeverDestroyed<RealtimeMediaSourceCenterQt5WebRTC>;

    RealtimeMediaSourceCenterQt5WebRTC();

    void validateRequestConstraints(MediaStreamCreationClient*,
                                    MediaConstraints& audioConstraints,
                                    MediaConstraints& videoConstraints) override;

    void createMediaStream(PassRefPtr<MediaStreamCreationClient>,
                           MediaConstraints& audioConstraints,
                           MediaConstraints& videoConstraints) override;

    bool getMediaStreamTrackSources(PassRefPtr<MediaStreamTrackSourcesRequestClient>) override;

    void createMediaStream(MediaStreamCreationClient*,
                           const String& audioDeviceID,
                           const String& videoDeviceID) override;

    RefPtr<TrackSourceInfo> sourceWithUID(const String&, RealtimeMediaSource::Type, MediaConstraints*) override;

    RefPtr<RealtimeMediaSourceQt5WebRTC> findSource(const String&, RealtimeMediaSource::Type);
    RealtimeMediaSourceQt5WebRTCMap enumerateSources(bool needsAudio, bool needsVideo);

    RealtimeMediaSourceQt5WebRTCMap m_sourceMap;
};

}

#endif


#endif  // _REALTIMEMEDIASOURCECENTERQT5WEBRTC_H_

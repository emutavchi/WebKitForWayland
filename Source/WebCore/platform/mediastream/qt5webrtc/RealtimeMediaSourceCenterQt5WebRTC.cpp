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

#include "config.h"
#include "RealtimeMediaSourceCenterQt5WebRTC.h"

#if ENABLE(MEDIA_STREAM)

#include "MediaStream.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackSourcesRequestClient.h"
#include "UUID.h"

#include <wtf/NeverDestroyed.h>
#include <wtf/MainThread.h>
#include <NotImplemented.h>

#include "FloatRect.h"

#include <cairo.h>

namespace WebCore {

void enableQt5WebRTCPeerConnectionBackend();

WRTCInt::RTCMediaSourceCenter& getRTCMediaSourceCenter()
{
    static std::unique_ptr<WRTCInt::RTCMediaSourceCenter> rtcMediaSourceCenter;
    if (!rtcMediaSourceCenter)
        rtcMediaSourceCenter.reset(WRTCInt::createRTCMediaSourceCenter());
    return *rtcMediaSourceCenter.get();
}

void RealtimeMediaSourceQt5WebRTC::startProducingData()
{
    if (m_stream) {
        m_isProducingData = true;
    }
}

void RealtimeMediaSourceQt5WebRTC::stopProducingData()
{
    if (m_isProducingData) {
        m_isProducingData = false;
        m_stream.reset();
    }
}

RealtimeVideoSourceQt5WebRTC::RealtimeVideoSourceQt5WebRTC(const String& id, const String& name)
    : RealtimeMediaSourceQt5WebRTC(id, RealtimeMediaSource::Video, name)
{
    // TODO: obtain settings from the device
    m_currentSettings.setWidth(320);
    m_currentSettings.setHeight(240);
}

RealtimeMediaSourceCenter& RealtimeMediaSourceCenter::platformCenter()
{
    ASSERT(isMainThread());
    static NeverDestroyed<RealtimeMediaSourceCenterQt5WebRTC> center;
    return center;
}

RealtimeMediaSourceCenterQt5WebRTC::RealtimeMediaSourceCenterQt5WebRTC()
{
    WRTCInt::init();

    enableQt5WebRTCPeerConnectionBackend();

    m_supportedConstraints.setSupportsWidth(true);
    m_supportedConstraints.setSupportsHeight(true);
}

void RealtimeMediaSourceCenterQt5WebRTC::validateRequestConstraints(MediaStreamCreationClient* client, MediaConstraints& audioConstraints, MediaConstraints& videoConstraints)
{
    ASSERT(client);

    bool needsAudio = audioConstraints.isValid();
    bool needsVideo = videoConstraints.isValid();

    m_sourceMap = enumerateSources(needsAudio, needsVideo);

    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;
    for (auto& source : m_sourceMap.values()) {
        if ( needsAudio && source->type() == RealtimeMediaSource::Type::Audio )
            audioSources.append(source);
        if ( needsVideo && source->type() == RealtimeMediaSource::Type::Video )
            videoSources.append(source);
    }
    client->constraintsValidated(audioSources, videoSources);
}

void RealtimeMediaSourceCenterQt5WebRTC::createMediaStream(PassRefPtr<MediaStreamCreationClient> /*prpQueryClient*/,
                                                           MediaConstraints& /*audioConstraints*/,
                                                           MediaConstraints& /*videoConstraints*/)
{
    notImplemented();
}

void RealtimeMediaSourceCenterQt5WebRTC::createMediaStream(MediaStreamCreationClient* client, const String& audioDeviceID, const String& videoDeviceID)
{
    ASSERT(client);

    RefPtr<RealtimeMediaSourceQt5WebRTC> audioSource = findSource(audioDeviceID, RealtimeMediaSource::Audio);
    RefPtr<RealtimeMediaSourceQt5WebRTC> videoSource = findSource(videoDeviceID, RealtimeMediaSource::Video);

    m_sourceMap.clear();

    if (!audioSource && !videoSource) {
        client->failedToCreateStreamWithPermissionError();
        return;
    }

    String audioSourceName = audioSource ? audioSource->name() : String();
    String videoSourceName = videoSource ? videoSource->name() : String();

    std::shared_ptr<WRTCInt::RTCMediaStream> rtcStream(
        getRTCMediaSourceCenter().createMediaStream(
            audioSourceName.utf8().data(), videoSourceName.utf8().data()));

    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;

    if (audioSource) {
        audioSource->setRTCStream(rtcStream);
        audioSources.append(audioSource.release());
    }

    if (videoSource) {
        videoSource->setRTCStream(rtcStream);
        videoSources.append(videoSource.release());
    }

    String id = rtcStream->id().c_str();
    client->didCreateStream(MediaStreamPrivate::create(id, audioSources, videoSources));
}

bool RealtimeMediaSourceCenterQt5WebRTC::getMediaStreamTrackSources(PassRefPtr<MediaStreamTrackSourcesRequestClient> prpClient)
{
    RefPtr<MediaStreamTrackSourcesRequestClient> requestClient = prpClient;

    RealtimeMediaSourceQt5WebRTCMap sourceMap = enumerateSources(true, true);
    TrackSourceInfoVector sources;
    for (auto& source : sourceMap.values()) {
        RefPtr<TrackSourceInfo> info = TrackSourceInfo::create(
            source->persistentID(),
            source->id(),
            source->type() == RealtimeMediaSource::Type::Video ? TrackSourceInfo::SourceKind::Video : TrackSourceInfo::SourceKind::Audio,
            source->name());
        sources.append(info);
    }

    callOnMainThread([this, requestClient, sources] {
        requestClient->didCompleteTrackSourceInfoRequest(sources);
    });

    return true;
}

RefPtr<TrackSourceInfo> RealtimeMediaSourceCenterQt5WebRTC::sourceWithUID(const String& /*UID*/, RealtimeMediaSource::Type, MediaConstraints* /*constraints*/)
{
    notImplemented();
    return nullptr;
}


RefPtr<RealtimeMediaSourceQt5WebRTC> RealtimeMediaSourceCenterQt5WebRTC::findSource(const String& id, RealtimeMediaSource::Type type)
{
    if (!id.isEmpty()) {
        auto sourceIterator = m_sourceMap.find(id);
        if (sourceIterator != m_sourceMap.end()) {
            RefPtr<RealtimeMediaSourceQt5WebRTC> source = sourceIterator->value;
            if (source->type() == type)
                return source.release();
        }
    }
    return nullptr;
}

RealtimeMediaSourceQt5WebRTCMap RealtimeMediaSourceCenterQt5WebRTC::enumerateSources(bool needsAudio, bool needsVideo)
{
    RealtimeMediaSourceQt5WebRTCMap sourceMap;

    if (needsAudio) {
        std::vector<std::string> audioDevices;
        WRTCInt::enumerateDevices(WRTCInt::AUDIO, audioDevices);
        for (auto& device : audioDevices) {
            String name(device.c_str());
            String id(createCanonicalUUIDString());
            printf("audio device id='%s', name='%s'\n", id.utf8().data(), name.utf8().data());
            RefPtr<RealtimeMediaSourceQt5WebRTC> audioSource = adoptRef(new RealtimeAudioSourceQt5WebRTC(id, name));
            sourceMap.add(id, audioSource.release());
        }
    }

    if (needsVideo) {
        std::vector<std::string> videoDevices;
        WRTCInt::enumerateDevices(WRTCInt::VIDEO, videoDevices);
        for (auto& device : videoDevices) {
            String name(device.c_str());
            String id(createCanonicalUUIDString());
            printf("video device id='%s', name='%s'\n", id.utf8().data(), name.utf8().data());
            RefPtr<RealtimeMediaSourceQt5WebRTC> videoSource = adoptRef(new RealtimeVideoSourceQt5WebRTC(id, name));
            sourceMap.add(id, videoSource.release());
        }
    }
    return WTFMove(sourceMap);
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

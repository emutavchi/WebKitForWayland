#include "config.h"
#include "MediaPlayerPrivateQt5WebRTC.h"
#include "RealtimeMediaSourceCenterQt5WebRTC.h"

#include <wtf/HashSet.h>
#include <wtf/text/WTFString.h>
#include <wtf/NeverDestroyed.h>

#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "TimeRanges.h"
#include "MediaStreamPrivate.h"

namespace WebCore {

void MediaPlayerPrivateQt5WebRTC::getSupportedTypes(HashSet<String, ASCIICaseInsensitiveHash>& types)
{
    static NeverDestroyed<HashSet<String, ASCIICaseInsensitiveHash>> cache;
    types = cache;
}

MediaPlayer::SupportsType MediaPlayerPrivateQt5WebRTC::supportsType(const MediaEngineSupportParameters& parameters)
{
    if (parameters.isMediaStream)
        return MediaPlayer::IsSupported;
    return MediaPlayer::IsNotSupported;
}

void MediaPlayerPrivateQt5WebRTC::registerMediaEngine(MediaEngineRegistrar registrar)
{
    registrar([](MediaPlayer* player) { return std::make_unique<MediaPlayerPrivateQt5WebRTC>(player); },
            getSupportedTypes, supportsType, 0, 0, 0,
              [](const String&, const String&) { return false; });
}

MediaPlayerPrivateQt5WebRTC::MediaPlayerPrivateQt5WebRTC(MediaPlayer* player)
    : m_player(player)
{ }

MediaPlayerPrivateQt5WebRTC::~MediaPlayerPrivateQt5WebRTC()
{
    m_player = 0;
}

void MediaPlayerPrivateQt5WebRTC::load(MediaStreamPrivate& stream)
{
    m_stream = &stream;
    m_player->readyStateChanged();
}

FloatSize MediaPlayerPrivateQt5WebRTC::naturalSize() const
{
    if (!m_stream)
        return FloatSize();
    return m_stream->intrinsicSize();
}

void MediaPlayerPrivateQt5WebRTC::setSize(const IntSize& size)
{
    m_size = size;
}

void MediaPlayerPrivateQt5WebRTC::play()
{
    if (!m_stream || !m_stream->isProducingData())
        return;
    m_paused = false;
    MediaStreamTrackPrivate *videoTrack = m_stream->activeVideoTrack();
    if (videoTrack) {
        RealtimeVideoSourceQt5WebRTC& videoSource = static_cast<RealtimeVideoSourceQt5WebRTC&>(videoTrack->source());
        videoSource.startRenderer();
    }
}

PlatformLayer* MediaPlayerPrivateQt5WebRTC::platformLayer() const
{
    if (m_stream)
        return m_stream->platformLayer();
    return nullptr;
}

}

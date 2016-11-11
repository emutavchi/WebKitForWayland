#include "config.h"
#include "MediaPlayerPrivateQt5WebRTC.h"
#include "RealtimeMediaSourceCenterQt5WebRTC.h"

#include <wtf/HashSet.h>
#include <wtf/text/WTFString.h>
#include <wtf/NeverDestroyed.h>

#if USE(COORDINATED_GRAPHICS_THREADED)
#include "TextureMapperGL.h"
#include "TextureMapperPlatformLayerBuffer.h"
#endif

#include "BitmapImage.h"
#include "BitmapTextureGL.h"
#include "FloatRect.h"

#include <cairo.h>

#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "TimeRanges.h"
#include "MediaStreamPrivate.h"

namespace {

class ConditionNotifier {
public:
    ConditionNotifier(Lock& lock, Condition& condition)
        : m_locker(lock), m_condition(condition)
    {
    }
    ~ConditionNotifier()
    {
        m_condition.notifyOne();
    }
private:
    LockHolder m_locker;
    Condition& m_condition;
};

}  // namespace

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
    , m_size(320, 240)
{
#if USE(COORDINATED_GRAPHICS_THREADED)
    m_platformLayerProxy = adoptRef(new TextureMapperPlatformLayerProxy());
    LockHolder locker(m_platformLayerProxy->lock());
    m_platformLayerProxy->pushNextBuffer(
        std::make_unique<TextureMapperPlatformLayerBuffer>(
            0, m_size, TextureMapperGL::ShouldOverwriteRect, GraphicsContext3D::DONT_CARE));
#endif
}

MediaPlayerPrivateQt5WebRTC::~MediaPlayerPrivateQt5WebRTC()
{
    removeRenderer();
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
    if (size == m_size)
        return;
    m_size = size;
    updateVideoRectangle();
}

void MediaPlayerPrivateQt5WebRTC::setPosition(const IntPoint& position)
{
    if (position == m_position)
        return;
    m_position = position;
    updateVideoRectangle();
}

void MediaPlayerPrivateQt5WebRTC::play()
{
    if (!m_stream || !m_stream->isProducingData())
        return;
    m_paused = false;
    tryAttachRenderer();
}

void MediaPlayerPrivateQt5WebRTC::pause()
{
    m_paused = true;
    removeRenderer();
}

void MediaPlayerPrivateQt5WebRTC::setVisible(bool visible)
{
    if (visible)
        tryAttachRenderer();
    else
        removeRenderer();
}

void MediaPlayerPrivateQt5WebRTC::updateVideoRectangle()
{
    if (m_rtcRenderer)
        m_rtcRenderer->setVideoRectangle(m_position.x(), m_position.y(), m_size.width(), m_size.height());
}

void MediaPlayerPrivateQt5WebRTC::tryAttachRenderer()
{
    if (m_rtcRenderer)
        return;

    if (!m_stream || !m_stream->isProducingData())
        return;

    if (m_paused || !m_player->visible())
        return;

    MediaStreamTrackPrivate *videoTrack = m_stream->activeVideoTrack();
    if (!videoTrack)
        return;

    RealtimeVideoSourceQt5WebRTC& videoSource = static_cast<RealtimeVideoSourceQt5WebRTC&>(videoTrack->source());
    m_rtcRenderer.reset(getRTCMediaSourceCenter().createVideoRenderer(videoSource.rtcStream(), this));
    videoSource.addObserver(this);

    updateVideoRectangle();
}

void MediaPlayerPrivateQt5WebRTC::removeRenderer()
{
    if (!m_rtcRenderer)
        return;

    m_rtcRenderer.reset();

    MediaStreamTrackPrivate *videoTrack = m_stream ? m_stream->activeVideoTrack() : nullptr;
    if (videoTrack) {
        RealtimeVideoSourceQt5WebRTC& videoSource = static_cast<RealtimeVideoSourceQt5WebRTC&>(videoTrack->source());
        videoSource.removeObserver(this);
    }
}

void MediaPlayerPrivateQt5WebRTC::renderFrame(const unsigned char *data, int byteCount, int width, int height)
{
#if USE(COORDINATED_GRAPHICS_THREADED)
    cairo_format_t cairoFormat = CAIRO_FORMAT_ARGB32;
    int stride = cairo_format_stride_for_width (cairoFormat, width);
    ASSERT(byteCount >= (height * stride));
    RefPtr<cairo_surface_t> surface = adoptRef(
        cairo_image_surface_create_for_data(
            (unsigned char*)data, cairoFormat, width, height, stride));
    ASSERT(cairo_surface_status(surface.get()) == CAIRO_STATUS_SUCCESS);
    RefPtr<BitmapImage> frame = BitmapImage::create(WTFMove(surface));
    LockHolder lock(m_drawMutex);
    bool succeeded = m_platformLayerProxy->scheduleUpdateOnCompositorThread([this, frame] {
        ConditionNotifier notifier(m_drawMutex, m_drawCondition);
        pushTextureToCompositor(frame);
    });
    if (succeeded) {
        m_drawCondition.wait(m_drawMutex);
    } else {
        printf("***Error: scheduleUpdateOnCompositorThread failed\n");
    }
#endif
}

void MediaPlayerPrivateQt5WebRTC::punchHole(int width, int height)
{
#if USE(COORDINATED_GRAPHICS_THREADED)
    LockHolder lock(m_drawMutex);
    bool succeeded = m_platformLayerProxy->scheduleUpdateOnCompositorThread([this, width, height] {
        ConditionNotifier notifier(m_drawMutex, m_drawCondition);
        LockHolder holder(m_platformLayerProxy->lock());
        m_platformLayerProxy->pushNextBuffer(
            std::make_unique<TextureMapperPlatformLayerBuffer>(
                0, IntSize(width, height), TextureMapperGL::ShouldOverwriteRect, GraphicsContext3D::DONT_CARE));
    });
    if (succeeded) {
        m_drawCondition.wait(m_drawMutex);
    } else {
        printf("***Error: scheduleUpdateOnCompositorThread failed\n");
    }
#endif
}

#if USE(COORDINATED_GRAPHICS_THREADED)
void MediaPlayerPrivateQt5WebRTC::pushTextureToCompositor(RefPtr<Image> frame)
{
    LockHolder holder(m_platformLayerProxy->lock());
    if (!m_platformLayerProxy->isActive()) {
        printf("***Error: platformLayerProxy is not ready yet\n");
        return;
    }
    IntSize size(frame->width(), frame->height());
    std::unique_ptr<TextureMapperPlatformLayerBuffer> buffer =
        m_platformLayerProxy->getAvailableBuffer(size, GraphicsContext3D::DONT_CARE);
    if (UNLIKELY(!buffer)) {
        if (UNLIKELY(!m_context3D))
            m_context3D = GraphicsContext3D::create(
                GraphicsContext3D::Attributes(), nullptr, GraphicsContext3D::RenderToCurrentGLContext);
        RefPtr<BitmapTexture> texture = adoptRef(new BitmapTextureGL(m_context3D.copyRef()));
        texture->reset(size, BitmapTexture::SupportsAlpha);
        buffer = std::make_unique<TextureMapperPlatformLayerBuffer>(WTFMove(texture));
    }
    IntRect rect(IntPoint(),size);
    buffer->textureGL().updateContents(
        frame.get(), rect, /*offset*/ IntPoint(),
        BitmapTexture::UpdateContentsFlag::UpdateCanModifyOriginalImageData);
    m_platformLayerProxy->pushNextBuffer(WTFMove(buffer));
}
#endif

void MediaPlayerPrivateQt5WebRTC::sourceStopped()
{
    removeRenderer();
}

void MediaPlayerPrivateQt5WebRTC::sourceMutedChanged()
{
    // Ignored
}

void MediaPlayerPrivateQt5WebRTC::sourceSettingsChanged()
{
    // Ignored
}

}

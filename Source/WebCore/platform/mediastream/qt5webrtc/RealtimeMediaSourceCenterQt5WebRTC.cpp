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

#if USE(COORDINATED_GRAPHICS_THREADED)
#include "TextureMapperGL.h"
#include "TextureMapperPlatformLayerBuffer.h"
#endif

#include "BitmapImage.h"
#include "BitmapTextureGL.h"
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
    , m_size(320, 240)
{
#if USE(COORDINATED_GRAPHICS_THREADED)
    m_platformLayerProxy = adoptRef(new TextureMapperPlatformLayerProxy());
    LockHolder locker(m_platformLayerProxy->lock());
    m_platformLayerProxy->pushNextBuffer(std::make_unique<TextureMapperPlatformLayerBuffer>(0, m_size, TextureMapperGL::ShouldOverwriteRect));
#endif
    m_currentSettings.setWidth(m_size.width());
    m_currentSettings.setHeight(m_size.height());
}

void RealtimeVideoSourceQt5WebRTC::stopProducingData()
{
    stopRenderer();
    RealtimeMediaSourceQt5WebRTC::stopProducingData();
}

void RealtimeVideoSourceQt5WebRTC::startRenderer()
{
    if (isProducingData() && !m_rtcRenderer) {
        m_rtcRenderer.reset(getRTCMediaSourceCenter().createVideoRenderer(rtcStream(), this));
    }
}

void RealtimeVideoSourceQt5WebRTC::stopRenderer()
{
    m_rtcRenderer.reset();
}

void RealtimeVideoSourceQt5WebRTC::renderFrame(const unsigned char *data, int byteCount, int width, int height)
{
    if (!isProducingData() || muted())
        return;
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
        this->pushTextureToCompositor(frame);
    });
    if (succeeded) {
        m_drawCondition.wait(m_drawMutex);
    } else {
        printf("***Error: scheduleUpdateOnCompositorThread failed\n");
    }
#endif
}

#if USE(COORDINATED_GRAPHICS_THREADED)
void RealtimeVideoSourceQt5WebRTC::pushTextureToCompositor(RefPtr<Image> frame)
{
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
    ConditionNotifier notifier(m_drawMutex, m_drawCondition);
    LockHolder holder(m_platformLayerProxy->lock());
    if (!m_platformLayerProxy->isActive()) {
        printf("***Error: platformLayerProxy is not ready yet\n");
        return;
    }
    // TODO: update m_size
    IntSize size(frame->width(), frame->height());
    std::unique_ptr<TextureMapperPlatformLayerBuffer> buffer =
        m_platformLayerProxy->getAvailableBuffer(size, GraphicsContext3D::DONT_CARE);
    if (UNLIKELY(!buffer)) {
        if (UNLIKELY(!m_context3D))
            m_context3D = GraphicsContext3D::create(GraphicsContext3D::Attributes(),
                                                    nullptr, GraphicsContext3D::RenderToCurrentGLContext);
        RefPtr<BitmapTexture> texture = adoptRef(new BitmapTextureGL(m_context3D));
        texture->reset(size, BitmapTexture::SupportsAlpha);
        buffer = std::make_unique<TextureMapperPlatformLayerBuffer>(WTFMove(texture));
    }

    IntRect rect(IntPoint(),size);
    buffer->textureGL().updateContents(frame.get(),
                                       rect, /*offset*/ IntPoint(),
                                       BitmapTexture::UpdateContentsFlag::UpdateCanModifyOriginalImageData);

    m_platformLayerProxy->pushNextBuffer(WTFMove(buffer));
}
#endif


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

void RealtimeMediaSourceCenterQt5WebRTC::validateRequestConstraints(MediaStreamCreationClient* client, RefPtr<MediaConstraints>& audioConstraints, RefPtr<MediaConstraints>& videoConstraints)
{
    ASSERT(client);

    bool needsAudio = !!audioConstraints;
    bool needsVideo = !!videoConstraints;

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
                                                           PassRefPtr<MediaConstraints> /*audioConstraints*/,
                                                           PassRefPtr<MediaConstraints> /*videoConstraints*/)
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

    client->didCreateStream(MediaStreamPrivate::create(audioSources, videoSources));
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

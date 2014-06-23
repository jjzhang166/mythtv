#include "openglvideo.h"
#include "mythlogging.h"
#include "mythxdisplay.h"
#include "mythcodecid.h"
#include "mythframe.h"
#include "vaapicontext.h"
#include "mythmainwindow.h"
#include "myth_imgconvert.h"
#include <GL/gl.h>
#include <GL/glx.h>

#define LOC QString("VAAPI: ")
#define ERR QString("VAAPI Error: ")
#define NUM_VAAPI_BUFFERS 24

#define INIT_ST \
  VAStatus va_status; \
  bool ok = true

#define CHECK_ST \
  ok &= (va_status == VA_STATUS_SUCCESS); \
  if (!ok) \
      LOG(VB_GENERAL, LOG_ERR, LOC + QString("Error at %1:%2 (#%3, %4)") \
              .arg(__FILE__).arg( __LINE__).arg(va_status) \
              .arg(vaErrorStr(va_status)))

#define CREATE_CHECK(arg1, arg2) \
  if (ok) \
  { \
      ok = arg1; \
      if (!ok) \
          LOG(VB_GENERAL, LOG_ERR, LOC + arg2); \
  } while(0)

QString profileToString(VAProfile profile);
QString entryToString(VAEntrypoint entry);
VAProfile preferredProfile(MythCodecID codec);

class VAAPIRenderedPicture
{
public:
    Display *m_display;
    Pixmap m_pixmap;
    GLXPixmap m_glpixmap;
    GLuint m_texture;
    GLenum m_textureTarget;
};

QString profileToString(VAProfile profile)
{
    if (VAProfileMPEG2Simple == profile)         return "MPEG2Simple";
    if (VAProfileMPEG2Main == profile)           return "MPEG2Main";
    if (VAProfileMPEG4Simple == profile)         return "MPEG4Simple";
    if (VAProfileMPEG4AdvancedSimple == profile) return "MPEG4AdvSimple";
    if (VAProfileMPEG4Main == profile)           return "MPEG4Main";
    if (VAProfileH264Baseline == profile)        return "H264Base";
    if (VAProfileH264Main == profile)            return "H264Main";
    if (VAProfileH264High == profile)            return "H264High";
    if (VAProfileVC1Simple == profile)           return "VC1Simple";
    if (VAProfileVC1Main == profile)             return "VC1Main";
    if (VAProfileVC1Advanced == profile)         return "VC1Advanced";
    if (VAProfileH263Baseline == profile)        return "H263Base";
    return "Unknown";
}

QString entryToString(VAEntrypoint entry)
{
    if (VAEntrypointVLD == entry)        return "VLD ";
    if (VAEntrypointIZZ == entry)        return "IZZ (UNSUPPORTED) ";
    if (VAEntrypointIDCT == entry)       return "IDCT (UNSUPPORTED) ";
    if (VAEntrypointMoComp == entry)     return "MC (UNSUPPORTED) ";
    if (VAEntrypointDeblocking == entry) return "Deblock (UNSUPPORTED) ";
    if (VAEntrypointEncSlice == entry)   return "EncSlice (UNSUPPORTED) ";
    return "Unknown";
}

VAProfile preferredProfile(MythCodecID codec)
{
    if (kCodec_H263_VAAPI  == codec) return VAProfileMPEG4AdvancedSimple;
    if (kCodec_MPEG4_VAAPI == codec) return VAProfileMPEG4AdvancedSimple;
    if (kCodec_H264_VAAPI  == codec) return VAProfileH264High;
    if (kCodec_VC1_VAAPI   == codec) return VAProfileVC1Advanced;
    if (kCodec_WMV3_VAAPI  == codec) return VAProfileVC1Main;
    if (kCodec_MPEG2_VAAPI == codec) return VAProfileMPEG2Main;
    if (kCodec_MPEG1_VAAPI == codec) return VAProfileMPEG2Main;
    return VAProfileMPEG2Simple; // error
}

class VAAPIDisplay : public ReferenceCounter
{
  protected:
    VAAPIDisplay(VAAPIDisplayType display_type, MythRenderOpenGL *render) :
        ReferenceCounter("VAAPIDisplay"),
        m_va_disp_type(display_type),
        m_va_disp(NULL), m_x_disp(NULL),
        m_driver(), m_render(render) { }
  public:
    virtual ~VAAPIDisplay()
    {
        if (m_va_disp)
        {
            INIT_ST;
            XLOCK(m_x_disp, va_status = vaTerminate(m_va_disp));
            CHECK_ST;
        }
        if (m_x_disp)
        {
            m_x_disp->Sync(true);
            delete m_x_disp;
        }
    }

    bool Create(void)
    {
        m_x_disp = OpenMythXDisplay();
        if (!m_x_disp)
            return false;

        MythXLocker locker(m_x_disp);

        m_va_disp = vaGetDisplay(m_x_disp->GetDisplay());

        if (!m_va_disp)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to create VADisplay");
            return false;
        }

        int major_ver, minor_ver;
        INIT_ST;
        va_status = vaInitialize(m_va_disp, &major_ver, &minor_ver);
        CHECK_ST;

        if (ok)
            m_driver = vaQueryVendorString(m_va_disp);

        static bool debugged = false;
        if (ok && !debugged)
        {
            debugged = true;
            LOG(VB_GENERAL, LOG_INFO, LOC + QString("Version: %1.%2")
                                        .arg(major_ver).arg(minor_ver));
            LOG(VB_GENERAL, LOG_INFO, LOC + QString("Driver : %1").arg(m_driver));
        }
        if (ok)
        {
            LOG(VB_PLAYBACK, LOG_INFO, LOC +
                QString("Created VAAPI X11 display"));
        }
        return ok;
    }

    QString GetDriver(void)
    {
        QString ret = m_driver;
        m_driver.detach();
        return ret;
    }

    static VAAPIDisplay *GetDisplay(VAAPIDisplayType display_type, bool noreuse,
                                    MythRenderOpenGL *render)
    {
        VAAPIDisplay *tmp = new VAAPIDisplay(display_type, render);
        if (tmp->Create())
        {
            return tmp;
        }
        tmp->DecrRef();
        return NULL;
    }

    VAAPIDisplayType     m_va_disp_type;
    void                *m_va_disp;
    MythXDisplay        *m_x_disp;
    QString              m_driver;
    MythRenderOpenGL    *m_render;
};

bool VAAPIContext::IsFormatAccelerated(QSize size, MythCodecID codec,
                                       PixelFormat &pix_fmt)
{
    bool result = false;
    VAAPIContext *ctx = new VAAPIContext(kVADisplayX11, codec);
    if (ctx->CreateDisplay(size, true))
    {
        pix_fmt = ctx->GetPixelFormat();
        result = pix_fmt == PIX_FMT_VAAPI_VLD;
    }
    delete ctx;
    return result;
}

VAAPIContext::VAAPIContext(VAAPIDisplayType display_type,
                           MythCodecID codec)
  : m_dispType(display_type),
    m_codec(codec),
    m_display(NULL),
    m_vaProfile(VAProfileMPEG2Main)/* ?? */,
    m_vaEntrypoint(VAEntrypointEncSlice),
    m_pix_fmt(PIX_FMT_YUV420P), m_numSurfaces(NUM_VAAPI_BUFFERS),
    m_surfaces(NULL), m_surfaceData(NULL), m_pictureAttributes(NULL),
    m_pictureAttributeCount(0), m_hueBase(0), m_deriveSupport(false),
    m_copy(NULL)
{
    memset(&m_ctx, 0, sizeof(vaapi_context));
    memset(&m_image, 0, sizeof(m_image));
    m_image.image_id = VA_INVALID_ID;
}

VAAPIContext::~VAAPIContext()
{
    delete [] m_pictureAttributes;

    ClearPixmaps();

    if (m_display)
    {
        m_display->m_x_disp->Lock();

        INIT_ST;

        if (m_image.image_id != VA_INVALID_ID)
        {
            va_status = vaDestroyImage(m_ctx.display, m_image.image_id);
            CHECK_ST;
        }
        if (m_ctx.context_id)
        {
            va_status = vaDestroyContext(m_ctx.display, m_ctx.context_id);
            CHECK_ST;
        }
        if (m_ctx.config_id)
        {
            va_status = vaDestroyConfig(m_ctx.display, m_ctx.config_id);
            CHECK_ST;
        }
        if (m_surfaces)
        {
            va_status = vaDestroySurfaces(m_ctx.display, m_surfaces, m_numSurfaces);
            CHECK_ST;
        }
    }

    if (m_surfaces)
        delete [] m_surfaces;
    if (m_surfaceData)
        delete [] m_surfaceData;

    if (m_display)
    {
        m_display->m_x_disp->Unlock();
        m_display->DecrRef();
    }

    delete m_copy;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "Deleted context");
}

bool VAAPIContext::CreateDisplay(QSize size, bool noreuse,
                                 MythRenderOpenGL *render)
{
    m_size = size;
    if (!m_copy)
    {
        m_copy = new MythUSWCCopy(m_size.width());
    }
    else
    {
        m_copy->reset(m_size.width());
    }

    bool ok = true;
    m_display = VAAPIDisplay::GetDisplay(m_dispType, noreuse, render);
    CREATE_CHECK(!m_size.isEmpty(), "Invalid size");
    CREATE_CHECK(m_display != NULL, "Invalid display");
    CREATE_CHECK(InitDisplay(),     "Invalid VADisplay");
    CREATE_CHECK(InitProfiles(),    "No supported profiles");
    if (ok)
        LOG(VB_PLAYBACK, LOG_INFO, LOC +
            QString("Created context (%1x%2->%3x%4)")
            .arg(size.width()).arg(size.height())
            .arg(m_size.width()).arg(m_size.height()));
    // ATI hue adjustment
    if (m_display)
        m_hueBase = VideoOutput::CalcHueBase(m_display->GetDriver());

    return ok;
}

void VAAPIContext::InitPictureAttributes(VideoColourSpace &colourspace)
{
    if (!m_display)
        return;
    if (!m_display->m_va_disp)
        return;

    delete [] m_pictureAttributes;
    m_pictureAttributeCount = 0;
    int supported_controls = kPictureAttributeSupported_None;
    QList<VADisplayAttribute> supported;
    int num = vaMaxNumDisplayAttributes(m_display->m_va_disp);
    VADisplayAttribute* attribs = new VADisplayAttribute[num];

    int actual = 0;
    INIT_ST;
    va_status = vaQueryDisplayAttributes(m_display->m_va_disp, attribs, &actual);
    CHECK_ST;

    for (int i = 0; i < actual; i++)
    {
        int type = attribs[i].type;
        if ((attribs[i].flags & VA_DISPLAY_ATTRIB_SETTABLE) &&
            (type == VADisplayAttribBrightness ||
             type == VADisplayAttribContrast ||
             type == VADisplayAttribHue ||
             type == VADisplayAttribSaturation))
        {
            supported.push_back(attribs[i]);
            if (type == VADisplayAttribBrightness)
                supported_controls += kPictureAttributeSupported_Brightness;
            if (type == VADisplayAttribHue)
                supported_controls += kPictureAttributeSupported_Hue;
            if (type == VADisplayAttribContrast)
                supported_controls += kPictureAttributeSupported_Contrast;
            if (type == VADisplayAttribSaturation)
                supported_controls += kPictureAttributeSupported_Colour;
        }
    }

    colourspace.SetSupportedAttributes((PictureAttributeSupported)supported_controls);
    delete [] attribs;

    if (supported.isEmpty())
        return;

    m_pictureAttributeCount = supported.size();
    m_pictureAttributes = new VADisplayAttribute[m_pictureAttributeCount];
    for (int i = 0; i < m_pictureAttributeCount; i++)
        m_pictureAttributes[i] = supported.at(i);

    if (supported_controls & kPictureAttributeSupported_Brightness)
        SetPictureAttribute(kPictureAttribute_Brightness,
            colourspace.GetPictureAttribute(kPictureAttribute_Brightness));
    if (supported_controls & kPictureAttributeSupported_Hue)
        SetPictureAttribute(kPictureAttribute_Hue,
            colourspace.GetPictureAttribute(kPictureAttribute_Hue));
    if (supported_controls & kPictureAttributeSupported_Contrast)
        SetPictureAttribute(kPictureAttribute_Contrast,
            colourspace.GetPictureAttribute(kPictureAttribute_Contrast));
    if (supported_controls & kPictureAttributeSupported_Colour)
        SetPictureAttribute(kPictureAttribute_Colour,
            colourspace.GetPictureAttribute(kPictureAttribute_Colour));
}

int VAAPIContext::SetPictureAttribute(PictureAttribute attribute, int newValue)
{
    if (!m_display)
        return newValue;
    if (!m_display->m_va_disp)
        return newValue;

    int adj = 0;
    VADisplayAttribType attrib = VADisplayAttribBrightness;
    switch (attribute)
    {
        case kPictureAttribute_Brightness:
            attrib = VADisplayAttribBrightness;
            break;
        case kPictureAttribute_Contrast:
            attrib = VADisplayAttribContrast;
            break;
        case kPictureAttribute_Hue:
            attrib = VADisplayAttribHue;
            adj = m_hueBase;
            break;
        case kPictureAttribute_Colour:
            attrib = VADisplayAttribSaturation;
            break;
        default:
            return -1;
    }

    bool found = false;
    for (int i = 0; i < m_pictureAttributeCount; i++)
    {
        if (m_pictureAttributes[i].type == attrib)
        {
            int min = m_pictureAttributes[i].min_value;
            int max = m_pictureAttributes[i].max_value;
            int val = min + (int)(((float)((newValue + adj) % 100) / 100.0) * (max - min));
            m_pictureAttributes[i].value = val;
            found = true;
            break;
        }
    }

    if (found)
    {
        INIT_ST;
        va_status = vaSetDisplayAttributes(m_display->m_va_disp,
                                           m_pictureAttributes,
                                           m_pictureAttributeCount);
        CHECK_ST;
        return newValue;
    }
    return -1;
}

bool VAAPIContext::CreateBuffers(void)
{
    bool ok = true;
    CREATE_CHECK(!m_size.isEmpty(), "Invalid size");
    CREATE_CHECK(InitBuffers(),     "Failed to create buffers.");
    CREATE_CHECK(InitContext(),     "Failed to create context");
    if (ok)
        LOG(VB_PLAYBACK, LOG_INFO, LOC +
            QString("Created %1 buffers").arg(m_numSurfaces));
    return ok;
}

bool VAAPIContext::InitDisplay(void)
{
    if (!m_display)
        return false;
    m_ctx.display = m_display->m_va_disp;
    return m_ctx.display;
}

bool VAAPIContext::InitProfiles(void)
{
    if (!(codec_is_vaapi_hw(m_codec)) || !m_ctx.display)
        return false;

    MythXLocker locker(m_display->m_x_disp);
    int max_profiles, max_entrypoints;
    VAProfile profile_wanted = preferredProfile(m_codec);
    if (profile_wanted == VAProfileMPEG2Simple)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "Codec is not supported.");
        return false;
    }

    VAProfile profile_found  = VAProfileMPEG2Simple; // unsupported value
    VAEntrypoint entry_found = VAEntrypointEncSlice; // unsupported value

    max_profiles          = vaMaxNumProfiles(m_ctx.display);
    max_entrypoints       = vaMaxNumEntrypoints(m_ctx.display);
    VAProfile *profiles   = new VAProfile[max_profiles];
    VAEntrypoint *entries = new VAEntrypoint[max_entrypoints];

    static bool debugged = false;
    if (profiles && entries)
    {
        INIT_ST;
        int act_profiles, act_entries;
        va_status = vaQueryConfigProfiles(m_ctx.display,
                                          profiles,
                                         &act_profiles);
        CHECK_ST;
        if (ok && act_profiles > 0)
        {
            for (int i = 0; i < act_profiles; i++)
            {
                va_status = vaQueryConfigEntrypoints(m_ctx.display,
                                                     profiles[i],
                                                     entries,
                                                    &act_entries);
                if (va_status == VA_STATUS_SUCCESS && act_entries > 0)
                {
                    if (profiles[i] == profile_wanted)
                    {
                        profile_found = profile_wanted;
                        for (int j = 0; j < act_entries; j++)
                            if (entries[j] < entry_found)
                                entry_found = entries[j];
                    }

                    if (!debugged)
                    {
                        QString entrylist = "Entrypoints: ";
                        for (int j = 0; j < act_entries; j++)
                            entrylist += entryToString(entries[j]);
                        LOG(VB_GENERAL, LOG_INFO, LOC +
                            QString("Profile: %1 %2")
                                .arg(profileToString(profiles[i]))
                                .arg(entrylist));
                    }
                }
            }
        }
        debugged = true;
    }
    delete [] profiles;
    delete [] entries;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Desired profile for '%1': %2")
        .arg(toString(m_codec)).arg(profileToString(profile_wanted)));
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Found profile %1 with entry %2")
        .arg(profileToString(profile_found)).arg(entryToString(entry_found)));

    if (profile_wanted != profile_found)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to find supported profile.");
        return false;
    }

    if (entry_found > VAEntrypointVLD)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to find suitable entry point.");
        return false;
    }

    m_vaProfile = profile_wanted;
    m_vaEntrypoint = entry_found;
    if (VAEntrypointVLD == m_vaEntrypoint)
        m_pix_fmt = PIX_FMT_VAAPI_VLD;
    return true;
}

bool VAAPIContext::InitBuffers(void)
{
    if (!m_ctx.display)
        return false;

    MythXLocker locker(m_display->m_x_disp);
    m_surfaces    = new VASurfaceID[m_numSurfaces];
    m_surfaceData = new vaapi_surface[m_numSurfaces];

    if (!m_surfaces || !m_surfaceData)
        return false;

    memset(m_surfaces, 0, m_numSurfaces * sizeof(VASurfaceID));
    memset(m_surfaceData, 0, m_numSurfaces * sizeof(vaapi_surface));

    INIT_ST;
    va_status = vaCreateSurfaces(m_ctx.display, m_size.width(), m_size.height(),
                                 VA_RT_FORMAT_YUV420, m_numSurfaces,
                                 m_surfaces);
    CHECK_ST;

    for (int i = 0; i < m_numSurfaces; i++)
        m_surfaceData[i].m_id = m_surfaces[i];
    return ok;
}

bool VAAPIContext::InitContext(void)
{
    if (!m_ctx.display || m_vaEntrypoint > VAEntrypointVLD)
        return false;

    MythXLocker locker(m_display->m_x_disp);
    VAConfigAttrib attrib;
    attrib.type = VAConfigAttribRTFormat;
    INIT_ST;
    va_status = vaGetConfigAttributes(m_ctx.display, m_vaProfile,
                                      m_vaEntrypoint, &attrib, 1);
    CHECK_ST;

    if (!ok || !(attrib.value & VA_RT_FORMAT_YUV420))
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to confirm YUV420 chroma");
        return false;
    }

    va_status = vaCreateConfig(m_ctx.display, m_vaProfile, m_vaEntrypoint,
                               &attrib, 1, &m_ctx.config_id);
    CHECK_ST;
    if (!ok)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to create decoder config.");
        return false;
    }

    va_status = vaCreateContext(m_ctx.display, m_ctx.config_id,
                                m_size.width(), m_size.height(), VA_PROGRESSIVE,
                                m_surfaces, m_numSurfaces,
                                &m_ctx.context_id);
    CHECK_ST;
    if (!ok)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to create decoder context.");
        return false;
    }
    return true;
}

void* VAAPIContext::GetVideoSurface(int i)
{
    if (i < 0 || i >= m_numSurfaces)
        return NULL;
    return &m_surfaceData[i];
}

uint8_t* VAAPIContext::GetSurfaceIDPointer(void* buf)
{
    if (!buf)
        return NULL;

    const vaapi_surface *surf = (vaapi_surface*)buf;
    if (!surf->m_id)
        return NULL;

    INIT_ST;
    va_status = vaSyncSurface(m_ctx.display, surf->m_id);
    CHECK_ST;
    return (uint8_t*)(uintptr_t)surf->m_id;
}

bool VAAPIContext::InitImage(const void *buf)
{
    if (!buf)
        return false;
    if (!m_dispType == kVADisplayX11)
        return true;

    int num_formats = 0;
    int max_formats = vaMaxNumImageFormats(m_ctx.display);
    VAImageFormat *formats = new VAImageFormat[max_formats];

    INIT_ST;
    va_status = vaQueryImageFormats(m_ctx.display, formats, &num_formats);
    CHECK_ST;

    const vaapi_surface *surf = (vaapi_surface*)buf;
    unsigned int deriveImageFormat = 0;

    if (vaDeriveImage(m_ctx.display, surf->m_id, &m_image) == VA_STATUS_SUCCESS)
    {
        m_deriveSupport = true;
        deriveImageFormat = m_image.format.fourcc;
        vaDestroyImage(m_ctx.display, m_image.image_id);
    }

    int nv12support = -1;

    for (int i = 0; i < num_formats; i++)
    {
        if (formats[i].fourcc == VA_FOURCC_YV12 ||
            formats[i].fourcc == VA_FOURCC_IYUV ||
            formats[i].fourcc == VA_FOURCC_NV12)
        {
            if (vaCreateImage(m_ctx.display, &formats[i],
                              m_size.width(), m_size.height(), &m_image))
            {
                m_image.image_id = VA_INVALID_ID;
                continue;
            }

            if (vaGetImage(m_ctx.display, surf->m_id, 0, 0,
                           m_size.width(), m_size.height(), m_image.image_id))
            {
                vaDestroyImage(m_ctx.display, m_image.image_id);
                m_image.image_id = VA_INVALID_ID;
                continue;
            }

            if (formats[i].fourcc == VA_FOURCC_NV12)
            {
                // mark as NV12 as supported, but favor other formats first
                nv12support = i;
                vaDestroyImage(m_ctx.display, m_image.image_id);
                m_image.image_id = VA_INVALID_ID;
                continue;
            }
            break;
        }
    }

    if (m_image.image_id == VA_INVALID_ID && nv12support >= 0)
    {
        // only nv12 is supported, use that format
        if (vaCreateImage(m_ctx.display, &formats[nv12support],
                          m_size.width(), m_size.height(), &m_image))
        {
            m_image.image_id = VA_INVALID_ID;
        }
    }
    else if (m_deriveSupport && deriveImageFormat != m_image.format.fourcc)
    {
        // only use vaDerive if it's giving us a format we can handle natively
        m_deriveSupport = false;
    }

    delete [] formats;

    if (m_image.image_id == VA_INVALID_ID)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to create software image.");
        return false;
    }

    LOG(VB_GENERAL, LOG_DEBUG,
        LOC + QString("InitImage: id %1, width %2 height %3 "
                      "format %4 vaDeriveSupport:%5")
        .arg(m_image.image_id).arg(m_image.width).arg(m_image.height)
        .arg(m_image.format.fourcc).arg(m_deriveSupport));

    if (m_deriveSupport)
    {
        vaDestroyImage(m_ctx.display, m_image.image_id );
        m_image.image_id = VA_INVALID_ID;
    }

    return true;
}

bool VAAPIContext::CopySurfaceToFrame(VideoFrame *frame, const void *buf)
{
    MythXLocker locker(m_display->m_x_disp);

    if (!m_deriveSupport && m_image.image_id == VA_INVALID_ID)
        InitImage(buf);

    if (!frame || !buf || (m_dispType != kVADisplayX11) ||
        (!m_deriveSupport && m_image.image_id == VA_INVALID_ID))
        return false;

    const vaapi_surface *surf = (vaapi_surface*)buf;

    INIT_ST;
    va_status = vaSyncSurface(m_ctx.display, surf->m_id);
    CHECK_ST;

    if (m_deriveSupport)
    {
        va_status = vaDeriveImage(m_ctx.display, surf->m_id, &m_image);
    }
    else
    {
        va_status = vaGetImage(m_ctx.display, surf->m_id, 0, 0,
                               m_size.width(), m_size.height(),
                               m_image.image_id);
    }
    CHECK_ST;

    if (ok)
    {
        VideoFrame src;
        void* source = NULL;

        if (vaMapBuffer(m_ctx.display, m_image.buf, &source))
            return false;

        if (m_image.format.fourcc == VA_FOURCC_NV12)
        {
            init(&src, FMT_NV12, (unsigned char*)source, m_image.width,
                 m_image.height, m_image.data_size, NULL,
                 NULL, frame->aspect, frame->frame_rate);
            for (int i = 0; i < 2; i++)
            {
                src.pitches[i] = m_image.pitches[i];
                src.offsets[i] = m_image.offsets[i];
            }
        }
        else
        {
            // Our VideoFrame YV12 format, is really YUV420P/IYUV
            bool swap = m_image.format.fourcc == VA_FOURCC_YV12;
            init(&src, FMT_YV12, (unsigned char*)source, m_image.width,
                 m_image.height, m_image.data_size, NULL,
                 NULL, frame->aspect, frame->frame_rate);
            src.pitches[0] = m_image.pitches[0];
            src.pitches[1] = m_image.pitches[swap ? 2 : 1];
            src.pitches[2] = m_image.pitches[swap ? 1 : 2];
            src.offsets[0] = m_image.offsets[0];
            src.offsets[1] = m_image.offsets[swap ? 2 : 1];
            src.offsets[2] = m_image.offsets[swap ? 1 : 2];
        }
        m_copy->copy(frame, &src);

        if (vaUnmapBuffer(m_ctx.display, m_image.buf))
            return false;

        if (m_deriveSupport)
        {
            vaDestroyImage(m_ctx.display, m_image.image_id );
            m_image.image_id = VA_INVALID_ID;
        }

        return true;
    }

    LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to get image");
    return false;
}

bool VAAPIContext::CopySurfaceToTexture(const void* buf, uint texture,
                                        uint texture_type, FrameScanType scan)
{
    if (!buf)
        return false;

    VAAPIRenderedPicture *pic = GetPixmap(texture, texture_type);
    if (!pic)
        return false;

    MythXLocker locker(m_display->m_x_disp);
    Display *xdisplay = m_display->m_x_disp->GetDisplay();
    VASurfaceID surface =
        (VASurfaceID)(uintptr_t)GetSurfaceIDPointer((void*)buf);

    int field = VA_FRAME_PICTURE;
    if (scan == kScan_Interlaced)
        field = VA_TOP_FIELD;
    else if (scan == kScan_Intr2ndField)
        field = VA_BOTTOM_FIELD;

    INIT_ST;
    va_status = vaPutSurface(m_display->m_va_disp, surface,
                             pic->m_pixmap,
                             0, 0,
                             m_size.width(), m_size.height(),
                             0, 0,
                             m_size.width(), m_size.height(),
                             NULL, 0,
                             field);
    CHECK_ST;

    XSync(xdisplay, false);

    return ok;
}

VAAPIRenderedPicture* VAAPIContext::GetPixmap(uint texture, uint texture_type)
{
    if (m_dispType != kVADisplayGLX)
        return NULL;

    if (m_glxSurfaces.contains(texture))
        return m_glxSurfaces.value(texture);

    MythXLocker locker(m_display->m_x_disp);
    Display *xdisplay = m_display->m_x_disp->GetDisplay();
    Window wid = m_display->m_x_disp->GetRoot();
    XWindowAttributes wndattribs;
    const int pixmap_config[] = {
        GLX_BIND_TO_TEXTURE_RGBA_EXT, True,
        GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
        GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
        GLX_DOUBLEBUFFER, False,
        GLX_Y_INVERTED_EXT, GLX_DONT_CARE,
        None
    };
    const int pixmap_attribs[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGB_EXT,
        None
    };

    XGetWindowAttributes(xdisplay, wid, &wndattribs);

    int c = 0;
    GLXFBConfig *configs = glXChooseFBConfig(xdisplay, 0,
                                             pixmap_config, &c);
    if (configs == NULL)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            "GetGLXSurface - No compatible framebuffers found");
        return NULL;
    }

    VAAPIRenderedPicture *pic = new VAAPIRenderedPicture();
    pic->m_display = xdisplay;
    pic->m_textureTarget = texture;
    pic->m_pixmap = XCreatePixmap(xdisplay, wid,
                                  m_size.width(), m_size.height(),
                                  wndattribs.depth);
    if (!pic->m_pixmap)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            "GetGLXSurface - Unable to create XPixmap");
        return NULL;
    }

    pic->m_glpixmap = glXCreatePixmap(xdisplay, configs[0],
                                      pic->m_pixmap, pixmap_attribs);
    if (!pic->m_glpixmap)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            "GetGLXSurface - Could not create glPixmap");
        return NULL;
    }

    glEnable(texture);
    glGenTextures(1, &pic->m_texture);
    glBindTexture(texture, pic->m_texture);
    m_display->m_render->BindTexImage(xdisplay, (unsigned long)pic->m_glpixmap);
    glDisable(texture);

    m_glxSurfaces.insert(texture, pic);

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Number of VAAPI GLX surfaces: %1")
        .arg(m_glxSurfaces.size()));

    return pic;
}

void VAAPIContext::ClearPixmaps(void)
{
    if (!m_display || (m_dispType != kVADisplayGLX))
        return;

    foreach (VAAPIRenderedPicture* pic, m_glxSurfaces)
    {
        glBindTexture(pic->m_textureTarget, pic->m_texture);
        m_display->m_render->ReleaseTexImage(pic->m_display,
            (unsigned long)pic->m_glpixmap);
        glBindTexture(pic->m_textureTarget, 0);
        glDeleteTextures(1, &pic->m_texture);
        glXDestroyPixmap(pic->m_display, pic->m_glpixmap);
        XFreePixmap(pic->m_display, pic->m_pixmap);
        delete pic;
    }
    m_glxSurfaces.clear();
}

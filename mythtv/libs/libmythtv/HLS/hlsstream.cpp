/*****************************************************************************
 * hlsstream.cpp
 * MythTV
 *
 * Created by Jean-Yves Avenard on 6/05/12.
 * Copyright (c) 2012 Bubblestuff Pty Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include <sys/time.h> // for gettimeofday

#include <cstring> // for memcpy

#include <algorithm> // for max()
using namespace std;

#include "mythlogging.h"
#include "hlssegment.h"
#include "hlsstream.h"

#include <QUrl>

#ifdef USING_LIBCRYPTO
// encryption related stuff
#include <openssl/aes.h>
#define AES_BLOCK_SIZE 16       // HLS only support AES-128
#endif

#define LOC QString("HLSStream: ")

static QString relative_URI(QString &surl, QString &spath)
{
    QUrl url  = QUrl(surl);
    QUrl path = QUrl(spath);

    if (!path.isRelative())
    {
        return spath;
    }
    return url.resolved(path).toString();
}

static uint64_t mdate(void)
{
    timeval  t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000000ULL + t.tv_usec;
}

HLSStream::HLSStream(const int mid, const uint64_t bitrate, QString &uri)
{
    m_id            = mid;
    m_bitrate       = bitrate;
    m_targetduration= -1;   // not known yet
    m_size          = 0LL;
    m_duration      = 0LL;
    m_live          = true;
    m_startsequence = 0;    // default is 0
    m_version       = 1;    // default protocol version
    m_cache         = true;
    m_url           = uri;
#ifdef USING_LIBCRYPTO
    m_ivloaded      = false;
#endif
}

HLSStream::HLSStream(HLSStream &rhs, bool copy)
{
    (*this) = rhs;
    if (!copy)
        return;
    // copy all the segments across
    QList<HLSSegment*>::iterator it = m_segments.begin();
    for (; it != m_segments.end(); ++it)
    {
        const HLSSegment *old = *it;
        HLSSegment *segment = new HLSSegment(*old);
        AppendSegment(segment);
    }
}

HLSStream::~HLSStream()
{
    QList<HLSSegment*>::iterator it = m_segments.begin();
    for (; it != m_segments.end(); ++it)
    {
        delete *it;
    }
}

HLSStream &HLSStream::operator=(const HLSStream &rhs)
{
    if (this == &rhs)
        return *this;
    // do not copy segments
    m_id            = rhs.m_id;
    m_version       = rhs.m_version;
    m_startsequence = rhs.m_startsequence;
    m_targetduration= rhs.m_targetduration;
    m_bitrate       = rhs.m_bitrate;
    m_size          = rhs.m_size;
    m_duration      = rhs.m_duration;
    m_live          = rhs.m_live;
    m_url           = rhs.m_url;
    m_cache         = rhs.m_cache;
#ifdef USING_LIBCRYPTO
    m_keypath       = rhs.m_keypath;
    m_ivloaded      = rhs.m_ivloaded;
#endif
    return *this;
}

/**
 * Return the estimated size of the stream in bytes
 * if a segment hasn't been downloaded, its size is estimated according
 * to the bandwidth of the stream
 */
uint64_t HLSStream::Size(bool force)
{
    if (m_size > 0 && !force)
        return m_size;
    QMutexLocker lock(&m_lock);

    int64_t size = 0;
    int count = NumSegments();
    for (int i = 0; i < count; i++)
    {
        HLSSegment *segment    = GetSegment(i);
        segment->Lock();
        if (segment->Size() > 0)
        {
            size += (int64_t)segment->Size();
        }
        else
        {
            size += segment->Duration() * Bitrate() / 8;
        }
        segment->Unlock();
    }
    m_size = size;
    return m_size;
}

HLSSegment *HLSStream::GetSegment(const int wanted)
{
    int count = NumSegments();
    if (count <= 0)
        return NULL;
    if ((wanted < 0) || (wanted >= count))
        return NULL;
    return m_segments[wanted];
}

HLSSegment *HLSStream::FindSegment(const int id, int *segnum)
{
    int count = NumSegments();
    if (count <= 0)
        return NULL;
    for (int n = 0; n < count; n++)
    {
        HLSSegment *segment = GetSegment(n);
        if (segment == NULL)
            break;
        if (segment->Id() == id)
        {
            if (segnum != NULL)
            {
                *segnum = n;
            }
            return segment;
        }
    }
    return NULL;
}

void HLSStream::AddSegment(const int duration, QString &title, QString &uri)
{
    QMutexLocker lock(&m_lock);
    QString psz_uri = relative_URI(m_url, uri);
    int id = NumSegments() + m_startsequence;
#ifndef USING_LIBCRYPTO
    QString m_keypath;
#endif
    HLSSegment *segment = new HLSSegment(duration, id, title, psz_uri,
                                         m_keypath);
    AppendSegment(segment);
    m_duration += duration;
}

void HLSStream::RemoveSegment(HLSSegment *segment, bool willdelete)
{
    QMutexLocker lock(&m_lock);
    m_duration -= segment->Duration();
    if (willdelete)
    {
        delete segment;
    }
    int count = NumSegments();
    if (count <= 0)
        return;
    for (int n = 0; n < count; n++)
    {
        HLSSegment *old = GetSegment(n);
        if (old == segment)
        {
            m_segments.removeAt(n);
            break;
        }
    }
    return;
}

void HLSStream::RemoveSegment(int segnum, bool willdelete)
{
    QMutexLocker lock(&m_lock);
    HLSSegment *segment = GetSegment(segnum);
    m_duration -= segment->Duration();
    if (willdelete)
    {
        delete segment;
    }
    m_segments.removeAt(segnum);
    return;
}

void HLSStream::RemoveListSegments(QMap<HLSSegment*,bool> &table)
{
    QMap<HLSSegment*,bool>::iterator it;
    for (it = table.begin(); it != table.end(); ++it)
    {
        bool todelete   = *it;
        HLSSegment *p  = it.key();
        RemoveSegment(p, todelete);
    }
}

int HLSStream::DownloadSegmentData(int segnum, uint64_t &bandwidth, int stream)
{
    HLSSegment *segment = GetSegment(segnum);
    if (segment == NULL)
        return RET_ERROR;

    segment->Lock();
    if (!segment->IsEmpty())
    {
        /* Segment already downloaded */
        segment->Unlock();
        return RET_OK;
    }

    LOG(VB_PLAYBACK, LOG_DEBUG, LOC +
        QString("started download of segment %1 [%2/%3] using stream %4")
        .arg(segnum).arg(segment->Id()).arg(NumSegments()+m_startsequence)
        .arg(stream));

    /* sanity check - can we download this segment on time? */
    if ((bandwidth > 0) && (m_bitrate > 0))
    {
        uint64_t size = (segment->Duration() * m_bitrate); /* bits */
        int estimated = (int)(size / bandwidth);
        if (estimated > segment->Duration())
        {
            LOG(VB_PLAYBACK, LOG_INFO, LOC +
                QString("downloading of segment %1 [id:%2] will take %3s, "
                        "which is longer than its playback (%4s) at %5bit/s")
                .arg(segnum)
                .arg(segment->Id())
                .arg(estimated)
                .arg(segment->Duration())
                .arg(bandwidth));
        }
    }

    uint64_t start = mdate();
    if (segment->Download() != RET_OK)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            QString("downloaded segment %1 [id:%2] from stream %3 failed")
            .arg(segnum).arg(segment->Id()).arg(m_id));
        segment->Unlock();
        return RET_ERROR;
    }

    uint64_t downloadduration = mdate() - start;
    if (m_bitrate == 0 && segment->Duration() > 0)
    {
        /* Try to estimate the bandwidth for this stream */
        m_bitrate = (uint64_t)(((double)segment->Size() * 8) /
                               ((double)segment->Duration()));
    }

#ifdef USING_LIBCRYPTO
    /* If the segment is encrypted, decode it */
    if (segment->HasKeyPath())
    {
        /* Do we have loaded the key ? */
        if (!segment->KeyLoaded())
        {
            if (ManageSegmentKeys() != RET_OK)
            {
                LOG(VB_PLAYBACK, LOG_ERR, LOC +
                    "couldn't retrieve segment AES-128 key");
                segment->Unlock();
                return RET_OK;
            }
        }
        if (segment->DecodeData(m_ivloaded ? m_AESIV : NULL) != RET_OK)
        {
            segment->Unlock();
            return RET_ERROR;
        }
    }
#endif
    segment->Unlock();

    downloadduration = downloadduration < 1 ? 1 : downloadduration;
    bandwidth = segment->Size() * 8 * 1000000ULL / downloadduration; /* bits / s */
    LOG(VB_PLAYBACK, LOG_DEBUG, LOC +
        QString("downloaded segment %1 [id:%2] took %3ms for %4 bytes: bandwidth:%5kiB/s")
        .arg(segnum)
        .arg(segment->Id())
        .arg(downloadduration / 1000)
        .arg(segment->Size())
        .arg(bandwidth / 8192.0));

    return RET_OK;
}

void HLSStream::UpdateWith(HLSStream &upd)
{
    QMutexLocker lock(&m_lock);
    m_targetduration    = upd.m_targetduration < 0 ?
        m_targetduration : upd.m_targetduration;
    m_cache             = upd.m_cache;
}

#ifdef USING_LIBCRYPTO
/**
 * Will download all required segment AES-128 keys
 * Will try to re-use already downloaded keys if possible
 */
int HLSStream::ManageSegmentKeys()
{
    HLSSegment   *seg       = NULL;
    HLSSegment   *prev_seg  = NULL;
    int          count      = NumSegments();

    for (int i = 0; i < count; i++)
    {
        prev_seg = seg;
        seg = GetSegment(i);
        if (seg == NULL )
            continue;
        if (!seg->HasKeyPath())
            continue;   /* No key to load ? continue */
        if (seg->KeyLoaded())
            continue;   /* The key is already loaded */

        /* if the key has not changed, and already available from previous segment,
         * try to copy it, and don't load the key */
        if (prev_seg && prev_seg->KeyLoaded() &&
            (seg->KeyPath() == prev_seg->KeyPath()))
        {
            seg->CopyAESKey(*prev_seg);
            continue;
        }
        if (seg->DownloadKey() != RET_OK)
            return RET_ERROR;
    }
    return RET_OK;
}
#endif // USING_LIBCRYPTO

#ifdef USING_LIBCRYPTO
bool HLSStream::SetAESIV(QString &line)
{
    /*
     * If the EXT-X-KEY tag has the IV attribute, implementations MUST use
     * the attribute value as the IV when encrypting or decrypting with that
     * key.  The value MUST be interpreted as a 128-bit hexadecimal number
     * and MUST be prefixed with 0x or 0X.
     */
    if (!line.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        return false;
    if (line.size() % 2)
    {
        // not even size, pad with front 0
        line.insert(2, QLatin1String("0"));
    }
    int padding = max(0, AES_BLOCK_SIZE - (line.size() - 2));
    QByteArray ba = QByteArray(padding, 0x0);
    ba.append(QByteArray::fromHex(QByteArray(line.toAscii().constData() + 2)));
    memcpy(m_AESIV, ba.constData(), ba.size());
    m_ivloaded = true;
    return true;
}
#endif // USING_LIBCRYPTO

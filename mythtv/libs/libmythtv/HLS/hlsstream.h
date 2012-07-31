/*****************************************************************************
 * hlsstream.h
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

#ifndef MythXCode_hlsstream_h
#define MythXCode_hlsstream_h

#include <stdint.h>

#include <QByteArray>
#include <QString>
#include <QMutex>
#include <QMap>

/* stream container */

class HLSStream
{
public:
    HLSStream(const int mid, const uint64_t bitrate, QString &uri);

    HLSStream(HLSStream &rhs, bool copy = true);

    ~HLSStream();

    HLSStream &operator=(const HLSStream &rhs);

    static bool IsGreater(const HLSStream *s1, const HLSStream *s2)
    {
        return s1->Bitrate() > s2->Bitrate();
    }

    bool operator<(HLSStream &b)
    {
        return this->Bitrate() < b.Bitrate();
    }

    bool operator>(HLSStream &b)
    {
        return this->Bitrate() > b.Bitrate();
    }

    /**
     * Return the estimated size of the stream in bytes
     * if a segment hasn't been downloaded, its size is estimated according
     * to the bandwidth of the stream
     */
    uint64_t Size(bool force = false);

    int64_t Duration(void)
    {
        QMutexLocker lock(&m_lock);
        return m_duration;
    }

    void Clear(void)
    {
        m_segments.clear();
    }

    int NumSegments(void)
    {
        return m_segments.size();
    }

    void AppendSegment(HLSSegment *segment)
    {
        // must own lock
        m_segments.append(segment);
    }

    HLSSegment *GetSegment(const int wanted);

    HLSSegment *FindSegment(const int id, int *segnum = NULL);

    void AddSegment(const int duration, QString &title, QString &uri);

    void RemoveSegment(HLSSegment *segment, bool willdelete = true);

    void RemoveSegment(int segnum, bool willdelete = true);

    void RemoveListSegments(QMap<HLSSegment*,bool> &table);

    int DownloadSegmentData(int segnum, uint64_t &bandwidth, int stream);

    int Id(void) const
    {
        return m_id;
    }
    int Version(void)
    {
        return m_version;
    }
    void SetVersion(int x)
    {
        m_version = x;
    }
    int StartSequence(void)
    {
        return m_startsequence;
    }
    void SetStartSequence(int x)
    {
        m_startsequence = x;
    }
    int TargetDuration(void)
    {
        return m_targetduration;
    }
    void SetTargetDuration(int x)
    {
        m_targetduration = x;
    }
    uint64_t Bitrate(void) const
    {
        return m_bitrate;
    }
    bool Cache(void)
    {
        return m_cache;
    }
    void SetCache(bool x)
    {
        m_cache = x;
    }
    bool Live(void)
    {
        return m_live;
    }
    void SetLive(bool x)
    {
        m_live = x;
    }
    void Lock(void)
    {
        m_lock.lock();
    }
    void Unlock(void)
    {
        m_lock.unlock();
    }
    QString Url(void)
    {
        return m_url;
    }
    void UpdateWith(HLSStream &upd);
#ifdef USING_LIBCRYPTO

    int ManageSegmentKeys();

    bool SetAESIV(QString &line);

    uint8_t *AESIV(void)
    {
        return m_AESIV;
    }
    void SetKeyPath(QString &x)
    {
        m_keypath = x;
    }
private:
    QString     m_keypath;              // URL path of the encrypted key
    bool        m_ivloaded;
    uint8_t     m_AESIV[AES_BLOCK_SIZE];// IV used when decypher the block
#endif

private:
    int         m_id;                   // program id
    int         m_version;              // protocol version should be 1
    int         m_startsequence;        // media starting sequence number
    int         m_targetduration;       // maximum duration per segment (s)
    uint64_t    m_bitrate;              // bitrate of stream content (bits per second)
    uint64_t    m_size;                 // stream length is calculated by taking the sum
                                        // foreach segment of (segment->duration * hls->bitrate/8)
    int64_t     m_duration;             // duration of the stream in seconds
    bool        m_live;

    QList<HLSSegment*> m_segments;      // list of segments
    QString     m_url;                  // uri to m3u8
    QMutex      m_lock;
    bool        m_cache;                // allow caching
};

#endif // MythXCode_hlsstream_h

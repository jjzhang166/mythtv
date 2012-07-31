/*****************************************************************************
 * hlssegment.h
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

#ifndef MythXCode_hlssegment_h
#define MythXCode_hlssegment_h

#include <stdint.h>

#include <QByteArray>
#include <QString>
#include <QMutex>

#ifdef USING_LIBCRYPTO
// encryption related stuff
#include <openssl/aes.h>
#define AES_BLOCK_SIZE 16       // HLS only support AES-128
#endif

/* segment container */

enum
{
    RET_ERROR = -1,
    RET_OK    = 0,
};

class HLSSegment
{
public:
    HLSSegment(const int mduration, const int id, QString &title,
               QString &uri, QString &current_key_path);

    HLSSegment(const HLSSegment &rhs)
    {
        *this = rhs;
    }

    ~HLSSegment()
    {
    }

    HLSSegment &operator=(const HLSSegment &rhs);

    int Duration(void)
    {
        return m_duration;
    }

    int Id(void)
    {
        return m_id;
    }

    void Lock(void)
    {
        m_lock.lock();
    }

    void Unlock(void)
    {
        m_lock.unlock();
    }

    bool IsEmpty(void)
    {
        return m_data.isEmpty();
    }

    int32_t Size(void)
    {
        return m_data.size();
    }

    int Download(void);

    QString Url(void)
    {
        return m_url;
    }

    int32_t SizePlayed(void)
    {
        return m_played;
    }

    uint32_t Read(uint8_t *buffer, int32_t length, FILE *fd = NULL);

    void Reset(void)
    {
        m_played = 0;
    }

    void Clear(void)
    {
        m_played = 0;
        m_data.clear();
    }

    QString Title(void)
    {
        return m_title;
    }
    void SetTitle(QString &x)
    {
        m_title = x;
    }
    /**
     * provides pointer to raw segment data
     */
    const char *Data(void)
    {
        return m_data.constData();
    }

#ifdef USING_LIBCRYPTO
    int DownloadKey(void);
    int DecodeData(uint8_t *IV);

    bool HasKeyPath(void)
    {
        return !m_psz_key_path.isEmpty();
    }

    bool KeyLoaded(void)
    {
        return m_keyloaded;
    }

    QString KeyPath(void)
    {
        return m_psz_key_path;
    }

    void SetKeyPath(QString &path)
    {
        m_psz_key_path = path;
    }

    void CopyAESKey(HLSSegment &segment);

private:
    AES_KEY     m_aeskey;       ///< AES-128 key
    bool        m_keyloaded;
    QString     m_psz_key_path; ///< URL key path
#endif // USING_LIBCRYPTO

private:
    int         m_id;           ///< unique sequence number
    int         m_duration;     ///< segment duration (seconds)
    uint64_t    m_bitrate;      ///< bitrate of segment's content (bits per second)
    QString     m_title;        ///< human-readable informative title of the media segment

    QString     m_url;
    QByteArray  m_data;         ///< raw data
    int32_t     m_played;       ///< bytes counter of data already read from segment
    QMutex      m_lock;
};

#endif // MythXCode_hlssegment_h

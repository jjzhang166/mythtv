/*****************************************************************************
 * hlssegment.cpp
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

#include <cstring> // for memcpy

#include "mythdownloadmanager.h"
#include "mythlogging.h"
#include "hlssegment.h"

#define LOC QString("HLSBuffer: ")

static bool downloadURL(QString url, QByteArray *buffer)
{
    MythDownloadManager *mdm = GetMythDownloadManager();
    return mdm->download(url, buffer);
}

HLSSegment::HLSSegment(const int mduration, const int id, QString &title,
                       QString &uri, QString &current_key_path)
{
    m_duration      = mduration; /* seconds */
    m_id            = id;
    m_bitrate       = 0;
    m_url           = uri;
    m_played        = 0;
    m_title         = title;
#ifdef USING_LIBCRYPTO
    m_keyloaded     = false;
    m_psz_key_path  = current_key_path;
#endif
}

HLSSegment &HLSSegment::operator=(const HLSSegment &rhs)
{
    if (this == &rhs)
        return *this;
    m_id            = rhs.m_id;
    m_duration      = rhs.m_duration;
    m_bitrate       = rhs.m_bitrate;
    m_url           = rhs.m_url;
    // keep the old data downloaded
    m_data          = m_data;
    m_played        = m_played;
    m_title         = rhs.m_title;
#ifdef USING_LIBCRYPTO
    m_psz_key_path  = rhs.m_psz_key_path;
    memcpy(&m_aeskey, &(rhs.m_aeskey), sizeof(m_aeskey));
    m_keyloaded     = rhs.m_keyloaded;
#endif
    return *this;
}

int HLSSegment::Download(void)
{
    // must own lock
    bool ret = downloadURL(m_url, &m_data);
    // didn't succeed, clear buffer
    if (!ret)
    {
        m_data.clear();
        return RET_ERROR;
    }
    return RET_OK;
}

uint32_t HLSSegment::Read(uint8_t *buffer, int32_t length, FILE *fd)
{
    int32_t left = m_data.size() - m_played;
    if (length > left)
    {
        length = left;
    }
    if (buffer != NULL)
    {
        memcpy(buffer, m_data.constData() + m_played, length);
        // write data to disk if required
        if (fd)
        {
            fwrite(m_data.constData() + m_played, length, 1, fd);
        }
    }
    m_played += length;
    return length;
}

#ifdef USING_LIBCRYPTO
int HLSSegment::DownloadKey(void)
{
    // must own lock
    if (m_keyloaded)
        return RET_OK;
    QByteArray key;
    bool ret = downloadURL(m_psz_key_path, &key);
    if (ret != RET_OK || key.size() != AES_BLOCK_SIZE)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            QString("The AES key loaded doesn't have the right size (%1)")
            .arg(key.size()));
        return RET_ERROR;
    }
    AES_set_decrypt_key((const unsigned char*)key.constData(), 128, &m_aeskey);
    m_keyloaded = true;
    return RET_OK;
}
#endif // USING_LIBCRYPTO

#ifdef USING_LIBCRYPTO
int HLSSegment::DecodeData(uint8_t *IV)
{
    /* Decrypt data using AES-128 */
    int aeslen = m_data.size() & ~0xf;
    unsigned char iv[AES_BLOCK_SIZE];
    char *decrypted_data = new char[aeslen];
    if (IV == NULL)
    {
        /*
         * If the EXT-X-KEY tag does not have the IV attribute, implementations
         * MUST use the sequence number of the media file as the IV when
         * encrypting or decrypting that media file.  The big-endian binary
         * representation of the sequence number SHALL be placed in a 16-octet
         * buffer and padded (on the left) with zeros.
         */
        memset(iv, 0, AES_BLOCK_SIZE);
        iv[15] = m_id         & 0xff;
        iv[14] = (m_id >> 8)  & 0xff;
        iv[13] = (m_id >> 16) & 0xff;
        iv[12] = (m_id >> 24) & 0xff;
    }
    else
    {
        memcpy(iv, IV, sizeof(iv));
    }
    AES_cbc_encrypt((unsigned char*)m_data.constData(),
                    (unsigned char*)decrypted_data, aeslen,
                    &m_aeskey, iv, AES_DECRYPT);
    memcpy(decrypted_data + aeslen, m_data.constData() + aeslen,
           m_data.size() - aeslen);

    // remove the PKCS#7 padding from the buffer
    int pad = m_data[m_data.size()-1];
    if (pad <= 0 || pad > AES_BLOCK_SIZE)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            QString("bad padding character (0x%1)").arg(pad, 0, 16, QLatin1Char('0')));
        return RET_ERROR;
    }
    aeslen = m_data.size() - pad;
    m_data = QByteArray(decrypted_data, aeslen);
    delete[] decrypted_data;

    return RET_OK;
}
#endif // USING_LIBCRYPTO

#ifdef USING_LIBCRYPTO
void HLSSegment::CopyAESKey(HLSSegment &segment)
{
    memcpy(&m_aeskey, &(segment.m_aeskey), sizeof(m_aeskey));
    m_keyloaded = segment.m_keyloaded;
}
#endif // USING_LIBCRYPTO

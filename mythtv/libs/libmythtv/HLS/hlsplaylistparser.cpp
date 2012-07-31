/*****************************************************************************
 * hlsplaylistparser.cpp
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

#include "mythdownloadmanager.h"
#include "hlsplaylistparser.h"
#include "mythlogging.h"

#include <QStringList>
#include <QTextStream>
#include <QByteArray>
#include <QUrl>

#define LOC QString("HLSPlaylistParser: ")

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

static bool downloadURL(QString url, QByteArray *buffer)
{
    MythDownloadManager *mdm = GetMythDownloadManager();
    return mdm->download(url, buffer);
}

bool HLSPlaylistParser::IsHTTPLiveStreaming(QByteArray *s)
{
    if (!s || s->size() < 7)
        return false;

    if (!s->startsWith((const char*)"#EXTM3U"))
        return false;

    QTextStream stream(s);
    /* Parse stream and search for
     * EXT-X-TARGETDURATION or EXT-X-STREAM-INF tag, see
     * http://tools.ietf.org/html/draft-pantos-http-live-streaming-04#page-8 */
    while (1)
    {
        QString line = stream.readLine();
        if (line.isNull())
            break;
        LOG(VB_PLAYBACK, LOG_DEBUG, LOC +
            QString("IsHTTPLiveStreaming: %1").arg(line));
        if (line.startsWith(QLatin1String("#EXT-X-TARGETDURATION"))  ||
            line.startsWith(QLatin1String("#EXT-X-MEDIA-SEQUENCE"))  ||
            line.startsWith(QLatin1String("#EXT-X-KEY"))             ||
            line.startsWith(QLatin1String("#EXT-X-ALLOW-CACHE"))     ||
            line.startsWith(QLatin1String("#EXT-X-ENDLIST"))         ||
            line.startsWith(QLatin1String("#EXT-X-STREAM-INF"))      ||
            line.startsWith(QLatin1String("#EXT-X-DISCONTINUITY"))   ||
            line.startsWith(QLatin1String("#EXT-X-VERSION")))
        {
            return true;
        }
    }
    return false;
}

/* Parsing */
QString HLSPlaylistParser::ParseAttributes(QString &line, const char *attr)
{
    int p = line.indexOf(QLatin1String(":"));
    if (p < 0)
        return QString();

    QStringList list = QStringList(line.mid(p+1).split(','));
    QStringList::iterator it = list.begin();
    for (; it != list.end(); ++it)
    {
        QString arg = (*it).trimmed();
        if (arg.startsWith(attr))
        {
            int pos = arg.indexOf(QLatin1String("="));
            if (pos < 0)
                continue;
            return arg.mid(pos+1);
        }
    }
    return QString();
}

/**
 * Return the decimal argument in a line of type: blah:<decimal>
 * presence of valud <decimal> is compulsory or it will return RET_ERROR
 */
int HLSPlaylistParser::ParseDecimalValue(QString &line, int &target)
{
    int p = line.indexOf(QLatin1String(":"));
    if (p < 0)
        return RET_ERROR;
    int i = p;
    while (++i < line.size() && line[i].isNumber());
    if (i == p + 1)
        return RET_ERROR;
    target = line.mid(p+1, i - p - 1).toInt();
    return RET_OK;
}

int HLSPlaylistParser::ParseSegmentInformation(HLSStream *hls, QString &line,
                                           int &duration, QString &title)
{
    /*
     * #EXTINF:<duration>,<title>
     *
     * "duration" is an integer that specifies the duration of the media
     * file in seconds.  Durations SHOULD be rounded to the nearest integer.
     * The remainder of the line following the comma is the title of the
     * media file, which is an optional human-readable informative title of
     * the media segment
     */
    int p = line.indexOf(QLatin1String(":"));
    if (p < 0)
        return RET_ERROR;

    QStringList list = QStringList(line.mid(p+1).split(','));

    /* read duration */
    if (list.isEmpty())
    {
        return RET_ERROR;
    }
    QString val = list[0];
    bool ok;

    if (hls->Version() < 3)
    {
        duration = val.toInt(&ok);
        if (!ok)
        {
            duration = -1;
            return RET_ERROR;
        }
    }
    else
    {
        double d = val.toDouble(&ok);
        if (!ok)
        {
            duration = -1;
            return RET_ERROR;
        }
        if ((d) - ((int)d) >= 0.5)
            duration = ((int)d) + 1;
        else
            duration = ((int)d);
    }

    if (list.size() >= 2)
    {
        title = list[1];
    }

    /* Ignore the rest of the line */
    return RET_OK;
}

int HLSPlaylistParser::ParseTargetDuration(HLSStream *hls, QString &line)
{
    /*
     * #EXT-X-TARGETDURATION:<s>
     *
     * where s is an integer indicating the target duration in seconds.
     */
    int duration       = -1;

    if (ParseDecimalValue(line, duration) != RET_OK)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "expected #EXT-X-TARGETDURATION:<s>");
        return RET_ERROR;
    }
    hls->SetTargetDuration(duration); /* seconds */
    return RET_OK;
}

HLSStream *HLSPlaylistParser::ParseStreamInformation(QString &line, QString &uri)
{
    /*
     * #EXT-X-STREAM-INF:[attribute=value][,attribute=value]*
     *  <URI>
     */
    int id;
    uint64_t bw;
    QString attr;

    attr = ParseAttributes(line, "PROGRAM-ID");
    if (attr.isNull())
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "#EXT-X-STREAM-INF: expected PROGRAM-ID=<value>");
        return NULL;
    }
    id = attr.toInt();

    attr = ParseAttributes(line, "BANDWIDTH");
    if (attr.isNull())
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "#EXT-X-STREAM-INF: expected BANDWIDTH=<value>");
        return NULL;
    }
    bw = attr.toInt();

    if (bw == 0)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "#EXT-X-STREAM-INF: bandwidth cannot be 0");
        return NULL;
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("bandwidth adaptation detected (program-id=%1, bandwidth=%2")
        .arg(id).arg(bw));

    QString psz_uri = relative_URI(m_m3u8, uri);

    return new HLSStream(id, bw, psz_uri);
}

int HLSPlaylistParser::ParseMediaSequence(HLSStream *hls, QString &line)
{
    /*
     * #EXT-X-MEDIA-SEQUENCE:<number>
     *
     * A Playlist file MUST NOT contain more than one EXT-X-MEDIA-SEQUENCE
     * tag.  If the Playlist file does not contain an EXT-X-MEDIA-SEQUENCE
     * tag then the sequence number of the first URI in the playlist SHALL
     * be considered to be 0.
     */
    int sequence;

    if (ParseDecimalValue(line, sequence) != RET_OK)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "expected #EXT-X-MEDIA-SEQUENCE:<s>");
        return RET_ERROR;
    }

    if (hls->StartSequence() > 0 && !hls->Live())
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            QString("EXT-X-MEDIA-SEQUENCE already present in playlist (new=%1, old=%2)")
            .arg(sequence).arg(hls->StartSequence()));
    }
    hls->SetStartSequence(sequence);
    return RET_OK;
}


int HLSPlaylistParser::ParseKey(HLSStream *hls, QString &line)
{
    /*
     * #EXT-X-KEY:METHOD=<method>[,URI="<URI>"][,IV=<IV>]
     *
     * The METHOD attribute specifies the encryption method.  Two encryption
     * methods are defined: NONE and AES-128.
     */
    int err = 0;
    QString attr = ParseAttributes(line, "METHOD");
    if (attr.isNull())
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "#EXT-X-KEY: expected METHOD=<value>");
        return RET_ERROR;
    }

    if (attr.startsWith(QLatin1String("NONE")))
    {
        QString uri = ParseAttributes(line, "URI");
        if (!uri.isNull())
        {
            LOG(VB_PLAYBACK, LOG_ERR, LOC + "#EXT-X-KEY: URI not expected");
            err = RET_ERROR;
        }
        /* IV is only supported in version 2 and above */
        if (hls->Version() >= 2)
        {
            QString iv = ParseAttributes(line, "IV");
            if (!iv.isNull())
            {
                LOG(VB_PLAYBACK, LOG_ERR, LOC + "#EXT-X-KEY: IV not expected");
                err = RET_ERROR;
            }
        }
    }
#ifdef USING_LIBCRYPTO
    else if (attr.startsWith(QLatin1String("AES-128")))
    {
        QString uri, iv;
        if (m_aesmsg == false)
        {
            LOG(VB_PLAYBACK, LOG_INFO, LOC +
                "playback of AES-128 encrypted HTTP Live media detected.");
            m_aesmsg = true;
        }
        uri = ParseAttributes(line, "URI");
        if (uri.isNull())
        {
            LOG(VB_PLAYBACK, LOG_ERR, LOC +
                "#EXT-X-KEY: URI not found for encrypted HTTP Live media in AES-128");
            return RET_ERROR;
        }

        /* Url is between quotes, remove them */
        hls->SetKeyPath(uri.remove(QChar(QLatin1Char('"'))));

        iv = ParseAttributes(line, "IV");
        if (!hls->SetAESIV(iv))
        {
            LOG(VB_PLAYBACK, LOG_ERR, LOC + "invalid IV");
            err = RET_ERROR;
        }
    }
#endif
    else
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            "invalid encryption type, only NONE "
#ifdef USING_LIBCRYPTO
            "and AES-128 are supported"
#else
            "is supported."
#endif
            );
        err = RET_ERROR;
    }
    return err;
}

int HLSPlaylistParser::ParseProgramDateTime(HLSStream *hls, QString &line)
{
    /*
     * #EXT-X-PROGRAM-DATE-TIME:<YYYY-MM-DDThh:mm:ssZ>
     */
    LOG(VB_PLAYBACK, LOG_WARNING, LOC +
        QString("tag not supported: #EXT-X-PROGRAM-DATE-TIME %1")
        .arg(line));
    return RET_OK;
}

int HLSPlaylistParser::ParseAllowCache(HLSStream *hls, QString &line)
{
    /*
     * The EXT-X-ALLOW-CACHE tag indicates whether the client MAY or MUST
     * NOT cache downloaded media files for later replay.  It MAY occur
     * anywhere in the Playlist file; it MUST NOT occur more than once.  The
     * EXT-X-ALLOW-CACHE tag applies to all segments in the playlist.  Its
     * format is:
     *
     * #EXT-X-ALLOW-CACHE:<YES|NO>
     */
    int pos = line.indexOf(QLatin1String(":"));
    if (pos < 0)
        return RET_ERROR;
    QString answer = line.mid(pos+1, 3);
    if (answer.size() < 2)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "#EXT-X-ALLOW-CACHE, ignoring ...");
        return RET_ERROR;
    }
    hls->SetCache(!answer.startsWith(QLatin1String("NO")));
    return RET_OK;
}

int HLSPlaylistParser::ParseVersion(QString &line, int &version)
{
    /*
     * The EXT-X-VERSION tag indicates the compatibility version of the
     * Playlist file.  The Playlist file, its associated media, and its
     * server MUST comply with all provisions of the most-recent version of
     * this document describing the protocol version indicated by the tag
     * value.
     *
     * Its format is:
     *
     * #EXT-X-VERSION:<n>
     */

    if (ParseDecimalValue(line, version) != RET_OK)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            "#EXT-X-VERSION: no protocol version found, should be version 1.");
        return RET_ERROR;
    }

    if (version <= 0 || version > 3)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            QString("#EXT-X-VERSION should be version 1, 2 or 3 iso %1")
            .arg(version));
        return RET_ERROR;
    }
    return RET_OK;
}

int HLSPlaylistParser::ParseEndList(HLSStream *hls)
{
    /*
     * The EXT-X-ENDLIST tag indicates that no more media files will be
     * added to the Playlist file.  It MAY occur anywhere in the Playlist
     * file; it MUST NOT occur more than once.  Its format is:
     */
    hls->SetLive(false);
    LOG(VB_PLAYBACK, LOG_INFO, LOC + "video on demand (vod) mode");
    return RET_OK;
}

int HLSPlaylistParser::ParseDiscontinuity(HLSStream *hls, QString &line)
{
    /* Not handled, never seen so far */
    LOG(VB_PLAYBACK, LOG_DEBUG, LOC + QString("#EXT-X-DISCONTINUITY %1").arg(line));
    return RET_OK;
}

int HLSPlaylistParser::ParseM3U8(const QByteArray *buffer, StreamsList *streams)
{
    /**
     * dThe http://tools.ietf.org/html/draft-pantos-http-live-streaming-04#page-8
     * document defines the following new tags: EXT-X-TARGETDURATION,
     * EXT-X-MEDIA-SEQUENCE, EXT-X-KEY, EXT-X-PROGRAM-DATE-TIME, EXT-X-
     * ALLOW-CACHE, EXT-X-STREAM-INF, EXT-X-ENDLIST, EXT-X-DISCONTINUITY,
     * and EXT-X-VERSION.
     */
    QTextStream stream(*buffer); stream.setCodec("UTF-8");

    QString line = stream.readLine();
    if (line.isNull())
        return RET_ERROR;

    if (!line.startsWith(QLatin1String("#EXTM3U")))
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC + "missing #EXTM3U tag .. aborting");
        return RET_ERROR;
    }

    /* What is the version ? */
    int version = 1;
    int p = buffer->indexOf("#EXT-X-VERSION:");
    if (p >= 0)
    {
        stream.seek(p);
        QString psz_version = stream.readLine();
        if (psz_version.isNull())
            return RET_ERROR;
        int ret = ParseVersion(psz_version, version);
        if (ret != RET_OK)
        {
            LOG(VB_GENERAL, LOG_WARNING, LOC +
                "#EXT-X-VERSION: no protocol version found, assuming version 1.");
            version = 1;
        }
    }

    /* Is it a meta index file ? */
    bool meta = buffer->indexOf("#EXT-X-STREAM-INF") < 0 ? false : true;

    int err = RET_OK;

    if (meta)
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "Meta playlist");

        /* M3U8 Meta Index file */
        stream.seek(0); // rewind
        while (true)
        {
            line = stream.readLine();
            if (line.isNull())
                break;

            if (line.startsWith(QLatin1String("#EXT-X-STREAM-INF")))
            {
                m_meta = true;
                QString uri = stream.readLine();
                if (uri.isNull())
                {
                    err = RET_ERROR;
                    break;
                }
                if (uri.startsWith(QLatin1String("#")))
                {
                    LOG(VB_GENERAL, LOG_INFO, LOC +
                        QString("Skipping invalid stream-inf: %1")
                        .arg(uri));
                }
                else
                {
                    HLSStream *hls = ParseStreamInformation(line, uri);
                    if (hls)
                    {
                        streams->append(hls);
                        /* Download playlist file from server */
                        QByteArray buf;
                        bool ret = downloadURL(hls->Url(), &buf);
                        if (!ret)
                        {
                            err = RET_ERROR;
                            break;
                        }
                        /* Parse HLS m3u8 content. */
                        err = ParseM3U8(&buf, streams);
                        if (err != RET_OK)
                            break;
                        hls->SetVersion(version);
                    }
                }
            }
        }
    }
    else
    {
        HLSStream *hls = NULL;
        if (m_meta)
            hls = streams->GetLastStream();
        else
        {
            /* No Meta playlist used */
            hls = new HLSStream(0, 0, m_m3u8);
            streams->append(hls);
            /* Get TARGET-DURATION first */
            p = buffer->indexOf("#EXT-X-TARGETDURATION:");
            if (p >= 0)
            {
                stream.seek(p);
                QString psz_duration = stream.readLine();
                if (psz_duration.isNull())
                    return RET_ERROR;
                err = ParseTargetDuration(hls, psz_duration);
            }
            /* Store version */
            hls->SetVersion(version);
        }
        LOG(VB_PLAYBACK, LOG_INFO, LOC +
            QString("%1 Playlist HLS protocol version: %2")
            .arg(hls->Live() ? "Live": "VOD").arg(version));

        // rewind
        stream.seek(0);
        /* */
        int segment_duration = -1;
        QString title;
        do
        {
            /* Next line */
            line = stream.readLine();
            if (line.isNull())
                break;
            LOG(VB_PLAYBACK, LOG_DEBUG, LOC + QString("ParseM3U8: %1")
                .arg(line));

            if (line.startsWith(QLatin1String("#EXTINF")))
                err = ParseSegmentInformation(hls, line, segment_duration, title);
            else if (line.startsWith(QLatin1String("#EXT-X-TARGETDURATION")))
                err = ParseTargetDuration(hls, line);
            else if (line.startsWith(QLatin1String("#EXT-X-MEDIA-SEQUENCE")))
                err = ParseMediaSequence(hls, line);
            else if (line.startsWith(QLatin1String("#EXT-X-KEY")))
                err = ParseKey(hls, line);
            else if (line.startsWith(QLatin1String("#EXT-X-PROGRAM-DATE-TIME")))
                err = ParseProgramDateTime(hls, line);
            else if (line.startsWith(QLatin1String("#EXT-X-ALLOW-CACHE")))
                err = ParseAllowCache(hls, line);
            else if (line.startsWith(QLatin1String("#EXT-X-DISCONTINUITY")))
                err = ParseDiscontinuity(hls, line);
            else if (line.startsWith(QLatin1String("#EXT-X-VERSION")))
            {
                int version;
                err = ParseVersion(line, version);
                hls->SetVersion(version);
            }
            else if (line.startsWith(QLatin1String("#EXT-X-ENDLIST")))
                err = ParseEndList(hls);
            else if (!line.startsWith(QLatin1String("#")) && !line.isEmpty())
            {
                hls->AddSegment(segment_duration, title, line);
                segment_duration = -1; /* reset duration */
                title = "";
            }
        }
        while (err == RET_OK);
    }
    return err;
}

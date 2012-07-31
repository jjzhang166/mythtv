/*****************************************************************************
 * hlsplaylistparser.h
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

#ifndef MythXCode_hlsplaylistparser_h
#define MythXCode_hlsplaylistparser_h

#include "hlssegment.h"
#include "hlsstream.h"

#include <QString>

class HLSPlaylistParser
{
  public:
    HLSPlaylistParser(QString &m3u8, bool &meta, bool &aesmsg) :
        m_m3u8(m3u8), m_meta(meta), m_aesmsg(aesmsg) {}

    int ParseM3U8(const QByteArray *buffer, StreamsList *streams);

    static bool IsHTTPLiveStreaming(QByteArray *s);

  private:
    int ParseKey(HLSStream *hls, QString &line);
    HLSStream *ParseStreamInformation(QString &line, QString &uri);

    static QString ParseAttributes(
        QString &line, const char *attr);
    static int ParseDecimalValue(QString &line, int &target);
    static int ParseSegmentInformation(
        HLSStream *hls, QString &line,
        int &duration, QString &title);
    static int ParseTargetDuration(HLSStream *hls, QString &line);
    static int ParseMediaSequence(HLSStream *hls, QString &line);
    static int ParseProgramDateTime(
        HLSStream *hls, QString &line);
    static int ParseAllowCache(HLSStream *hls, QString &line);
    static int ParseVersion(QString &line, int &version);
    static int ParseEndList(HLSStream *hls);
    static int ParseDiscontinuity(HLSStream *hls, QString &line);

  private:
    QString &m_m3u8;
    bool    &m_meta;
    bool    &m_aesmsg;
};

#endif // MythXCode_hlsplaylistparser_h

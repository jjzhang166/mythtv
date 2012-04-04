/** -*- Mode: c++ -*-
 *  IptvRTSP
 *  Distributed as part of MythTV under GPL v2 and later.
 */

#include <QStringList>
#include <QTcpSocket>
#include <QUrl>

// MythTV includes
#include "iptvrtsp.h"
#include "mythlogging.h"


#define LOC QString("IptvRTSP(%1): ").arg(_ip)

QMutex IptvRTSP::_rtspMutex;

IptvRTSP::IptvRTSP(const QUrl &url) :
    _ip(url.host()),
    _port((url.port() >= 0) ? url.port() : 554),
    _sequenceNumber(0),
    _responseCode(-1)
{
    _requestUrl = url.toString();
}


bool IptvRTSP::ProcessRequest(
    const QString &method, const QString &controlUrl,
    const QStringList *headers)
{
    QMutexLocker locker(&_rtspMutex);
    QTcpSocket socket;
    socket.connectToHost(_ip, _port);

    QStringList requestHeaders;
    QString url;
    if(controlUrl.isEmpty())
        url.append(_requestUrl);
    else
        url.append(controlUrl);

    requestHeaders.append(QString("%1 %2 RTSP/1.0").arg(method, url));
    requestHeaders.append(QString("User-Agent: MythTV IPTV Recorder"));
    requestHeaders.append(QString("CSeq: %1").arg(++_sequenceNumber));

    if (!_sessionID.isEmpty())
        requestHeaders.append(QString("Session: %1").arg(_sessionID));

    if (headers != NULL)
    {
        for(int i = 0; i < headers->count(); i++)
        {
            QString header = headers->at(i);
            requestHeaders.append(header);
        }
    }
    requestHeaders.append(QString("\r\n"));
    QString request = requestHeaders.join("\r\n");


    socket.write(request.toAscii());

    _responseHeaders.clear();

    QRegExp firstLineRegex(
        "^RTSP/1.0 (\\d+) ([^\r\n]+)", Qt::CaseSensitive, QRegExp::RegExp2);
    QRegExp headerRegex(
        "^([^:]+):\\s*([^\\r\\n]+)", Qt::CaseSensitive, QRegExp::RegExp2);
    QRegExp blankLineRegex(
        "^[\\r\\n]*$", Qt::CaseSensitive, QRegExp::RegExp2);

    bool firstLine = true;
    while (true)
    {
        if (!socket.canReadLine())
        {
            bool ready = socket.waitForReadyRead();
            if (!ready)
            {
                LOG(VB_RECORD, LOG_ERR, LOC + "RTSP server did not respond");
                return false;
            }
            continue;
        }

        QString line = socket.readLine();
        LOG(VB_RECORD, LOG_DEBUG, LOC + QString("read: %1").arg(line));

        if (firstLine)
        {
            if (firstLineRegex.indexIn(line) == -1)
            {
                _responseCode = -1;
                _responseMessage =
                    QString("Could not parse first line of response: '%1'")
                    .arg(line);
                return false;
            }

            QStringList parts = firstLineRegex.capturedTexts();
            _responseCode = parts.at(1).toInt();
            _responseMessage = parts.at(2);

            firstLine = false;
            continue;
        }

        if (blankLineRegex.indexIn(line) != -1) break;

        if (headerRegex.indexIn(line) == -1)
        {
            _responseCode = -1;
            _responseMessage = QString("Could not parse response header: '%1'")
                .arg(line);
            return false;
        }
        QStringList parts = headerRegex.capturedTexts();
        _responseHeaders.insert(parts.at(1), parts.at(2));
    }

    QString cSeq = _responseHeaders.value("Cseq");
    if (cSeq != QString("%1").arg(_sequenceNumber))
    {
        LOG(VB_RECORD, LOG_WARNING, LOC +
            QString("Expected CSeq of %1 but got %2")
            .arg(_sequenceNumber).arg(cSeq));
    }

    _responseContent.clear();
    int contentLength = _responseHeaders.value("Content-Length").toInt();
    if (contentLength > 0)
    {
        _responseContent.resize(contentLength);
        char* data = _responseContent.data();
        int bytesRead = 0;
        while (bytesRead < contentLength)
        {
            if (socket.bytesAvailable() == 0)
                socket.waitForReadyRead();

            int count = socket.read(data+bytesRead, contentLength-bytesRead);
            if (count == -1)
            {
                _responseCode = -1;
                _responseMessage = "Could not read response content";
                return false;
            }
            bytesRead += count;
        }
    }
    return true;
}

bool IptvRTSP::GetOptions(QStringList &options)
{
    if (ProcessRequest("OPTIONS"))
    {
        options = _responseHeaders.value("Public").split(",");
        return true;
    }
    return false;
}

bool IptvRTSP::Describe(void)
{
    if (!ProcessRequest("DESCRIBE"))
        return false;

    if (!_responseContent.contains("m=video"))
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            QString("expected content to be type "
                    "\"m=video\" but it appears not to be"));
        return false;
    }

    return true;
}

bool IptvRTSP::Setup(ushort &clientPort1, ushort &clientPort2)
{
    QStringList extraHeaders;
    extraHeaders.append(QString("Transport: RTP/AVP;multicast"));

    /*Parse for control Url for RTSP setup*/
    int pos = _responseContent.indexOf("m=video");
    int pos1 = _responseContent.indexOf("a=control:", pos);
    QList<QByteArray> parts = _responseContent
        .mid(pos1+QString("a=control:").size()).split('\n');
    QString controlUrl = QString(parts[0].data());
    controlUrl.remove('\r');

    if (!ProcessRequest("SETUP", controlUrl, &extraHeaders))
        return false;

    _sessionID = _responseHeaders.value("Session");
    if (_sessionID.isEmpty())
    {
        LOG(VB_RECORD, LOG_ERR, LOC +
            "session number not found in SETUP response");
        return false;
    }

    /*Parse for the client ports*/
    QStringList list = _responseHeaders.value("Transport").split(";");
    for (int i = 0; i < list.size(); i++)
    {
        if (list.at(i).contains("port"))
        {
            int pos = list.at(i).indexOf("=", 0);
            QStringList parts = list.at(i).mid(pos+1).split("-");
            clientPort1 = parts.at(0).toInt();
            clientPort2 = parts.at(1).toInt();
            break;
        }
    }
    return true;
}

bool IptvRTSP::Play(void)
{
    return ProcessRequest("PLAY");
}

bool IptvRTSP::Teardown(void)
{

    bool result = ProcessRequest("TEARDOWN");
    _sessionID = QString();
    return result;
}

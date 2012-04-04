/** -*- Mode: c++ -*-
 *  IptvRTSP
 *  Distributed as part of MythTV under GPL v2 and later.
 */

#ifndef IPTVRTSP_H
#define IPTVRTSP_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QMutex>

class IptvRTSP
{
  public:
    explicit IptvRTSP(const QUrl&);

    bool GetOptions(QStringList &options);
    bool Describe(void);
    bool Setup(ushort &clientPort1, ushort &clientPort2);
    bool Play(void);
    bool Teardown(void);

  protected:
    bool ProcessRequest(
        const QString &method, const QString &controlUrl = QString(),
        const QStringList *headers = NULL);

  private:
    QString     _ip;
    ushort      _port;
    uint        _sequenceNumber;
    QString      _sessionID;
    QString     _requestUrl;

    int                     _responseCode;
    QString                 _responseMessage;
    QHash<QString,QString>  _responseHeaders;
    QByteArray              _responseContent;

    static QMutex _rtspMutex;

};

#endif // IPTVRTSP_H

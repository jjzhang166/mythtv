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
    bool Setup(void);
    bool Play(void);
    bool Teardown(void);

  protected:
    bool ProcessRequest(
        const QString &method, const QStringList *headers = NULL);

  private:
    QString     _ip;
    ushort      _port;
    uint        _sequenceNumber;
    uint        _sessionNumber;
    QString     _requestUrl;

    int                     _responseCode;
    QString                 _responseMessage;
    QHash<QString,QString>  _responseHeaders;
    QByteArray              _responseContent;

    static QMutex _rtspMutex;

};

#endif // IPTVRTSP_H

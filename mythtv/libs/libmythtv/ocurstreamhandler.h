// -*- Mode: c++ -*-

#ifndef _OCUR_STREAM_HANDLER_H_
#define _OCUR_STREAM_HANDLER_H_

#include <vector>
using namespace std;

#include <QMutex>
#include <QMap>

#include "streamhandler.h"
#include "util.h"

class QString;
class OCURStreamHandler;
class DTVSignalMonitor;
class OCURChannel;
class DeviceReadBuffer;

typedef QMap<uint,int> FilterMap;

class OCURStreamHandler : public StreamHandler
{
  public:
    static OCURStreamHandler *Get(const QString &devicename);
    static void Return(OCURStreamHandler * & ref);

    virtual void AddListener(MPEGStreamData *data,
                             bool allow_section_reader = false,
                             bool needs_drb            = false)
    {
        StreamHandler::AddListener(data, false, true);
    } // StreamHandler

  private:
    OCURStreamHandler(const QString &);
    ~OCURStreamHandler();

    bool Open(void);
    void Close(void);

    virtual void run(void); // QThread

  private:
    int                                     _device_num;
    int                                     _buf_size;
    int                                     _fd;

    // for implementing Get & Return
    static QMutex                            _handlers_lock;
    static QMap<QString, OCURStreamHandler*> _handlers;
    static QMap<QString, uint>               _handlers_refcnt;
};

#endif // _OCUR_STREAM_HANDLER_H_

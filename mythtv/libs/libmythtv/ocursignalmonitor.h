// -*- Mode: c++ -*-

#ifndef OCURSIGNALMONITOR_H
#define OCURSIGNALMONITOR_H

#include <QMap>

#include "dtvsignalmonitor.h"

class OCURChannel;
class OCURStreamHandler;

typedef QMap<uint,int> FilterMap;

class OCURSignalMonitor: public DTVSignalMonitor
{
  public:
    OCURSignalMonitor(int db_cardnum, OCURChannel* _channel,
                      uint64_t _flags = 0);
    virtual ~OCURSignalMonitor();

    void Stop(void);

  protected:
    OCURSignalMonitor(void);
    OCURSignalMonitor(const OCURSignalMonitor&);

    virtual void UpdateValues(void);
    OCURChannel *GetOCURChannel(void);

  protected:
    bool               streamHandlerStarted;
    OCURStreamHandler *streamHandler;
};

#endif // OCURSIGNALMONITOR_H

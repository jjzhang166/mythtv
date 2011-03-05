/// -*- Mode: c++ -*-

#ifndef _OCUR_CHANNEL_H_
#define _OCUR_CHANNEL_H_

#include <vector>
using namespace std;

// Qt headers
#include <QString>

// MythTV headers
#include "dtvchannel.h"
#include "soapclient.h"

class OCURChannel;
class OCURStreamHandler;
class ProgramMapTable;

class OCURChannel : public DTVChannel, SOAPClient
{
    friend class OCURSignalMonitor;
    friend class OCURRecorder;

  public:
    OCURChannel(TVRec *parent, const QString &device);
    ~OCURChannel(void);

    // Commands
    bool Open(void);
    void Close(void);
    bool Tune(const DTVMultiplex &tuning, QString inputname);
    bool Tune(const QString &freqid, int finetune);

    // Gets
    bool IsOpen(void) const { return !m_upnp_usn.isEmpty(); }
    QString GetDevice(void) const { return m_device; }
    virtual vector<DTVTunerType> GetTunerTypes(void) const
    { return m_tuner_types; }

  protected:
    vector<DTVTunerType>  m_tuner_types;
    QString               m_device;
    QString               m_upnp_nt;
    QString               m_upnp_usn;
};

#endif // _OCUR_CHANNEL_H_

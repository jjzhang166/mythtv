// -*- Mode: c++ -*-

// POSIX headers
#include <fcntl.h>
#include <unistd.h>
#ifndef USING_MINGW
#include <sys/select.h>
#include <sys/ioctl.h>
#endif

// Qt headers
#include <QString>
#include <QFile>

// MythTV headers
#include "ocurstreamhandler.h"
#include "ocurchannel.h"
#include "dtvsignalmonitor.h"
#include "streamlisteners.h"
#include "mpegstreamdata.h"
#include "cardutil.h"
#include "upnp.h"

#define LOC      QString("OCURSH(%1): ").arg(_device)
#define LOC_WARN QString("OCURSH(%1) Warning: ").arg(_device)
#define LOC_ERR  QString("OCURSH(%1) Error: ").arg(_device)

QMap<QString,OCURStreamHandler*> OCURStreamHandler::_handlers;
QMap<QString,uint>               OCURStreamHandler::_handlers_refcnt;
QMutex                           OCURStreamHandler::_handlers_lock;

OCURStreamHandler *OCURStreamHandler::Get(const QString &devname)
{
    QMutexLocker locker(&_handlers_lock);

    QString devkey = devname.toUpper();

    QMap<QString,OCURStreamHandler*>::iterator it = _handlers.find(devkey);

    if (it == _handlers.end())
    {
        OCURStreamHandler *newhandler = new OCURStreamHandler(devname);
        newhandler->Open();
        _handlers[devkey] = newhandler;
        _handlers_refcnt[devkey] = 1;

        VERBOSE(VB_RECORD,
                QString("OCURSH: Creating new stream handler %1 for %2")
                .arg(devkey).arg(devname));
    }
    else
    {
        _handlers_refcnt[devkey]++;
        uint rcount = _handlers_refcnt[devkey];
        VERBOSE(VB_RECORD,
                QString("OCURSH: Using existing stream handler %1 for %2")
                .arg(devkey)
                .arg(devname) + QString(" (%1 in use)").arg(rcount));
    }

    return _handlers[devkey];
}

void OCURStreamHandler::Return(OCURStreamHandler * & ref)
{
    QMutexLocker locker(&_handlers_lock);

    QString devname = ref->_device;
    QString devkey = devname.toUpper();

    QMap<QString,uint>::iterator rit = _handlers_refcnt.find(devkey);
    if (rit == _handlers_refcnt.end())
        return;

    if (*rit > 1)
    {
        ref = NULL;
        (*rit)--;
        return;
    }

    QMap<QString,OCURStreamHandler*>::iterator it = _handlers.find(devkey);
    if ((it != _handlers.end()) && (*it == ref))
    {
        VERBOSE(VB_RECORD, QString("OCURSH: Closing handler for %1")
                           .arg(devname));
        ref->Close();
        delete *it;
        _handlers.erase(it);
    }
    else
    {
        VERBOSE(VB_IMPORTANT,
                QString("OCURSH Error: Couldn't find handler for %1")
                .arg(devname));
    }

    _handlers_refcnt.erase(rit);
    ref = NULL;
}

OCURStreamHandler::OCURStreamHandler(const QString &device) :
    StreamHandler(device), _device_num(-1), _buf_size(-1), _fd(-1)
{
}

OCURStreamHandler::~OCURStreamHandler()
{
    if (!_stream_data_list.empty())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "dtor & _stream_data_list not empty");
    }
}

void OCURStreamHandler::run(void)
{
    VERBOSE(VB_RECORD, LOC + "run(): begin");

    if (!Open())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("Failed to open device %1 : %2")
                .arg(_device).arg(strerror(errno)));
        _error = true;
        return;
    }

    // TODO use _buf_size...
    DeviceReadBuffer *drb = new DeviceReadBuffer(this);
    bool ok = drb->Setup(_device, _fd);
    if (!ok)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Failed to allocate DRB buffer");
        delete drb;
        Close();
        _error = true;
        return;
    }

    int buffer_size = TSPacket::kSize * 15000;
    unsigned char *buffer = new unsigned char[buffer_size];
    if (!buffer)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Failed to allocate buffer");
        delete drb;
        Close();
        _error = true;
        return;
    }
    bzero(buffer, buffer_size);

    SetRunning(true, true, false);

    drb->Start();

    int remainder = 0;
    while (_running_desired && !_error)
    {
        UpdateFiltersFromStreamData();

        ssize_t len = 0;

        len = drb->Read(
            &(buffer[remainder]), buffer_size - remainder);

        // Check for DRB errors
        if (drb->IsErrored())
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "Device error detected");
            _error = true;
        }

        if (drb->IsEOF())
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "Device EOF detected");
            _error = true;
        }

        if ((0 == len) || (-1 == len))
        {
            usleep(100);
            continue;
        }

        len += remainder;

        if (len < 10) // 10 bytes = 4 bytes TS header + 6 bytes PES header
        {
            remainder = len;
            continue;
        }

        _listener_lock.lock();

        if (_stream_data_list.empty())
        {
            _listener_lock.unlock();
            continue;
        }

        StreamDataList::const_iterator it = _stream_data_list.begin();
        for (; it != _stream_data_list.end(); ++it)
            remainder = it.key()->ProcessData(buffer, len);

        _listener_lock.unlock();

        if (remainder > 0 && (len > remainder)) // leftover bytes
            memmove(buffer, &(buffer[len - remainder]), remainder);
    }
    VERBOSE(VB_RECORD, LOC + "run(): " + "shutdown");

    RemoveAllPIDFilters();

    if (drb->IsRunning())
        drb->Stop();

    delete drb;
    delete[] buffer;
    Close();

    VERBOSE(VB_RECORD, LOC + "run(): " + "end");

    SetRunning(false, true, false);
}

bool OCURStreamHandler::Open(void)
{
    if (_fd >= 0)
        return true;

    QStringList dev = _device.split(":");
    if (dev.size() < 2)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Invalid device, should be in the form uuid:recorder_number"); 
        return false;
    }
    QString uuid = dev[0].toLower();
    uint    num  = dev[1].toUInt();

    // HACK HACK HACK - begin
    QString devive = QString("/dev/ceton/ctn91xx_mpeg3_%1").arg(num);

    // actually open the device
    _fd = open(devive.toLocal8Bit().constData(), O_RDONLY, 0);
    if (_fd < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("Failed to open '%1'").arg(devive) + ENO);
        return false;
    }
    // HACK HACK HACK - end

    return true;
}

void OCURStreamHandler::Close(void)
{
    close(_fd);
    _fd = -1;
}

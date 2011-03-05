// -*- Mode: c++ -*-
// Copyright (c) 2006, Daniel Thor Kristjansson

#include <cerrno>
#include <cstring>

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef USING_MINGW
#include <sys/select.h>
#endif

#include "mythverbose.h"
#include "mythdbcon.h"
#include "ocursignalmonitor.h"
#include "atscstreamdata.h"
#include "mpegtables.h"
#include "atsctables.h"

#include "ocurchannel.h"
#include "ocurrecorder.h"
#include "ocurstreamhandler.h"

#define LOC QString("OCURSM(%1): ").arg(channel->GetDevice())
#define LOC_ERR QString("OCURSM(%1), Error: ").arg(channel->GetDevice())

/**
 *  \brief Initializes signal lock and signal values.
 *
 *   Start() must be called to actually begin continuous
 *   signal monitoring. The timeout is set to 3 seconds,
 *   and the signal threshold is initialized to 0%.
 *
 *  \param db_cardnum Recorder number to monitor,
 *                    if this is less than 0, SIGNAL events will not be
 *                    sent to the frontend even if SetNotifyFrontend(true)
 *                    is called.
 *  \param _channel OCURChannel for card
 *  \param _flags   Flags to start with
 */
OCURSignalMonitor::OCURSignalMonitor(
    int db_cardnum, OCURChannel* _channel, uint64_t _flags) :
    DTVSignalMonitor(db_cardnum, _channel, _flags),
    streamHandlerStarted(false), streamHandler(NULL)
{
    VERBOSE(VB_CHANNEL, LOC + "ctor");

    signalStrength.SetThreshold(45);

    AddFlags(kSigMon_WaitForSig);

    streamHandler = OCURStreamHandler::Get(_channel->GetDevice());
}

/** \fn OCURSignalMonitor::~OCURSignalMonitor()
 *  \brief Stops signal monitoring and table monitoring threads.
 */
OCURSignalMonitor::~OCURSignalMonitor()
{
    VERBOSE(VB_CHANNEL, LOC + "dtor");
    Stop();
    OCURStreamHandler::Return(streamHandler);
}

/** \fn OCURSignalMonitor::Stop(void)
 *  \brief Stop signal monitoring and table monitoring threads.
 */
void OCURSignalMonitor::Stop(void)
{
    VERBOSE(VB_CHANNEL, LOC + "Stop() -- begin");
    SignalMonitor::Stop();
    if (GetStreamData())
        streamHandler->RemoveListener(GetStreamData());
    streamHandlerStarted = false;

    VERBOSE(VB_CHANNEL, LOC + "Stop() -- end");
}

OCURChannel *OCURSignalMonitor::GetOCURChannel(void)
{
    return dynamic_cast<OCURChannel*>(channel);
}

/** \fn OCURSignalMonitor::UpdateValues(void)
 *  \brief Fills in frontend stats and emits status Qt signals.
 *
 *   This is automatically called by MonitorLoop(), after Start()
 *   has been used to start the signal monitoring thread.
 */
void OCURSignalMonitor::UpdateValues(void)
{
    if (!running || exit)
        return;

    if (streamHandlerStarted)
    {
        EmitStatus();
        if (IsAllGood())
            SendMessageAllGood();

        // TODO dtv signals...

        update_done = true;
        return;
    }

    // Set SignalMonitorValues from info from card.
    bool isLocked = true;
    {
        QMutexLocker locker(&statusLock);
        signalStrength.SetValue(100);
        signalLock.SetValue(1);
    }

    EmitStatus();
    if (IsAllGood())
        SendMessageAllGood();

    // Start table monitoring if we are waiting on any table
    // and we have a lock.
    if (isLocked && GetStreamData() &&
        HasAnyFlag(kDTVSigMon_WaitForPAT | kDTVSigMon_WaitForPMT |
                   kDTVSigMon_WaitForMGT | kDTVSigMon_WaitForVCT |
                   kDTVSigMon_WaitForNIT | kDTVSigMon_WaitForSDT))
    {
        streamHandler->AddListener(GetStreamData());
        streamHandlerStarted = true;
    }

    update_done = true;
}

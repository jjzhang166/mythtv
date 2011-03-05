/*  -*- Mode: c++ -*-
 *   Class OCURRecorder
 *
 *   Copyright (C) Daniel Kristjansson 2010
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// Qt includes
#include <QString>

// MythTV includes
#include "ocurstreamhandler.h"
#include "ocurrecorder.h"
#include "ocurchannel.h"
#include "tv_rec.h"

#define LOC QString("OCURRec(%1): ").arg(tvrec->GetCaptureCardNum())
#define LOC_WARN QString("OCURRec(%1), Warning: ") \
                     .arg(tvrec->GetCaptureCardNum())
#define LOC_ERR QString("OCURRec(%1), Error: ") \
                    .arg(tvrec->GetCaptureCardNum())

OCURRecorder::OCURRecorder(TVRec *rec, OCURChannel *channel) :
    DTVRecorder(rec), m_channel(channel), m_stream_handler(NULL)
{
}

void OCURRecorder::SetOptionsFromProfile(RecordingProfile *profile,
                                        const QString &videodev,
                                        const QString &audiodev,
                                        const QString &vbidev)
{
    DTVRecorder::SetOptionsFromProfile(profile, videodev, audiodev, vbidev);
    SetIntOption(profile, "recordmpts");
}

void OCURRecorder::StartRecording(void)
{
    if (!Open())
    {
        _error = "Failed to open device";
        VERBOSE(VB_IMPORTANT, LOC_ERR + _error);
        return;
    }

    _continuity_error_count = 0;

    {
        QMutexLocker locker(&pauseLock);
        request_recording = true;
        recording = true;
        recordingWait.wakeAll();
    }


    // Listen for time table on DVB standard streams
    if (m_channel && (m_channel->GetSIStandard() == "dvb"))
        _stream_data->AddListeningPID(DVB_TDT_PID);

    // Make sure the first things in the file are a PAT & PMT
    bool tmp = _wait_for_keyframe_option;
    _wait_for_keyframe_option = false;
    HandleSingleProgramPAT(_stream_data->PATSingleProgram());
    HandleSingleProgramPMT(_stream_data->PMTSingleProgram());
    _wait_for_keyframe_option = tmp;

    _stream_data->AddAVListener(this);
    _stream_data->AddWritingListener(this);
    m_stream_handler->AddListener(_stream_data);

    while (IsRecordingRequested() && !IsErrored())
    {
        if (PauseAndWait())
            continue;

        {   // sleep 100 milliseconds unless StopRecording() or Unpause()
            // is called, just to avoid running this too often.
            QMutexLocker locker(&pauseLock);
            if (!request_recording || request_pause)
                continue;
            unpauseWait.wait(&pauseLock, 100);
        }

        if (!_input_pmt)
        {
            VERBOSE(VB_GENERAL, LOC_WARN +
                    "Recording will not commence until a PMT is set.");
            usleep(5000);
            continue;
        }

        if (!m_stream_handler->IsRunning())
        {
            _error = "Stream handler died unexpectedly.";
            VERBOSE(VB_IMPORTANT, LOC_ERR + _error);
        }
    }

    m_stream_handler->RemoveListener(_stream_data);
    _stream_data->RemoveWritingListener(this);
    _stream_data->RemoveAVListener(this);

    Close();

    FinishRecording();

    QMutexLocker locker(&pauseLock);
    recording = false;
    recordingWait.wakeAll();
}

bool OCURRecorder::Open(void)
{
    if (IsOpen())
    {
        VERBOSE(VB_GENERAL, LOC_WARN + "Card already open");
        return true;
    }

    memset(_stream_id,  0, sizeof(_stream_id));
    memset(_pid_status, 0, sizeof(_pid_status));
    memset(_continuity_counter, 0xff, sizeof(_continuity_counter));
    _continuity_error_count = 0;

    m_stream_handler = OCURStreamHandler::Get(m_channel->GetDevice());

    VERBOSE(VB_RECORD, LOC + "Opened successfully");

    return true;
}

bool OCURRecorder::IsOpen(void) const
{
    return m_stream_handler;
}

void OCURRecorder::Close(void)
{
    VERBOSE(VB_RECORD, LOC + "Close() -- begin");

    if (IsOpen())
        OCURStreamHandler::Return(m_stream_handler);

    VERBOSE(VB_RECORD, LOC + "Close() -- end");
}

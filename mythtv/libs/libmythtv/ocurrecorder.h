// -*- Mode: c++ -*-
/*
 *  Copyright (C) Daniel Kristjansson 2010
 *
 *  Copyright notice is in asirecorder.cpp of the MythTV project.
 */

#ifndef _OCUR_RECORDER_H_
#define _OCUR_RECORDER_H_

// MythTV includes
#include "dtvrecorder.h"

class OCURStreamHandler;
class RecordingProfile;
class OCURChannel;
class QString;
class TVRec;

/** \class OCURRecorder
 *  \brief This is a specialization of DTVRecorder used to
 *         handle streams from OCUR drivers.
 *
 *  \sa DTVRecorder
 */
class OCURRecorder : public DTVRecorder
{
  public:
    OCURRecorder(TVRec *rec, OCURChannel *channel);

    void SetOptionsFromProfile(RecordingProfile *profile,
                               const QString &videodev,
                               const QString &audiodev,
                               const QString &vbidev);

    void StartRecording(void);

    bool Open(void);
    bool IsOpen(void) const;
    void Close(void);

  private:
    OCURChannel       *m_channel;
    OCURStreamHandler *m_stream_handler;
};

#endif // _OCUR_RECORDER_H_

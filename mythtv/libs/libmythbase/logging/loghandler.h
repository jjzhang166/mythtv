/*
 *  Class LogHandler
 *
 *  Copyright (C) Daniel Thor Kristjansson 2013
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

#ifndef _LOG_HANDLER_H_
#define _LOG_HANDLER_H_

class QString;
class LogEntry;

class LogHandler
{
  public:
    /// This handles logging messages
    virtual void HandleLog(const LogEntry&) = 0;
    /// This handles messages to be sent to the console for the user (if applicable).
    virtual void HandlePrint(const LogEntry&) {}
    /// This handles log rotation (if applicable).
    virtual void RotateLogs(void) {} // for SIGHUP handling
    /// Flush() is called every few messages, a LogHandler may choose
    /// to buffer any messages up until Flush() is called and be
    /// assured that the logging will still stay current.
    virtual void Flush(void) {}

    // factory methods
    static LogHandler *GetConsoleHandler(void);
    static LogHandler *GetFileHandler(const QString&);
    static LogHandler *GetPathHandler(const QString&);
};

#endif // _LOG_HANDLER_H_

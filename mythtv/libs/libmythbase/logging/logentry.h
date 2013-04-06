/*
 *  Class LogEntry
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

#ifndef _LOG_ENTRY_H_
#define _LOG_ENTRY_H_

#include <stdint.h>

#include <QDateTime>
#include <QString>

class LogEntry
{
  public:
    /**
     * \note We hash file & function strings because they never change
     * \note We keep the thread_name as a QString because some threads
     *       change their names. Notably QRunnables in the threadpool
     *       change the name of the worker thread and the main or UI
     *       thread becomes the CoreContext thread for much of it's
     *       lifetime.
     */
    LogEntry(const QDateTime &when,
             uint64_t process_id, Qt::HANDLE thread_id,
             const QString &thread_name,
             uint64_t mask, int32_t level, int32_t file_hash,
             int32_t line, int32_t func_hash,
             const QString &msg) :
        m_when(when), m_mask(mask),
        m_processId(process_id), m_threadId(thread_id),
        m_funcHash(func_hash), m_fileHash(file_hash),
        m_line(line), m_level(level),
        m_msg(msg), m_threadName(thread_name)
    {
    }

    /** Creates Special log entry to send messages to the stdout */
    LogEntry(const QString &msg, bool flush) :
        m_when(), m_mask(0),
        m_processId(flush ? ~0ULL : 0ULL), m_threadId(0),
        m_funcHash(0), m_fileHash(0),
        m_line(-1), m_level(-1),
        m_msg(msg), m_threadName()
    {
    }

    /// Creates invalid log entry
    LogEntry() :
        m_when(), m_mask(0),
        m_processId(0), m_threadId(0),
        m_funcHash(0), m_fileHash(0),
        m_line(-1), m_level(-1),
        m_msg(), m_threadName()
    {
    }

    QString toString() const;
    bool IsPrint(void) const { return m_line < 0; }
    bool IsFlush(void) const { return m_processId == ~0ULL; }

    uint64_t GetMask(void) const { return m_mask; }
    int GetLevel(void) const { return m_level; }
    QString GetMessage(void) const { return m_msg; }

  private:
    QDateTime m_when;
    uint64_t m_mask;
    uint64_t m_processId;
    Qt::HANDLE m_threadId;
    int32_t m_funcHash;
    int32_t m_fileHash;
    int16_t m_line;
    int16_t m_level;
    QString m_msg;
    QString m_threadName;
};

#endif // _LOG_ENTRY_H_

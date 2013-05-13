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

#ifndef _DEBUG_LOG_HANDLER_H_
#define _DEBUG_LOG_HANDLER_H_

#include <vector>
using namespace std;

#include <QReadWriteLock>
#include <QDateTime>
#include <QString>
#include <QHash>
#include <QSet>

#include "syslogloghandler.h" // for Syslog::format()
#include "mythbaseexp.h"
#include "loghandler.h"
#include "logentry.h"

typedef enum
{
    kHandleLog,
    kHandlePrint,
    kRotateLogs,
    kFlush,
    kNone,
} Operator;

class MBASE_PUBLIC DebugLogHandlerEntry
{
  public:
    DebugLogHandlerEntry(Operator o, const LogEntry &e) :
        m_operator(o), m_logEntry(e)
    {
    }

    DebugLogHandlerEntry(Operator o) : m_operator(o)
    {
    }

    DebugLogHandlerEntry() : m_operator(kNone)
    {
    }

    Operator op(void) const { return m_operator; }
    LogEntry entry(void) const { return m_logEntry; }

    QString toString() const
    {
        QString opStr;
        switch (op())
        {
            case kHandleLog: opStr = "kHandleLog"; break;
            case kHandlePrint: opStr = "kHandlePrint"; break;
            case kRotateLogs: opStr = "kRotateLogs"; break;
            case kFlush: opStr = "kFlush"; break;
            case kNone: opStr = "kNone"; break;
        }

        return opStr + ": " + entry().toString();
    }

  private:
    Operator m_operator;
    LogEntry m_logEntry;
};

class MBASE_PUBLIC DebugLogHandler : public LogHandler
{
    static QSet<QString> s_is_replacing;
    static QHash<QString, DebugLogHandler*> s_replacement;

  public:
    /// \brief Specifies which LogHandlers are being replaced for unit tests.
    /// This must not be called after logging has been initialized.
    static void AddReplacement(const QString &className)
    {
        s_is_replacing.insert(className);
    }

    static bool IsReplacing(const QString &className)
    {
        return s_is_replacing.contains(className);
    }

    static DebugLogHandler *Get(const QString &className)
    {
        if (!s_replacement.contains(className))
        {
            s_replacement[className] =
                new DebugLogHandler(className + " -- never created");
        }
        return s_replacement[className];
    }

    DebugLogHandler(const QString &classReplaced,
                    QString param = QString()) :
        m_classReplaced(classReplaced), m_param(param)
    {
        s_replacement[classReplaced] = this;
    }

    DebugLogHandler(const QString &classReplaced,
                    SyslogFacility facility) :
        m_classReplaced(classReplaced), m_param(Syslog::format(facility))
    {
        s_replacement[classReplaced] = this;
    }

    ~DebugLogHandler()
    {
        Clear();
    }

    virtual void HandleLog(const LogEntry &e)
    {
        QWriteLocker locker(&m_lock);
        m_debugEntries.push_back(new DebugLogHandlerEntry(kHandleLog, e));
    }

    virtual void HandlePrint(const LogEntry &e)
    {
        QWriteLocker locker(&m_lock);
        m_debugEntries.push_back(new DebugLogHandlerEntry(kHandlePrint, e));
    }

    virtual void RotateLogs(void)
    {
        QWriteLocker locker(&m_lock);
        m_debugEntries.push_back(new DebugLogHandlerEntry(kRotateLogs));
    }

    virtual void Flush(void)
    {
        QWriteLocker locker(&m_lock);
        m_debugEntries.push_back(new DebugLogHandlerEntry(kFlush));
    }

    DebugLogHandlerEntry LastEntry(void) const
    {
        QReadLocker locker(&m_lock);
        if (m_debugEntries.empty())
            return DebugLogHandlerEntry();
        return *m_debugEntries.back();
    }

    DebugLogHandlerEntry LastEntry(Operator op) const
    {
        QReadLocker locker(&m_lock);
        vector<DebugLogHandlerEntry*>::const_reverse_iterator it;
        for (it = m_debugEntries.rbegin(); it != m_debugEntries.rend(); ++it)
        {
            if ((*it)->op() == op)
                return **it;
        }
        return DebugLogHandlerEntry();
    }

    void Clear(void)
    {
        QWriteLocker locker(&m_lock);
        while (!m_debugEntries.empty())
        {
            delete m_debugEntries.back();
            m_debugEntries.pop_back();
        }
    }

    void Clear(Operator op)
    {
        QWriteLocker locker(&m_lock);
        vector<DebugLogHandlerEntry*> new_list;
        for (uint i = 0; i < uint(m_debugEntries.size()); i++)
        {
            if (m_debugEntries[i]->op() == op)
                new_list.push_back(m_debugEntries[i]);
            else
                delete m_debugEntries[i];
        }
        m_debugEntries = new_list;
    }

    bool IsEmpty(void) const
    {
        QReadLocker locker(&m_lock);
        return m_debugEntries.empty();
    }

    uint64_t Size(void) const
    {
        QReadLocker locker(&m_lock);
        return m_debugEntries.size();
    }

    bool Has(Operator op) const
    {
        QReadLocker locker(&m_lock);
        if (kNone != op)
            return LastEntry(op).op() == op;
        return false;
    }

    mutable QReadWriteLock m_lock;
    QString m_classReplaced;
    QString m_param;
    vector<DebugLogHandlerEntry*> m_debugEntries;
};

#endif // _DEBUG_LOG_HANDLER_H_

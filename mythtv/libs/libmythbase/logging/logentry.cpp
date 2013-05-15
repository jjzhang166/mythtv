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

#include "logentry.h"
#include "logdeque.h"

/** Returns a string with the following format:
 * 2013-03-24 11:22:11.284064 N [pid/tid] ThreadName filename.cpp:line (FunctionName) - msg
 */
QString LogEntry::toString() const
{
    return QString("%1 %2 [%3] %5 %6:%7 %8 - %9")
        .arg(m_when.toString("yyyy-MM-dd hh:mm:ss.zzz"))
        .arg(LogDeque::Get().LookupLogLevelChar(m_level))
        .arg(m_processId)
//        .arg(reinterpret_cast<uint64_t>(m_threadId))
        .arg(m_threadName)
        .arg(LogDeque::Get().LookupHashedString(m_fileHash))
        .arg(m_line)
        .arg(LogDeque::Get().LookupHashedString(m_funcHash))
        .arg(m_msg);
}

QString LogEntry::GetFunctionName(void) const
{
    return LogDeque::Get().LookupHashedString(m_funcHash);
}

QString LogEntry::GetFilename(void) const
{
    return LogDeque::Get().LookupHashedString(m_fileHash);
}

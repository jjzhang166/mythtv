/*
 *  Class ThreadInfo
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

#ifndef _THREAD_INFO_H_
#define _THREAD_INFO_H_

#include <stdint.h>

#include <QString>

class ThreadInfo
{
  public:
    ThreadInfo() : m_name("Unknown"), m_processId(0)
    {
    }

    ThreadInfo(QString name, Qt::HANDLE handle, uint64_t processId) :
        m_name(name), m_handle(handle), m_processId(processId)
    {
    }

    QString GetName(void) const { return m_name; }
    Qt::HANDLE GetThreadId(void) const { return m_handle; }
    uint64_t GetProcessId(void) const { return m_processId; }

  private:
    QString m_name;
    Qt::HANDLE m_handle;
    uint64_t m_processId;
};

#endif // _THREAD_INFO_H_

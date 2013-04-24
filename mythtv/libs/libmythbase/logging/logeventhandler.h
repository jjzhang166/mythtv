/*
 *  Class LogEventHandler
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

#ifndef _LOG_EVENT_HANDLER_H_
#define _LOG_EVENT_HANDLER_H_

#include <QObject>

class LogDeque;
class QEvent;

class LogEventHandler : public QObject
{
    Q_OBJECT

  public:
    LogEventHandler(LogDeque &logDeque) : m_logDeque(logDeque)
    {
    }

    /// Event handler -- calls LogDeque::ProcessQueue()
    bool event(QEvent *e);

    /// Notify event handler there might be logging items.
    void Notify(void);

  private:
    LogDeque &m_logDeque;
};

#endif // _LOG_EVENT_HANDLER_H_

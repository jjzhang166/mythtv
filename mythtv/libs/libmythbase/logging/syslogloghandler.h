/*
 *  Class SyslogLogHandler
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

#ifndef _SYSLOG_LOG_HANDLER_H_
#define _SYSLOG_LOG_HANDLER_H_

// MythTV
#include "mythbaseexp.h"
#include "syslogdefs.h"
#include "loghandler.h"

class QString;

namespace Syslog
{
MBASE_PUBLIC bool parse(const QString&, SyslogFacility&);
MBASE_PUBLIC QString format(SyslogFacility);
MBASE_PUBLIC bool convert(SyslogFacility facility, int &syslog_facility);
};

class MBASE_PUBLIC SyslogLogHandler : public LogHandler
{
  public:
    SyslogLogHandler(SyslogFacility);
    virtual void HandleLog(const LogEntry&);
  private:
    bool m_initialized;
    int m_syslog_facility;
};

#endif // _SYSLOG_LOG_HANDLER_H_

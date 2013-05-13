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

// POSIX
#include <syslog.h>

// Qt
#include <QCoreApplication>
#include <QByteArray>
#include <QHash>

// MythTV
#include "syslogloghandler.h"
#include "logentry.h"

namespace Syslog
{

static bool init(void);

static QHash<QString, SyslogFacility> str_to_fac;
static QHash<SyslogFacility, QString> fac_to_str;
static bool dummy = init();

static bool init(void)
{
    str_to_fac["auth"] = kAuthFacility;
    str_to_fac["authpriv"] = kAuthPrivateFacility;
    str_to_fac["cron"] = kCronFacility;
    str_to_fac["daemon"] = kDaemonFacility;
    str_to_fac["ftp"] = kFTPFacility;
    str_to_fac["kern"] = kKernelFacility;
    str_to_fac["lpr"] = kLPRFacility;
    str_to_fac["mail"] = kMailFacility;
    str_to_fac["news"] = kNewsFacility;
    str_to_fac["security"] = kAuthFacility;
    str_to_fac["syslog"] = kSyslogFacility;
    str_to_fac["user"] = kUserFacility;
    str_to_fac["uucp"] = kUUCPFacility;

    str_to_fac["local0"] = kLocal0Facility;
    str_to_fac["local1"] = kLocal1Facility;
    str_to_fac["local2"] = kLocal2Facility;
    str_to_fac["local3"] = kLocal3Facility;
    str_to_fac["local4"] = kLocal4Facility;
    str_to_fac["local5"] = kLocal5Facility;
    str_to_fac["local6"] = kLocal6Facility;
    str_to_fac["local7"] = kLocal7Facility;

    QHash<QString, SyslogFacility>::const_iterator it = str_to_fac.begin();
    for (; it != str_to_fac.end(); ++it)
        fac_to_str[*it] = it.key();

    return true;
}


bool parse(const QString &str, SyslogFacility &f)
{
    QHash<QString, SyslogFacility>::const_iterator it =
        str_to_fac.find(str.trimmed().toLower());

    if (it != str_to_fac.end())
    {
        f = *it;
        return true;
    }

    return false;
}

QString format(SyslogFacility f)
{
    QHash<SyslogFacility, QString>::const_iterator it = fac_to_str.find(f);
    return (it != fac_to_str.end()) ? *it : QString();
}

bool convert(SyslogFacility facility, int &syslog_facility)
{
    switch (facility)
    {
        case kNoFacility: return false;
        case kAuthFacility: syslog_facility = LOG_AUTH; return true;
        case kAuthPrivateFacility: syslog_facility = LOG_AUTHPRIV; return true;
        case kCronFacility: syslog_facility = LOG_CRON; return true;
        case kDaemonFacility: syslog_facility = LOG_DAEMON; return true;
        case kFTPFacility: syslog_facility = LOG_FTP; return true;
        case kKernelFacility: syslog_facility = LOG_KERN; return true;
        case kLPRFacility: syslog_facility = LOG_LPR; return true;
        case kMailFacility: syslog_facility = LOG_MAIL; return true;
        case kNewsFacility: syslog_facility = LOG_NEWS; return true;
        case kSyslogFacility: syslog_facility = LOG_SYSLOG; return true;
        case kUserFacility: syslog_facility = LOG_USER; return true;
        case kUUCPFacility: syslog_facility = LOG_UUCP; return true;

        case kLocal0Facility: syslog_facility = LOG_LOCAL0; return true;
        case kLocal1Facility: syslog_facility = LOG_LOCAL1; return true;
        case kLocal2Facility: syslog_facility = LOG_LOCAL2; return true;
        case kLocal3Facility: syslog_facility = LOG_LOCAL3; return true;
        case kLocal4Facility: syslog_facility = LOG_LOCAL4; return true;
        case kLocal5Facility: syslog_facility = LOG_LOCAL5; return true;
        case kLocal6Facility: syslog_facility = LOG_LOCAL6; return true;
        case kLocal7Facility: syslog_facility = LOG_LOCAL7; return true;
    }
    return false;
}

} // end of Syslog namespace

SyslogLogHandler::SyslogLogHandler(SyslogFacility facility) :
    m_initialized(false)
{
    if (!Syslog::convert(facility, m_syslog_facility))
        return;

    QByteArray app_name = QCoreApplication::applicationName().toLatin1();
    openlog(app_name.constData(), /*options*/ 0, m_syslog_facility);

    m_initialized = true;
}

void SyslogLogHandler::HandleLog(const LogEntry &e)
{
    if (!m_initialized || (e.GetLevel() < 0) || (e.GetLevel() > 7))
        return;

    syslog(m_syslog_facility | e.GetLevel(), "%s",
           e.toString().toLatin1().constData());
}


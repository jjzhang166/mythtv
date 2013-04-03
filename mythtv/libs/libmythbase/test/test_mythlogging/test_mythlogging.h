/*
 *  Class TestMythLogging
 *
 *  Copyright (C) Daniel Kristjansson 2013
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
#include <errno.h>

// C++
#include <iostream>
using namespace std;

// Qt
#include <QtTest/QtTest>

// Enable whitebox testing
#define private public

// MythTV
#include "mythlogging_extra.h"
#include "logdeque.h"
using namespace myth_logging;

class TestMythLogging : public QObject
{
    Q_OBJECT

  private slots:
    void init(void)
    {
        // we set use_threads to true so we can examine the log queue.
        bool use_threads = true;
        set_parameters(VB_CHANNEL, LOG_WARNING, -1,
                       use_threads, false);

        // HACK HACK HACK - begin
        LogDeque::Get().m_singleThreaded = false;// force single threading off
        // HACK HACK HACK - end

        QVERIFY(VB_CHANNEL == get_verbose());
        QVERIFY(LOG_WARNING == get_log_level());
    }

    void log_will_use_single_verbose(void)
    {
        QVERIFY(log_will_use(VB_CHANNEL, LOG_WARNING));
    }

    void log_will_use_single_verbose_fail(void)
    {
        QVERIFY(!log_will_use(VB_DECODE, LOG_WARNING));
    }

    void log_will_use_multi_verbose_fail(void)
    {
        QVERIFY(!log_will_use(VB_CHANNEL|VB_DECODE, LOG_WARNING));
    }

    void log_will_use_multi_verbose_pass(void)
    {
        quint64 old_v_dec = set_verbose(VB_CHANNEL|VB_DECODE);
        QVERIFY(log_will_use(VB_CHANNEL|VB_DECODE, LOG_WARNING));
        set_verbose(old_v_dec);
    }

    void log_will_use_lower_level_fail(void)
    {
        QVERIFY(!log_will_use(VB_CHANNEL, LOG_INFO));
    }

    void log_will_use_higher_level_pass(void)
    {
        QVERIFY(log_will_use(VB_CHANNEL, LOG_ERR));
    }

    void log_might_use_single_verbose(void)
    {
        QVERIFY(log_might_use(VB_CHANNEL, LOG_WARNING));
    }

    void log_might_use_single_verbose_fail(void)
    {
        QVERIFY(!log_might_use(VB_DECODE, LOG_WARNING));
    }

    void log_might_use_multi_verbose_fail(void)
    {
        QVERIFY(!log_might_use(VB_UPNP|VB_DECODE, LOG_WARNING));
    }

    void log_might_use_multi_verbose_pass(void)
    {
        QVERIFY(log_might_use(VB_CHANNEL|VB_DECODE, LOG_WARNING));
    }

    void log_might_use_lower_level_fail(void)
    {
        QVERIFY(!log_might_use(VB_CHANNEL, LOG_INFO));
    }

    void log_might_use_higher_level_pass(void)
    {
        QVERIFY(log_might_use(VB_CHANNEL, LOG_ERR));
    }

    void log_errno_to_qstring_void_extracts_from_errno(void)
    {
        errno = EACCES;
        QVERIFY(errno_to_qstring() == errno_to_qstring(EACCES));
    }

    void log_errno_to_qstring_int_contains_error_number(void)
    {
        QVERIFY(errno_to_qstring(EACCES).contains(QString::number(EACCES)));
    }

    void log_errno_to_qstring_int_contains_strerror_output(void)
    {
        QVERIFY(errno_to_qstring(EACCES).contains(strerror(EACCES)));
    }

    void LOG_logs_if_it_should(void)
    {
        LogDeque::Get().m_messages.clear();
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        QVERIFY(!LogDeque::Get().m_messages.empty());
        LogDeque::Get().m_messages.clear();
    }

    void LOG_logs_on_higher_log_level(void)
    {
        LogDeque::Get().m_messages.clear();
        LOG(VB_CHANNEL, LOG_ERR, QString(__FUNCTION__));
        QVERIFY(!LogDeque::Get().m_messages.empty());
        LogDeque::Get().m_messages.clear();
    }

    void LOG_does_not_log_on_mask_missmatch(void)
    {
        LogDeque::Get().m_messages.clear();
        LOG(VB_GENERAL, LOG_WARNING, QString(__FUNCTION__));
        QVERIFY(LogDeque::Get().m_messages.empty());
        LogDeque::Get().m_messages.clear();
    }

    void LOG_does_not_log_on_lower_log_level(void)
    {
        LogDeque::Get().m_messages.clear();
        LOG(VB_CHANNEL, LOG_INFO, QString(__FUNCTION__));
        QVERIFY(LogDeque::Get().m_messages.empty());
        LogDeque::Get().m_messages.clear();
    }

    void LOG_PRINT_logs_with_flush(void)
    {
        LogDeque::Get().m_messages.clear();
        LOG_PRINT_FLUSH(QString(__FUNCTION__));
        QVERIFY(!LogDeque::Get().m_messages.empty());
        if (!LogDeque::Get().m_messages.empty())
        {
            QVERIFY(LogDeque::Get().m_messages[0].IsPrint());
            QVERIFY(LogDeque::Get().m_messages[0].IsFlush());
            QCOMPARE(LogDeque::Get().m_messages[0].GetMessage(),
                     QString(__FUNCTION__));
        }
        LogDeque::Get().m_messages.clear();
    }

    void LOG_PRINT_logs_without_flush(void)
    {
        LogDeque::Get().m_messages.clear();
        LOG_PRINT(QString(__FUNCTION__));
        QVERIFY(!LogDeque::Get().m_messages.empty());
        if (!LogDeque::Get().m_messages.empty())
        {
            QVERIFY(LogDeque::Get().m_messages[0].IsPrint());
            QVERIFY(!LogDeque::Get().m_messages[0].IsFlush());
            QCOMPARE(LogDeque::Get().m_messages[0].GetMessage(),
                     QString(__FUNCTION__));
        }
        LogDeque::Get().m_messages.clear();
    }

    void SetLogLevelSets(void)
    {
        int old_level_i = set_log_level(LOG_INFO);
        int new_level_i = set_log_level(old_level_i);
        int old_level_d = set_log_level(LOG_DEBUG);
        int new_level_d = set_log_level(old_level_d);

        QVERIFY(new_level_i == LOG_INFO);
        QVERIFY(new_level_d == LOG_DEBUG);
    }

    void GetLogLevelGets(void)
    {
        int old_level_i = set_log_level(LOG_INFO);
        int new_level_i_get = get_log_level();
        set_log_level(old_level_i);

        int old_level_d = set_log_level(LOG_DEBUG);
        int new_level_d_get = get_log_level();
        set_log_level(old_level_d);

        QVERIFY(new_level_i_get == LOG_INFO);
        QVERIFY(new_level_d_get == LOG_DEBUG);
    }

    void SetVerboseSets(void)
    {
        quint64 old_v_mheg = set_verbose(VB_MHEG|VB_UPNP);
        quint64 new_v_mheg = set_verbose(old_v_mheg);
        quint64 old_v_dec = set_verbose(VB_DECODE);
        quint64 new_v_dec = set_verbose(old_v_dec);

        QVERIFY(new_v_mheg == (VB_MHEG|VB_UPNP));
        QVERIFY(new_v_dec == (VB_DECODE));
    }

    void GetVerboseGets(void)
    {
        quint64 old_v_mheg = set_verbose(VB_MHEG|VB_UPNP);
        quint64 new_v_mheg_get = get_verbose();
        set_verbose(old_v_mheg);

        quint64 old_v_vbi = set_verbose(VB_VBI|VB_DECODE);
        quint64 new_v_vbi_get = get_verbose();
        set_verbose(old_v_vbi);

        QVERIFY(new_v_mheg_get == (VB_MHEG|VB_UPNP));
        QVERIFY(new_v_vbi_get == (VB_VBI|VB_DECODE));
    }

    void CommandLineArgumentsSaneAboutEnds(void)
    {
        QString args = command_line_arguments();
        QVERIFY(args.length() > 2);
        QVERIFY(args[0] == QChar(' '));
        QVERIFY(args[args.length() - 1] == QChar(' '));
    }

    void CommandLineArgumentsSaneWithVerbose(void)
    {
        QString args = command_line_arguments();
        QVERIFY(args.contains(" --verbose "));
        QVERIFY(args.contains(" channel "));
    }

    void CommandLineArgumentsSaneWithMultiVerbose(void)
    {
        quint64 old_v_mheg = set_verbose(VB_MHEG|VB_UPNP|VB_DECODE);
        QString args = command_line_arguments();
        QVERIFY(args.contains(" --verbose "));
        QVERIFY(args.contains(" mheg,upnp,decode "));
        set_verbose(old_v_mheg);
    }

    void CommandLineArgumentsSaneWithNoneVerbose(void)
    {
        quint64 old_v_none = set_verbose(VB_NONE);
        QString args = command_line_arguments();
        QVERIFY(args.contains(" --verbose "));
        QVERIFY(args.contains(" none "));
        set_verbose(old_v_none);
    }

    void CommandLineArgumentsSaneWithLogLevel(void)
    {
        QString args = command_line_arguments();
        QVERIFY(args.contains(" --loglevel"));
        QVERIFY(args.contains(" warning"));
    }

    void FormatVerboseOne(void)
    {
        QVERIFY(format_verbose(VB_CHANNEL) == "channel");
    }

    void FormatVerboseMulti(void)
    {
        QVERIFY(format_verbose(VB_MHEG|VB_UPNP|VB_DECODE) ==
                "mheg,upnp,decode");
    }

    void FormatVerboseNone(void)
    {
        QVERIFY(format_verbose(VB_NONE) == "none");
    }

    void FormatLogLevelInfo(void)
    {
        QVERIFY(format_log_level(LOG_INFO) == "info");
    }

    void FormatLogLevelInvalid(void)
    {
        QVERIFY(format_log_level(LOG_DEBUG+99).isEmpty());
    }

    void ParseVerboseValidSingleAdditive(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("decode", sub, add);
        QVERIFY(ok);
        QVERIFY(sub == VB_NONE);
        QVERIFY(add == VB_DECODE);
    }

    void ParseVerboseValidMultiAdditive(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("mheg,upnp,decode", sub, add);
        QVERIFY(ok);
        QVERIFY(sub == VB_NONE);
        QVERIFY(add == (VB_MHEG|VB_UPNP|VB_DECODE));
    }

    void ParseVerboseValidMixedCase(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("MHEG,UPnP,Decode", sub, add);
        QVERIFY(ok);
        QVERIFY(sub == VB_NONE);
        QVERIFY(add == (VB_MHEG|VB_UPNP|VB_DECODE));
    }

    void ParseVerboseValidNonAdditive(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("none,mheg,upnp,decode", sub, add);
        QVERIFY(ok);
        QVERIFY((sub | add) == VB_ALL);
        QVERIFY(add == (VB_MHEG|VB_UPNP|VB_DECODE));
    }

    void ParseVerboseValidNonAdditiveInMiddle(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("general,mheg,none,upnp,decode", sub, add);
        QVERIFY(ok);
        QVERIFY((sub | add) == VB_ALL);
        QVERIFY(add == (VB_UPNP|VB_DECODE));
    }

    void ParseVerboseInvalid(void)
    {
        uint64_t sub = 0xDeadBeef, add = 0x31137;
        bool ok = parse_verbose("decoder", sub, add);
        QVERIFY(!ok);
        QVERIFY(sub == 0xDeadBeef);
        QVERIFY(add == 0x31137);
    }

    void ParseVerboseValidLateSubtractive(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("mheg,upnp,nomheg,decode", sub, add);
        QVERIFY(ok);
        QVERIFY(sub == VB_MHEG);
        QVERIFY(add == (VB_UPNP|VB_DECODE));
    }

    void ParseVerboseValidEarlySubtractive(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("nomheg,upnp,mheg,decode", sub, add);
        QVERIFY(ok);
        QVERIFY(sub == VB_NONE);
        QVERIFY(add == (VB_MHEG|VB_UPNP|VB_DECODE));
    }

    void ParseVerboseValidSimpleSubtractive(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("nomheg,upnp,decode", sub, add);
        QVERIFY(ok);
        QVERIFY(sub == VB_MHEG);
        QVERIFY(add == (VB_UPNP|VB_DECODE));
    }

};

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
#include <QTemporaryFile>
#include <QByteArray>
#include <QString>
#include <QFile>

// Enable whitebox testing
#define private public

// MythTV
#include "compat.h" // for usleep
#include "debugloghandler.h"
#include "mythlogging_extra.h"
#include "logdeque.h"
using namespace myth_logging;

static DebugLogHandler *console_dbg()
{
    return DebugLogHandler::Get("ConsoleLogHandler");
}

static QString get_file_contents(const QString &file_name)
{
    QFile f(file_name);
    f.open(QIODevice::ReadOnly);
    QByteArray ba = f.readAll();
    return QString::fromUtf8(ba.constData(), ba.size());
}

class TestMythLogging : public QObject
{
    Q_OBJECT

    QString logfile;

  private slots:
    void init(void)
    {
        DebugLogHandler::AddReplacement("ConsoleLogHandler");
        DebugLogHandler::AddReplacement("PathLogHandler");

        {
            QTemporaryFile tf;
            tf.open();
            logfile = tf.fileName();
        }

        bool use_threads = false;
        QString logpath;
        initialize_logging(
            VB_CHANNEL, LOG_WARNING, -1, use_threads, false,
            logfile, logpath);

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
        console_dbg()->Clear();
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        QVERIFY(console_dbg()->Has(kHandleLog));
        console_dbg()->Clear();
    }

    void LOG_logs_on_higher_log_level(void)
    {
        console_dbg()->Clear();
        LOG(VB_CHANNEL, LOG_ERR, QString(__FUNCTION__));
        QVERIFY(console_dbg()->Has(kHandleLog));
        console_dbg()->Clear();
    }

    void LOG_does_not_log_on_mask_missmatch(void)
    {
        console_dbg()->Clear();
        LOG(VB_GENERAL, LOG_WARNING, QString(__FUNCTION__));
        QVERIFY(!console_dbg()->Has(kHandleLog));
        console_dbg()->Clear();
    }

    void LOG_does_not_log_on_lower_log_level(void)
    {
        console_dbg()->Clear();
        LOG(VB_CHANNEL, LOG_INFO, QString(__FUNCTION__));
        QVERIFY(!console_dbg()->Has(kHandleLog));
        console_dbg()->Clear();
    }

    void LOG_logs_to_file(void)
    {
        console_dbg()->Clear();
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        usleep(500 * 1000);
        QString s = get_file_contents(logfile);
        QVERIFY(s.contains(QString(__FUNCTION__)));
        console_dbg()->Clear();
    }

    void LOG_PRINT_logs_with_flush(void)
    {
        console_dbg()->Clear();
        LOG_PRINT_FLUSH(QString(__FUNCTION__));
        QVERIFY(console_dbg()->Has(kHandlePrint));
        DebugLogHandlerEntry l = console_dbg()->LastEntry(kHandlePrint);
        QVERIFY(l.entry().IsPrint());
        QVERIFY(l.entry().IsFlush());
        QCOMPARE(l.entry().GetMessage(), QString(__FUNCTION__));
        console_dbg()->Clear();
    }

    void LOG_PRINT_logs_without_flush(void)
    {
        console_dbg()->Clear();
        LOG_PRINT(QString(__FUNCTION__));
        QVERIFY(console_dbg()->Has(kHandlePrint));
        DebugLogHandlerEntry l = console_dbg()->LastEntry(kHandlePrint);
        QVERIFY(l.entry().IsPrint());
        QVERIFY(!l.entry().IsFlush());
        QCOMPARE(l.entry().GetMessage(), QString(__FUNCTION__));
        console_dbg()->Clear();
    }

    void log_line_c_accepts_var_args(void)
    {
        console_dbg()->Clear();
        log_line_c(VB_CHANNEL, LOG_WARNING, __FILE__, __LINE__,
                   __FUNCTION__, "%5.2f", 55.55555f);
        QVERIFY(console_dbg()->Has(kHandleLog));
        QVERIFY(console_dbg()->LastEntry(kHandleLog).entry().GetMessage()
                .contains("55.56"));
        console_dbg()->Clear();
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

    void ParseVerboseTrims(void)
    {
        uint64_t sub, add;
        bool ok = parse_verbose("\n\t mheg,upnp,decode \t\n", sub, add);
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

    void parse_log_level_recognizes_real_levels(void)
    {
        int level;
        bool ok = parse_log_level("err", level);
        QVERIFY(ok);
        QVERIFY(LOG_ERR == level);
    }

    void parse_log_level_case_insensitive(void)
    {
        int level;
        bool ok = parse_log_level("eRr", level);
        QVERIFY(ok);
        QVERIFY(LOG_ERR == level);
    }

    void parse_log_level_trims(void)
    {
        int level;
        bool ok = parse_log_level("\t\n err \t\n", level);
        QVERIFY(ok);
        QVERIFY(LOG_ERR == level);
    }

    void parse_log_level_rejects_non_parseable(void)
    {
        int level;
        bool uok = parse_log_level("unknown", level);
        bool aok = parse_log_level("any", level);
        QVERIFY(!uok && !aok);
    }

    void parse_log_level_rejects_random_strings(void)
    {
        int level;
        bool rok = parse_log_level("random", level);
        QVERIFY(!rok);
    }

    void get_verbose_help_contains_generated_info(void)
    {
        QString help = get_verbose_help();
        QVERIFY(help.contains("general"));
        QVERIFY(help.contains("upnp"));
        QVERIFY(help.contains("decode"));
    }

    void test_register_thread(void)
    {
        console_dbg()->Clear();
        QString old_name = register_thread("SillyNameRegister");
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        QVERIFY(console_dbg()->Has(kHandleLog));
        DebugLogHandlerEntry l = console_dbg()->LastEntry(kHandleLog);
        QVERIFY(l.entry().toString().contains("SillyNameRegister"));
        register_thread(old_name);
        console_dbg()->Clear();
    }

    void test_deregister_thread(void)
    {
        console_dbg()->Clear();
        QString old_name = register_thread("SillyNameUnregister");
        QString new_name = deregister_thread();
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        QVERIFY(console_dbg()->Has(kHandleLog));
        DebugLogHandlerEntry l = console_dbg()->LastEntry(kHandleLog);
        QVERIFY(!l.entry().toString().contains("SillyNameRegister"));
        QVERIFY(l.entry().toString().contains("Unknown"));
        register_thread(old_name);
        console_dbg()->Clear();
    }
};

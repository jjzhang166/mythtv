/*
 *  Class TestMythLoggingBase
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

#ifndef _TEST_MYTH_LOGGING_BASE_
#define _TEST_MYTH_LOGGING_BASE_

// POSIX
#include <errno.h>

// C++
#include <iostream>
using namespace std;

// Qt
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <QFile>
#include <QList>
#include <QDir>

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

static QString get_logpath_file_contents(const QString &logprefix)
{
    QString logpath = logprefix.mid(0,logprefix.lastIndexOf('/'));
    QString prefix = logprefix.mid(logprefix.lastIndexOf('/')+1);
    QFileInfoList fl = QDir(logpath).entryInfoList(QStringList(prefix+"*"));
    return (fl.empty()) ? QString() : get_file_contents(fl[0].filePath());
}

static bool rmdir_rf(const QString &item)
{
    QFileInfo fi(item);
    if (!fi.isDir())
        return false;

    bool ok = true;
    QFileInfoList fl = QDir(item).entryInfoList(
        QDir::Dirs|QDir::Files|QDir::System|QDir::Hidden|QDir::NoDotAndDotDot);
    for (QFileInfoList::iterator it = fl.begin(); it != fl.end() && ok; ++it)
    {
        ok = (*it).isDir() ? rmdir_rf((*it).canonicalFilePath()):
            QFile((*it).canonicalFilePath()).remove();
    }
    if (!ok)
        return false;

    return QDir(item.mid(0,item.lastIndexOf('/')))
        .rmdir(item.mid(item.lastIndexOf('/')+1));
}

static qint64 diff(const QDateTime &a, const QDateTime &b)
{
    qint64 e = a.toMSecsSinceEpoch() - b.toMSecsSinceEpoch();
    return (e >= 0) ? e : -e;
}

/// Waits until the log stops being appended to
/// and returns the time of the last appending.
static QDateTime wait_for_log_thread_completion(void)
{
    quint64 old_sz = 0, new_sz = 0;
    QDateTime last_append = QDateTime::currentDateTimeUtc();
    while (true)
    {
        old_sz = new_sz;
        new_sz = console_dbg()->Size();
        QDateTime new_dt = QDateTime::currentDateTimeUtc();
        if (old_sz != new_sz)
        {
            last_append = new_dt;
        }
        else if (diff(last_append, new_dt) > 15)
        {
            break;
        }
        usleep(5 * 1000);
    }
    return last_append;
}

static void log_many_times(uint how_many)
{
    QString what = QString(__FUNCTION__);
    for (uint i = 0; i < how_many; i++)
        LOG(VB_CHANNEL, LOG_WARNING, what);
}

class TestMythLoggingBase : public QObject
{
    Q_OBJECT

    QString logfile;
    QString logpath;
    QString logprefix;
    bool wasCalled;

    void clearWasCalled(void)
    {
        wasCalled = false;
    }

    QString setWasCalled(void)
    {
        wasCalled = true;
        return QString();
    }

  private slots:

    // called at the beginning of these sets of tests
    void initTestCase(bool use_threads)
    {
        DebugLogHandler::AddReplacement("ConsoleLogHandler");

        { // setup temporary log file
            QTemporaryFile tf;
            tf.open();
            logfile = tf.fileName();
        }

        { // setup temporary log path
            QDir temp_root = QDir::temp();
            QString tmp_dir;
            qsrand(QDateTime::currentDateTime().toTime_t());
            for (uint i = 0; i < 10 && tmp_dir.isEmpty(); i++)
            {
                tmp_dir = QString("temp.dir.%1").arg(qrand()%1000000);
                if (!temp_root.mkdir(tmp_dir))
                    tmp_dir.clear();
            }
            if (!tmp_dir.isEmpty())
            {
                logpath = temp_root.path()+"/"+tmp_dir;
                logprefix = logpath + "/test_mythlogging";
            }
        }

        initialize_logging(
            VB_CHANNEL, LOG_WARNING, -1, use_threads, false,
            logfile, logprefix);

        QVERIFY(!logfile.isEmpty());
        QVERIFY(!logpath.isEmpty());
        QVERIFY(VB_CHANNEL == get_verbose());
        QVERIFY(LOG_WARNING == get_log_level());
    }

    // called at the end of these sets of tests
    void cleanupTestCase(void)
    {
        thread_shutdown();

        QFile qfile_logfile(logfile);
        QVERIFY(qfile_logfile.remove());

        if (!rmdir_rf(logpath))
        {
            cerr << "failed to delete" << qPrintable(logpath) << endl;
            QVERIFY(false);
        }
    }

    // called before each test case
    void init(void)
    {
        console_dbg()->Clear();
    }

    // called after each test case
    void cleanup(void)
    {
        wait_for_log_thread_completion();
        console_dbg()->Clear();
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

    void LOG_does_not_interpret_unlogged_message(void)
    {
        clearWasCalled();
        LOG(VB_CHANNEL, LOG_DEBUG, QString(__FUNCTION__) + setWasCalled());
        QVERIFY(!wasCalled);
    }

    void LOG_logs_if_it_should(void)
    {
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        wait_for_log_thread_completion();
        QVERIFY(console_dbg()->Has(kHandleLog));
    }

    void LOG_logs_on_higher_log_level(void)
    {
        LOG(VB_CHANNEL, LOG_ERR, QString(__FUNCTION__));
        wait_for_log_thread_completion();
        QVERIFY(console_dbg()->Has(kHandleLog));
    }

    void LOG_does_not_log_on_mask_missmatch(void)
    {
        LOG(VB_GENERAL, LOG_WARNING, QString(__FUNCTION__));
        wait_for_log_thread_completion();
        QVERIFY(!console_dbg()->Has(kHandleLog));
    }

    void LOG_does_not_log_on_lower_log_level(void)
    {
        LOG(VB_CHANNEL, LOG_INFO, QString(__FUNCTION__));
        wait_for_log_thread_completion();
        QVERIFY(!console_dbg()->Has(kHandleLog));
    }

    void LOG_logs_to_file(void)
    {
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        usleep(100 * 1000);
        QString s = get_file_contents(logfile);
        QVERIFY(s.contains(QString(__FUNCTION__)));
    }

    void LOG_logs_to_file_in_logpath(void)
    {
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        usleep(100 * 1000);
        QString s = get_logpath_file_contents(logprefix);
        QVERIFY(s.contains(QString(__FUNCTION__)));
    }

    void LOG_PRINT_logs_with_flush(void)
    {
        LOG_PRINT_FLUSH(QString(__FUNCTION__));
        wait_for_log_thread_completion();
        QVERIFY(console_dbg()->Has(kHandlePrint));
        DebugLogHandlerEntry l = console_dbg()->LastEntry(kHandlePrint);
        QVERIFY(l.entry().IsPrint());
        QVERIFY(l.entry().IsFlush());
        QCOMPARE(l.entry().GetMessage(), QString(__FUNCTION__));
    }

    void LOG_PRINT_logs_without_flush(void)
    {
        LOG_PRINT(QString(__FUNCTION__));
        wait_for_log_thread_completion();
        QVERIFY(console_dbg()->Has(kHandlePrint));
        DebugLogHandlerEntry l = console_dbg()->LastEntry(kHandlePrint);
        QVERIFY(l.entry().IsPrint());
        QVERIFY(!l.entry().IsFlush());
        QCOMPARE(l.entry().GetMessage(), QString(__FUNCTION__));
    }

    void log_line_c_accepts_var_args(void)
    {
        log_line_c(VB_CHANNEL, LOG_WARNING, __FILE__, __LINE__,
                   __FUNCTION__, "%5.2f", 55.55555f);
        wait_for_log_thread_completion();
        QVERIFY(console_dbg()->Has(kHandleLog));
        QVERIFY(console_dbg()->LastEntry(kHandleLog).entry().GetMessage()
                .contains("55.56"));
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

    void CommandLineArgumentsSaneWithLogPath(void)
    {
        QString args = command_line_arguments();
        QVERIFY(args.contains(" --logpath"));
        QVERIFY(args.contains(QString(" %1").arg(logpath)));
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
        QString old_name = register_thread("SillyNameRegister");
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        wait_for_log_thread_completion();
        QVERIFY(console_dbg()->Has(kHandleLog));
        DebugLogHandlerEntry l = console_dbg()->LastEntry(kHandleLog);
        QVERIFY(l.entry().toString().contains("SillyNameRegister"));
        register_thread(old_name);
    }

    void test_deregister_thread(void)
    {
        QString old_name = register_thread("SillyNameUnregister");
        QString new_name = deregister_thread();
        LOG(VB_CHANNEL, LOG_WARNING, QString(__FUNCTION__));
        wait_for_log_thread_completion();
        QVERIFY(console_dbg()->Has(kHandleLog));
        DebugLogHandlerEntry l = console_dbg()->LastEntry(kHandleLog);
        QVERIFY(!l.entry().toString().contains("SillyNameRegister"));
        QVERIFY(l.entry().toString().contains("Unknown"));
        register_thread(old_name);
    }

    void benchmark_LOG_macro_10000(void)
    {
        QBENCHMARK
        {
            log_many_times(10000);
        }
    }

    void benchmark_processing_10000(void)
    {
        QBENCHMARK
        {
            log_many_times(10000);
            wait_for_log_thread_completion();
        }
    }
};

#endif // _TEST_MYTH_LOGGING_BASE_

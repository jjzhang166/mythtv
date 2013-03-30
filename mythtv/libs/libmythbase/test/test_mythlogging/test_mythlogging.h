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

#include <iostream>
using namespace std;

#include <QtTest/QtTest>

#include "mythlogging_extra.h"
using namespace myth_logging;

class TestMythLogging : public QObject
{
    Q_OBJECT

  private slots:
    void init(void)
    {
        set_parameters(VB_CHANNEL, LOG_WARNING, -1, false, false);            
        QVERIFY(VB_CHANNEL == get_verbose());
        QVERIFY(LOG_WARNING == get_log_level());
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

    void CommandLineArgumentsSaneWithVerbose(void)
    {
        QString args = command_line_arguments();
        QVERIFY(args.length() > 2);
        QVERIFY(args[0] == QChar(' '));
        QVERIFY(args[args.length() - 1] == QChar(' '));
        QVERIFY(args.contains(" --verbose"));
        QVERIFY(args.contains(" channel "));
    }

    void CommandLineArgumentsSaneWithMultiVerbose(void)
    {
        quint64 old_v_mheg = set_verbose(VB_MHEG|VB_UPNP|VB_DECODE);
        QString args = command_line_arguments();
        QVERIFY(args.length() > 2);
        QVERIFY(args[0] == QChar(' '));
        QVERIFY(args[args.length() - 1] == QChar(' '));
        QVERIFY(args.contains(" --verbose"));
        QVERIFY(args.contains(" mheg,upnp,decode "));
        set_verbose(old_v_mheg);
    }

    void CommandLineArgumentsSaneWithNoneVerbose(void)
    {
        quint64 old_v_none = set_verbose(VB_NONE);
        QString args = command_line_arguments();
        QVERIFY(args.length() > 2);
        QVERIFY(args[0] == QChar(' '));
        QVERIFY(args[args.length() - 1] == QChar(' '));
        QVERIFY(args.contains(" --verbose"));
        QVERIFY(args.contains(" none "));
        set_verbose(old_v_none);
    }

        //QVERIFY(args.contains(" --loglevel"));
        //QVERIFY(args.contains(" warning"));
};

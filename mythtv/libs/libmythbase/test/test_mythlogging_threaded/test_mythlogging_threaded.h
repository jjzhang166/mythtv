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

#include <QCoreApplication>
#include "test_mythloggingbase.h"

class TestMythLoggingThreaded : public TestMythLoggingBase
{
    Q_OBJECT

    TestMythLoggingThreaded() : my_app(argc, argv)
    {
    }

    void set_thresholds(uint new_fast, uint new_slow, uint new_hard)
    {
        old_fast = LogDeque::Get().m_messagesFastThreshold;
        old_slow = LogDeque::Get().m_messagesSlowThreshold;
        old_hard = LogDeque::Get().m_messagesHardThreshold;
        LogDeque::Get().m_messagesFastThreshold = new_fast;
        LogDeque::Get().m_messagesSlowThreshold = new_slow;
        LogDeque::Get().m_messagesHardThreshold = new_hard;
    }

    void reset_thresholds(void)
    {
        LogDeque::Get().m_messagesFastThreshold = old_fast;
        LogDeque::Get().m_messagesSlowThreshold = old_slow;
        LogDeque::Get().m_messagesHardThreshold = old_hard;
    }

  private slots:
    void initTestCase(void)
    {
        TestMythLoggingBase::initTestCase(true /*use_threads*/);
    }

    void test_feedback_when_off(void)
    {
        // make sure log many times is able to add messages more
        // quickly than logging thread can deal with them so the
        // test_feedback_when_on(void) test is actually valid.
        set_thresholds(900, 1000000, 2000000);
        log_many_times(10000);
        QVERIFY(LogDeque::Get().MessageQueueSize() > 2000);
        reset_thresholds();
    }

    void test_feedback_when_on(void)
    {
        // the logger should slow down LOG if we start filling
        // up the log queue beyond the slow threshold, we still
        // give it a 10% margin since it is a soft threshold.
        set_thresholds(900, 1000, 2000000);
        log_many_times(10000);
        QVERIFY(LogDeque::Get().MessageQueueSize() < 1100);
        reset_thresholds();
    }

    void test_hard_threshold(void)
    {
        set_thresholds(1000000, 2000000, 10);
        log_many_times(10000);
        QVERIFY(LogDeque::Get().MessageQueueSize() <= 10);
        reset_thresholds();
    }

  public:
    uint old_fast;
    uint old_slow;
    uint old_hard;
    static int argc;
    static char *argv[3];
    QCoreApplication my_app;
};

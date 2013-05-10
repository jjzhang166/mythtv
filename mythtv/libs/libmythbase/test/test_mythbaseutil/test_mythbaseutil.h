/*
 *  Class TestMythBaseUtil
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

#include <unistd.h> // for usleep

#include <QtGlobal> // for Q_OS macros
#include <QtTest/QtTest>
#include <QThreadPool>

#include "mythbaseutil.h"

class TestHelper : public QRunnable
{
  public:
    TestHelper(int64_t &id) : m_id(id) {}
    void run(void) { m_id = get_gdb_thread_id(); }
    int64_t &m_id;
};

class TestMythBaseUtil : public QObject
{
    Q_OBJECT

  private slots:

    void get_gdb_thread_id_returns_non_zero_integer(void)
    {
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_MAC)
        int64_t id = get_gdb_thread_id();
        QVERIFY(id != 0);
#else
        QSKIP("not implemented on platform");
#endif
    }

    void get_gdb_thread_id_is_thread_specific(void)
    {
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_MAC)
        QThreadPool pool;
        pool.setExpiryTimeout(30);
        const int id_count = 10;
        int64_t ids[id_count];
        for (int i = 0; i < id_count; i++)
        {
            pool.start(new TestHelper(ids[i]));
            pool.waitForDone();
            usleep(50 * 1000);
        }
        for (int i = 1; i < id_count; i++)
            QVERIFY(ids[0] != ids[i]);
#else
        QSKIP("not implemented on platform");
#endif
    }
};

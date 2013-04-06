/*
 *  Class LogHandler
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

#include "logentry.h"
#include "loghandler.h"
#include "debugloghandler.h"

#include <iostream>
using namespace std;

#include <QFile>

class ConsoleLogHandler : public LogHandler
{
  public:
    ConsoleLogHandler() {}

    virtual void HandleLog(const LogEntry &entry)
    {
        cerr << qPrintable(entry.toString()) << endl;
    }

    virtual void HandlePrint(const LogEntry &entry)
    {
        cout << qPrintable(entry.GetMessage());
        if (entry.IsFlush())
            cout << flush;
    }
};

class FileLogHandler : public LogHandler
{
  public:
    FileLogHandler(const QString &file_name) :
        m_file(new QFile(file_name))
    {
        m_file->open(QIODevice::WriteOnly|QIODevice::Append);
    }

    ~FileLogHandler()
    {
        delete m_file;
        m_file = NULL;
    }

    virtual void HandleLog(const LogEntry &entry)
    {
        if (m_file->isOpen())
        {
            QByteArray ba = entry.toString().toUtf8();
            ba += '\n';
            m_file->write(ba);
        }
    }

    virtual void RotateLogs(void)
    {
        m_file->close();
        m_file->open(QIODevice::WriteOnly|QIODevice::Append);
    }

    virtual void Flush(void)
    {
        if (m_file->isOpen())
            m_file->flush();
    }

  protected:
    QFile *m_file;
};

class PathLogHandler : public FileLogHandler
{
  public:
    PathLogHandler(const QString &prefix) :
        FileLogHandler(prefix + GetPostfix()), m_prefix(prefix)
    {
    }

    virtual void RotateLogs(void)
    {
        delete m_file;
        m_file = new QFile(m_prefix + GetPostfix());
        m_file->open(QIODevice::WriteOnly|QIODevice::Append);
    }

  private:
    static QString GetPostfix(void)
    {
        return QString("%1.%2.log")
            .arg("date+time") // TODO
            .arg("pid"); // TODO
    }

  private:
    QString m_prefix;
};

LogHandler *LogHandler::GetConsoleHandler(void)
{
    return (DebugLogHandler::IsReplacing("ConsoleLogHandler")) ?
        static_cast<LogHandler*>(new DebugLogHandler("ConsoleLogHandler")) :
        static_cast<LogHandler*>(new ConsoleLogHandler());
}

LogHandler *LogHandler::GetFileHandler(const QString &file_name)
{
    return (DebugLogHandler::IsReplacing("FileLogHandler")) ?
        static_cast<LogHandler*>(
            new DebugLogHandler("FileLogHandler", file_name)) :
        static_cast<LogHandler*>(new FileLogHandler(file_name));
}

LogHandler *LogHandler::GetPathHandler(const QString &prefix)
{
    return (DebugLogHandler::IsReplacing("PathLogHandler")) ?
        static_cast<LogHandler*>(
            new DebugLogHandler("PathLogHandler", prefix)) :
        static_cast<LogHandler*>(new PathLogHandler(prefix));
}

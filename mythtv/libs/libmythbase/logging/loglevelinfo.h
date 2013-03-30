/*
 *  Class LogLevelInfo
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

#ifndef _LOG_LEVEL_INFO_H_
#define _LOG_LEVEL_INFO_H_

#include <stdint.h>

#include <QString>
#include <QChar>

class LogLevelInfo
{
  public:
    LogLevelInfo() : m_value(0), m_name(QString::null)
    {
    }

    LogLevelInfo(int value, const QString &name, QChar charName) :
        m_value(value), m_name(name), m_charName(charName)
    {
    }

    bool IsValid(void) const { return !m_name.isNull(); }

    int GetLevel(void) const { return m_value; }
    QString GetName(void) const { return m_name; }
    QChar GetChar(void) const { return m_charName; }

    QString toString() const
    {
        return QString("level(%1) char_name(%2) name(%3)")
            .arg(m_value).arg(m_charName).arg(m_name);
    }

  private:
    int m_value;
    QString m_name;
    QChar m_charName;
};

#endif // _LOG_LEVEL_INFO_H_

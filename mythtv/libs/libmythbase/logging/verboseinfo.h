/*
 *  Class VerboseInfo
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

#ifndef _VERBOSE_INFO_H_
#define _VERBOSE_INFO_H_

#include <stdint.h>

#include <QDateTime>

class VerboseInfo
{
  public:
    VerboseInfo() : 
        m_mask(0), m_name(QString::null), m_additive(false), m_help()
    {
    }

    VerboseInfo(uint64_t mask, const QString &name,
                bool additive, const QString &help) :
        m_mask(mask), m_name(name), m_additive(additive), m_help(help)
    {
    }

    bool IsValid(void) const { return !m_name.isNull(); }

    uint64_t GetMask(void) const { return m_mask; }
    bool IsAdditive(void) const { return m_additive; }
    QString GetHelp(void) const { return m_help; }

    QString toString() const
    {
        return QString("mask(0x%1) name(%2) additive(%3) help(%4)")
            .arg(m_mask, 16, 16, QChar('0'))
            .arg(m_name).arg(m_additive).arg(m_help);
    }

  private:
    uint64_t m_mask;
    QString m_name;
    bool m_additive;
    QString m_help;
};

#endif // _VERBOSE_INFO_H_

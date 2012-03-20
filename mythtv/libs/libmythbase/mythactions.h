#ifndef MYTHACTIONS_H_
#define MYTHACTIONS_H_

#include <QStringList>
#include <QString>
#include <QHash>
#include <QList>
#include <QRegExp>
#include "mythbaseexp.h"  //  MBASE_PUBLIC , etc.
#include "mythlogging.h"

template <class T>
struct ActionDefStruct
{
    QString     name;
    bool        (T::*handler)(const QString &action);
};

template <class T>
struct ActionStruct
{
    ActionDefStruct<T>  def;
    QRegExp             regex;
};

#define NELEMS(x) ((sizeof(x)) / (sizeof(x[0])))
#define CALL_CLASS_MEMBER(pObj,pMember)  ((pObj)->*(pMember))

template <class T>
class ActionMap : public QHash<QString, struct ActionStruct<T> *>
{
  public:
    ActionMap()  {};
    ~ActionMap() {};
};

template <class T>
class MBASE_PUBLIC MythActions : public ActionMap<T>
{
  public:
    MythActions(T *parent, struct ActionDefStruct<T> *defs, int count,
                bool abbrev = false) :
        m_parent(parent), m_regex(false)
    {
        LOG(VB_KEYPRESS, LOG_DEBUG, "Creating hash");
        static QRegExp special("[\\^\\$\\[\\]]");

        int size = count;

        if (abbrev)
        {
            // This will likely be larger than needed (if containing a regex)
            size = 0;
            for (int i = 0; i < count; i++)
                size += defs[i].name.size();
        }

        m_actions = new struct ActionStruct<T>[size];
        for (int i = 0, j = 0; i < count; i++)
        {
            QString name = defs[i].name;
            if (name.contains(special))
            {
                LOG(VB_KEYPRESS, LOG_DEBUG, "Regexp: " + name);
                m_actions[j].def.name    = defs[i].name;
                m_actions[j].def.handler = defs[i].handler;
                m_actions[j].regex       = QRegExp(name);
                m_regexList.append(&m_actions[j]);
                m_regex = true;
                j++;
            }
            else
            {
                do {
                    LOG(VB_KEYPRESS, LOG_DEBUG, "Mapping " + name);
                    m_actions[j].def.name    = name;
                    m_actions[j].def.handler = defs[i].handler;
                    insertMulti(name, &m_actions[j]);
                    j++;
                    name.chop(1);
                } while (abbrev && !name.isEmpty());
            }
        }

        if (abbrev)
        {
            // prune the hash to just unique mappings
            LOG(VB_KEYPRESS, LOG_DEBUG, "Pruning abbreviation hash");
            QStringList keylist = this->uniqueKeys();

            for(QStringList::iterator it = keylist.begin(); it != keylist.end();
                ++it)
            {
                if (this->count(*it) > 1)
                {
                    LOG(VB_KEYPRESS, LOG_DEBUG, "Removing " + *it);
                    this->remove(*it);
                }
            }
        }
        LOG(VB_KEYPRESS, LOG_DEBUG, "Finished creating hash");
    }

    ~MythActions()
    {
        if (m_actions)
            delete [] m_actions;
    }

    bool handleActions(const QStringList &actions, bool *touched = NULL)
    {
        bool handled = false;

        if (touched)
            *touched = false;

        for (int i = 0; i < actions.size() && !handled; i++)
        {
            bool itemTouched;
            const QString &action = actions[i];
            handled = handleAction(action, itemTouched);

            if (touched)
                *touched |= itemTouched;
        }

        return handled;
    }

    bool handleAction(const QString &action, bool &touched)
    {
        struct ActionStruct<T> *def = this->value(action, NULL);

        touched = false;

        if (!def && m_regex)
        {
            LOG(VB_KEYPRESS, LOG_INFO, "Checking Regex");
            typename QList<struct ActionStruct<T> *>::iterator it;
            for (it = m_regexList.begin(); it != m_regexList.end(); ++it)
            {
                struct ActionStruct<T> *regex = *it;

                if (regex->regex.indexIn(action) != -1)
                {
                    def = regex;
                    break;
                }
            }
        }

        if (!def || !def->def.handler)
            return false;

        touched = true;

        LOG(VB_KEYPRESS, LOG_INFO, "Dispatching");
        return CALL_CLASS_MEMBER(m_parent, def->def.handler)(action);
    }

  private:
    T *m_parent;
    struct ActionStruct<T> *m_actions;
    QList<struct ActionStruct<T> *> m_regexList;
    bool m_regex;
};

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

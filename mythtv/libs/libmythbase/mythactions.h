#ifndef MYTHACTIONS_H_
#define MYTHACTIONS_H_

#include <QStringList>
#include <QString>
#include <QHash>
#include <QList>
#include <QRegExp>
#include "mythbaseexp.h"  //  MBASE_PUBLIC , etc.

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
    MythActions(T *parent, struct ActionDefStruct<T> *defs, int count) :
        m_parent(parent), m_regex(false)
    {
        static QRegExp special("[^$*]");

        m_actions = new struct ActionStruct<T>[count];
        for (int i = 0; i < count; i++)
        {
            m_actions[i].def.name    = defs[i].name;
            m_actions[i].def.handler = defs[i].handler;

            QString name = m_actions[i].def.name;

            if (name.contains(special))
            {
                m_actions[i].regex = QRegExp(name);
                m_regexList.append(&m_actions[i]);
                m_regex = true;
            }
            insert(name, &m_actions[i]);
        }
    }

    ~MythActions()
    {
        if (m_actions)
            delete m_actions;
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

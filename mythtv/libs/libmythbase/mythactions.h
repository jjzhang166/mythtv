#ifndef MYTHACTIONS_H_
#define MYTHACTIONS_H_

#include <QStringList>
#include <QString>
#include <QHash>
#include "mythbaseexp.h"  //  MBASE_PUBLIC , etc.

template <class T>
struct ActionDefStruct
{
    QString     name;
    bool        (T::*handler)(void);
};

#define NELEMS(x) ((sizeof(x)) / (sizeof(x[0])))
#define CALL_CLASS_MEMBER(pObj,pMember)  ((pObj)->*(pMember))

template <class T>
class ActionMap : public QHash<QString, struct ActionDefStruct<T> *>
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
        m_parent(parent)
    {
        for (int i = 0; i < count; i++)
            insert(defs[i].name, &defs[i]);
    }

    ~MythActions() {}

    bool handleActions(const QStringList &actions)
    {
        bool handled = false;
        for (int i = 0; i < actions.size() && !handled; i++)
        {
            const QString &action = actions[i];
            handled = handleAction(action);
        }

        return handled;
    }

    bool handleAction(const QString &action)
    {
        struct ActionDefStruct<T> *def = this->value(action, NULL);

        if (!def || !def->handler)
            return false;

        return CALL_CLASS_MEMBER(m_parent,def->handler)();
    }

  private:
    T *m_parent;
};

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

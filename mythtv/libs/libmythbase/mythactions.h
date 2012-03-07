#ifndef MYTHACTIONS_H_
#define MYTHACTIONS_H_

#include <QStringList>
#include <QString>
#include <QHash>
#include "mythbaseexp.h"  //  MBASE_PUBLIC , etc.

typedef bool (*ActionHandler)(void *args);

typedef struct
{
    QString         name;
    ActionHandler   handler;
} ActionDef;

#define NELEMS(x) ((sizeof(x)) / (sizeof(x[0])))

class MBASE_PUBLIC MythActions : public QHash<QString, ActionHandler>
{
  public:
    MythActions(ActionDef *defs, int count);
    ~MythActions();

    bool handleActions(const QStringList &actions, void *args);
    bool handleAction(const QString &action, void *args);
};

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

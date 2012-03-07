#include <QStringList>
#include <QString>
#include <QHash>

using namespace std;

#include "mythactions.h"

MythActions::MythActions(ActionDef *defs, int count)
{
    for (int i = 0; i < count; i++)
        insert(defs[i].name, defs[i].handler);
}

MythActions::~MythActions()
{
}

bool MythActions::handleActions(const QStringList &actions, void *args)
{
    bool handled = false;
    for (int i = 0; i < actions.size() && !handled; i++)
    {
        const QString &action = actions[i];
        handled = handleAction(action, args);
    }

    return handled;
}

bool MythActions::handleAction(const QString &action, void *args)
{
    ActionHandler handler = value(action, NULL);

    if (!handler)
        return false;

    return handler(args);
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

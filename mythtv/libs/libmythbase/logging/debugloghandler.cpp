#include "debugloghandler.h"

QSet<QString> DebugLogHandler::s_is_replacing;
QHash<QString, DebugLogHandler*> DebugLogHandler::s_replacement;

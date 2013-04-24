#include <QCoreApplication>

#include "logeventhandler.h"
#include "logdeque.h"

#include <iostream>
using namespace std;

bool LogEventHandler::event(QEvent *e)
{
    // TODO -- implement properly
    //cerr << qPrintable(QTime::currentTime().toString("mm:ss.zzz"))
    //     << " start event" << endl;
    m_logDeque.ProcessQueue();
    //cerr << qPrintable(QTime::currentTime().toString("mm:ss.zzz"))
    //     << " done event" << endl;
    return true;
}

void LogEventHandler::Notify(void)
{
    // TODO -- implement
    //cerr << qPrintable(QTime::currentTime().toString("mm:ss.zzz"))
    //     << " in notify" << endl;
    QCoreApplication::postEvent(this, new QEvent((QEvent::Type)9999));
}

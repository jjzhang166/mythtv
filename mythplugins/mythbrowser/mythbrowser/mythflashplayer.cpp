#include <stdlib.h>
#include <iostream>

// qt
#include <QApplication>
#include <QEvent>

// myth
#include <mythlogging.h>
#include <mythcontext.h>
#include <libmythui/mythmainwindow.h>
#include <mythuiwebbrowser.h>
#include <playgroup.h>

// mythbrowser
#include "webpage.h"
#include "mythflashplayer.h"
#include "mythactions.h"

using namespace std;

MythFlashPlayer::MythFlashPlayer(MythScreenStack *parent,
                                 QStringList &urlList) :
    MythScreenType (parent, "mythflashplayer"),
    m_browser(NULL), m_url(urlList[0]), m_actions(NULL)
{
    m_fftime       = PlayGroup::GetSetting("Default", "skipahead", 30);
    m_rewtime      = PlayGroup::GetSetting("Default", "skipback", 5);
    m_jumptime     = PlayGroup::GetSetting("Default", "jump", 10);
    qApp->setOverrideCursor(QCursor(Qt::BlankCursor));
}


MythFlashPlayer::~MythFlashPlayer()
{
    qApp->restoreOverrideCursor();

    if (m_browser)
    {
        m_browser->disconnect();
        DeleteChild(m_browser);
        m_browser = NULL;
    }

    if (m_actions)
        delete m_actions;
}

bool MythFlashPlayer::Create(void)
{
    m_browser = new MythUIWebBrowser(this, "mythflashplayer");
    m_browser->SetArea(GetMythMainWindow()->GetUIScreenRect());
    m_browser->Init();
    m_browser->SetActive(true);
    m_browser->Show();

    BuildFocusList();

    SetFocusWidget(m_browser);

    m_url.replace("mythflash://", "http://");
    LOG(VB_GENERAL, LOG_INFO, QString("Opening %1").arg(m_url));
    m_browser->LoadPage(QUrl::fromEncoded(m_url.toLocal8Bit()));

    return true;
}

QVariant MythFlashPlayer::evaluateJavaScript(const QString& source)
{
    return m_browser->evaluateJavaScript(source);
}

static struct ActionDefStruct<MythFlashPlayer> mfpActions[] = {
    { "PAUSE",       &MythFlashPlayer::doPause },
    { "INFO",        &MythFlashPlayer::doInfo },
    { "SEEKFFWD",    &MythFlashPlayer::doSeekFfwd },
    { "SEEKRWND",    &MythFlashPlayer::doSeekRwnd },
    { "CHANNELUP",   &MythFlashPlayer::doChanUp },
    { "CHANNELDOWN", &MythFlashPlayer::doChanDown },
    { "VOLUMEUP",    &MythFlashPlayer::doVolumeUp },
    { "VOLUMEDOWN",  &MythFlashPlayer::doVolumeDown }
};
static int mfpActionCount = NELEMS(mfpActions);

bool MythFlashPlayer::doPause(const QString &action)
{
    (void)action;
    evaluateJavaScript("play();");
    return true;
}

bool MythFlashPlayer::doInfo(const QString &action)
{
    (void)action;
    evaluateJavaScript("info();");
    return true;
}

bool MythFlashPlayer::doSeekFfwd(const QString &action)
{
    (void)action;
    evaluateJavaScript(QString("seek(%1);").arg(m_fftime));
    return true;
}

bool MythFlashPlayer::doSeekRwnd(const QString &action)
{
    (void)action;
    evaluateJavaScript(QString("seek(-%1);").arg(m_rewtime));
    return true;
}

bool MythFlashPlayer::doChanUp(const QString &action)
{
    (void)action;
    evaluateJavaScript(QString("seek(%1);").arg(m_jumptime * 60));
    return true;
}

bool MythFlashPlayer::doChanDown(const QString &action)
{
    (void)action;
    evaluateJavaScript(QString("seek(-%1);").arg(m_jumptime * 60));
    return true;
}

bool MythFlashPlayer::doVolumeUp(const QString &action)
{
    (void)action;
    evaluateJavaScript("adjustVolume(2);");
    return true;
}

bool MythFlashPlayer::doVolumeDown(const QString &action)
{
    (void)action;
    evaluateJavaScript("adjustVolume(-2);");
    return true;
}


bool MythFlashPlayer::keyPressEvent(QKeyEvent *event)
{
    QStringList actions;
    bool handled = GetMythMainWindow()->TranslateKeyPress("TV Playback", event,
                                                          actions);

    if (!handled)
    {
        if (!m_actions)
            m_actions = new MythActions<MythFlashPlayer>(this, mfpActions,
                                                         mfpActionCount);
        handled = m_actions->handleActions(actions);
    }

    if (!handled)
        handled = m_browser->keyPressEvent(event);

    if (!handled && MythScreenType::keyPressEvent(event))
        handled = true;

    return handled;
}

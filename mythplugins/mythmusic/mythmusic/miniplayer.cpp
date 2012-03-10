// mythtv
#include <mythcontext.h>
#include <lcddevice.h>

// mythmusic
#include "miniplayer.h"
#include "musicplayer.h"
#include "decoder.h"

MiniPlayer::MiniPlayer(MythScreenStack *parent) :
    MusicCommon(parent, "music_miniplayer"), m_actions(NULL)
{
    m_displayTimer = new QTimer(this);
    m_displayTimer->setSingleShot(true);
    connect(m_displayTimer, SIGNAL(timeout()), this, SLOT(timerTimeout()));
}

MiniPlayer::~MiniPlayer(void)
{
    gPlayer->removeListener(this);

    // Timers are deleted by Qt
    m_displayTimer->disconnect();
    m_displayTimer = NULL;

    if (LCD *lcd = LCD::Get())
        lcd->switchToTime ();

    if (m_actions)
        delete m_actions;
}

void MiniPlayer::timerTimeout(void)
{
    Close();
}

bool MiniPlayer::Create(void)
{
    bool err = false;

    // Load the theme for this screen
    err = LoadWindowFromXML("music-ui.xml", "miniplayer", this);

    if (!err)
        return false;

    // find common widgets available on any view
    err = CreateCommon();

    if (err)
    {
        LOG(VB_GENERAL, LOG_ERR, "Cannot load screen 'miniplayer'");
        return false;
    }

    m_displayTimer->start(10000);

    BuildFocusList();

    return true;
}

static struct ActionDefStruct<MiniPlayer> mpActions[] = {
    { "SELECT", &MiniPlayer::doSelect },
    { "ESCAPE", &MiniPlayer::doEscape },
    { "MENU",   &MiniPlayer::doMenu  }
};
static int mpActionCount = NELEMS(mpActions);

bool MiniPlayer::doSelect(const QString &action)
{
    (void)action;
    if (m_displayTimer)
        m_displayTimer->stop();
    return true;
}

bool MiniPlayer::doEscape(const QString &action)
{
    (void)action;
    Close();
    return true;
}

bool MiniPlayer::doMenu(const QString &action)
{
    (void)action;
    gPlayer->autoShowPlayer(!gPlayer->getAutoShowPlayer());
    //showAutoMode();
    return true;
}


bool MiniPlayer::keyPressEvent(QKeyEvent *event)
{
    // restart the display timer on any keypress if it is active
    if (m_displayTimer && m_displayTimer->isActive())
        m_displayTimer->start();

    if (GetFocusWidget() && GetFocusWidget()->keyPressEvent(event))
        return true;

    bool handled = false;
    QStringList actions;
    handled = GetMythMainWindow()->TranslateKeyPress("Music", event, actions);

    if (!handled)
    {
        if (!m_actions)
            m_actions = new MythActions<MiniPlayer>(this, mpActions,
                                                    mpActionCount);
        handled = m_actions->handleActions(actions);
    }

    if (!handled && MusicCommon::keyPressEvent(event))
        handled = true;

    if (!handled && MythScreenType::keyPressEvent(event))
        handled = true;

    return handled;
}

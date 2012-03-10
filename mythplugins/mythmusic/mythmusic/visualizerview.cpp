#include <iostream>
#include <cstdlib>

// qt
#include <QKeyEvent>
#include <QTimer>

// myth
#include <mythcontext.h>
#include <mythdbcon.h>
#include <mythmainwindow.h>
#include <mythuibuttonlist.h>
#include <mythuiwebbrowser.h>
#include <mythuitext.h>
#include <mythuiutils.h>
#include <mythdialogbox.h>

// mythmusic
#include "musiccommon.h"
#include "visualizerview.h"
#include "mythactions.h"

VisualizerView::VisualizerView(MythScreenStack *parent) :
    MusicCommon(parent, "visualizerview"), m_actions(NULL)
{
    m_currentView = MV_VISUALIZER;
}

VisualizerView::~VisualizerView()
{
    if (m_actions)
        delete m_actions;
}

bool VisualizerView::Create(void)
{
    bool err = false;

    // Load the theme for this screen
    err = LoadWindowFromXML("music-ui.xml", "visualizerview", this);

    if (!err)
        return false;

    // find common widgets available on any view
    err = CreateCommon();

    // find widgets specific to this view

    if (err)
    {
        LOG(VB_GENERAL, LOG_ERR, "Cannot load screen 'lyricsview'");
        return false;
    }

    BuildFocusList();

    return true;
}

void VisualizerView::customEvent(QEvent *event)
{
    if (event->type() == MusicPlayerEvent::TrackChangeEvent)
        showTrackInfoPopup();

    MusicCommon::customEvent(event);
}

static struct ActionDefStruct<VisualizerView> vvActions[] = {
    { "INFO",   &VisualizerView::doInfo }
};
static int vvActionCount = NELEMS(vvActions);

bool VisualizerView::doInfo(const QString &action)
{
    (void)action;
    showTrackInfoPopup();
    return true;
}

bool VisualizerView::keyPressEvent(QKeyEvent *event)
{
    if (GetFocusWidget() && GetFocusWidget()->keyPressEvent(event))
        return true;

    bool handled = false;
    QStringList actions;
    handled = GetMythMainWindow()->TranslateKeyPress("Music", event, actions);

    if (!handled)
    {
        if (!m_actions)
            m_actions = new MythActions<VisualizerView>(this, vvActions,
                                                        vvActionCount);
        handled = m_actions->handleActions(actions);
    }

    if (!handled && MusicCommon::keyPressEvent(event))
        handled = true;

    if (!handled && MythScreenType::keyPressEvent(event))
        handled = true;

    return handled;
}

void VisualizerView::ShowMenu(void)
{
    QString label = tr("Actions");

    MythMenu *menu = new MythMenu(label, this, "menu");

    menu->AddItem(tr("Change Visualizer"), NULL, createVisualizerMenu());
    menu->AddItem(tr("Show Track Info"), SLOT(showTrackInfoPopup()));
    menu->AddItem(tr("Other Options"), NULL, createMainMenu());

    MythScreenStack *popupStack = GetMythMainWindow()->GetStack("popup stack");

    MythDialogBox *menuPopup = new MythDialogBox(menu, popupStack, "actionmenu");

    if (menuPopup->Create())
        popupStack->AddScreen(menuPopup);
    else
        delete menuPopup;
}

void VisualizerView::showTrackInfoPopup(void)
{
    MythScreenStack *popupStack = GetMythMainWindow()->GetStack("popup stack");

    TrackInfoPopup *popup = new TrackInfoPopup(popupStack, gPlayer->getCurrentMetadata());

    if (!popup->Create())
    {
        delete popup;
        return;
    }

    popupStack->AddScreen(popup);
}

//---------------------------------------------------------
// TrackInfoPopup
//---------------------------------------------------------
#define MUSICINFOPOPUPTIME 8 * 1000

TrackInfoPopup::TrackInfoPopup(MythScreenStack *parent, Metadata *metadata) :
    MythScreenType(parent, "trackinfopopup", false), m_actions(NULL)
{
    m_metadata = metadata;
    m_displayTimer = NULL;
}

TrackInfoPopup::~TrackInfoPopup(void)
{
    if (m_displayTimer)
    {
        m_displayTimer->stop();
        delete m_displayTimer;
        m_displayTimer = NULL;
    }

    if (m_actions)
        delete m_actions;
}

bool TrackInfoPopup::Create(void)
{
    bool err = false;

    err = LoadWindowFromXML("music-ui.xml", "trackinfo_popup", this);

    if (!err)
        return false;

    // get map for current track
    MetadataMap metadataMap; 
    m_metadata->toMap(metadataMap); 

    // add the map from the next track
    Metadata *nextMetadata = gPlayer->getNextMetadata();
    if (nextMetadata)
        nextMetadata->toMap(metadataMap, "next");

    SetTextFromMap(metadataMap);

    MythUIStateType *ratingState = dynamic_cast<MythUIStateType *>(GetChild("ratingstate"));
    if (ratingState)
        ratingState->DisplayState(QString("%1").arg(m_metadata->Rating()));

    MythUIImage *albumImage = dynamic_cast<MythUIImage *>(GetChild("coverart"));
    if (albumImage)
    {
        if (!m_metadata->getAlbumArtFile().isEmpty())
        {
            albumImage->SetFilename(m_metadata->getAlbumArtFile());
            albumImage->Load();
        }
    }

    m_displayTimer = new QTimer(this);
    connect(m_displayTimer, SIGNAL(timeout()), this, SLOT(Close()));
    m_displayTimer->setSingleShot(true);
    m_displayTimer->start(MUSICINFOPOPUPTIME);

    return true;
}

static struct ActionDefStruct<TrackInfoPopup> tipActions[] = {
    { "INFO",   &TrackInfoPopup::doInfo }
};
static int tipActionCount = NELEMS(tipActions);

bool TrackInfoPopup::doInfo(const QString &action)
{
    (void)action;
    Close();
    return true;
}

bool TrackInfoPopup::keyPressEvent(QKeyEvent *event)
{
    QStringList actions;
    bool handled = GetMythMainWindow()->TranslateKeyPress("Music", event,
                                                          actions, false);

    if (!handled)
    {
        if (!m_actions)
            m_actions = new MythActions<TrackInfoPopup>(this, tipActions,
                                                        tipActionCount);
        handled = m_actions->handleActions(actions);
    }

    if (!handled && MythScreenType::keyPressEvent(event))
        handled = true;

    return handled;
}

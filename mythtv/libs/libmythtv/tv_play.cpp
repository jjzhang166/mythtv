#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <stdint.h>

#include <algorithm>
using namespace std;

#include <QCoreApplication>
#include <QKeyEvent>
#include <QRunnable>
#include <QRegExp>
#include <QTimer>
#include <QEvent>
#include <QFile>
#include <QDir>

#include "mythdb.h"
#include "tv_play.h"
#include "tv_rec.h"
#include "mythcorecontext.h"
#include "remoteencoder.h"
#include "remoteutil.h"
#include "tvremoteutil.h"
#include "mythplayer.h"
#include "DetectLetterbox.h"
#include "programinfo.h"
#include "vsync.h"
#include "lcddevice.h"
#include "jobqueue.h"
#include "audiooutput.h"
#include "DisplayRes.h"
#include "signalmonitorvalue.h"
#include "scheduledrecording.h"
#include "previewgenerator.h"
#include "mythconfig.h"
#include "livetvchain.h"
#include "playgroup.h"
#include "dvdringbuffer.h"
#include "bdringbuffer.h"
#include "datadirect.h"
#include "sourceutil.h"
#include "cardutil.h"
#include "channelutil.h"
#include "compat.h"
#include "mythuihelper.h"
#include "mythdialogbox.h"
#include "mythmainwindow.h"
#include "mythscreenstack.h"
#include "mythscreentype.h"
#include "tv_play_win.h"
#include "recordinginfo.h"
#include "mythsystemevent.h"
#include "videometadatautil.h"
#include "mythdirs.h"
#include "tvbrowsehelper.h"
#include "mythlogging.h"
#include "mythuistatetracker.h"
#include "mythactions.h"

#if ! HAVE_ROUND
#define round(x) ((int) ((x) + 0.5))
#endif

#define DEBUG_CHANNEL_PREFIX 0 /**< set to 1 to debug channel prefixing */
#define DEBUG_ACTIONS        0 /**< set to 1 to debug actions           */

#define LOC      QString("TV: ")

#define GetPlayer(X,Y) GetPlayerHaveLock(X, Y, __FILE__ , __LINE__)
#define GetOSDLock(X) GetOSDL(X, __FILE__, __LINE__)

#define SetOSDText(CTX, GROUP, FIELD, TEXT, TIMEOUT) { \
    OSD *osd = GetOSDLock(CTX); \
    if (osd) \
    { \
        QHash<QString,QString> map; \
        map.insert(FIELD,TEXT); \
        osd->SetText(GROUP, map, TIMEOUT); \
    } \
    ReturnOSDLock(CTX, osd); }

#define SetOSDMessage(CTX, MESSAGE) \
    SetOSDText(CTX, "osd_message", "message_text", MESSAGE, kOSDTimeout_Med)

#define HideOSDWindow(CTX, WINDOW) { \
    OSD *osd = GetOSDLock(CTX); \
    if (osd) \
        osd->HideWindow(WINDOW); \
    ReturnOSDLock(CTX, osd); }

const int  TV::kInitFFRWSpeed                = 0;
const uint TV::kInputKeysMax                 = 6;
const uint TV::kNextSource                   = 1;
const uint TV::kPreviousSource               = 2;
const uint TV::kMaxPIPCount                  = 4;
const uint TV::kMaxPBPCount                  = 2;


const uint TV::kInputModeTimeout             = 5000;
const uint TV::kLCDTimeout                   = 1000;
const uint TV::kBrowseTimeout                = 30000;
const uint TV::kKeyRepeatTimeout             = 300;
const uint TV::kPrevChanTimeout              = 750;
const uint TV::kSleepTimerDialogTimeout      = 45000;
const uint TV::kIdleTimerDialogTimeout       = 45000;
const uint TV::kVideoExitDialogTimeout       = 120000;

const uint TV::kEndOfPlaybackCheckFrequency  = 250;
const uint TV::kEndOfRecPromptCheckFrequency = 250;
const uint TV::kEmbedCheckFrequency          = 250;
const uint TV::kSpeedChangeCheckFrequency    = 250;
const uint TV::kErrorRecoveryCheckFrequency  = 250;
#ifdef USING_VALGRIND
const uint TV::kEndOfPlaybackFirstCheckTimer = 60000;
#else
const uint TV::kEndOfPlaybackFirstCheckTimer = 5000;
#endif

/**
 * \brief stores last program info. maintains info so long as
 * mythfrontend is active
 */
QStringList TV::lastProgramStringList = QStringList();

/**
 * \brief function pointer for RunPlaybackBox in playbackbox.cpp
 */
EMBEDRETURNVOID TV::RunPlaybackBoxPtr = NULL;

/**
 * \brief function pointer for RunViewScheduled in viewscheduled.cpp
 */
EMBEDRETURNVOID TV::RunViewScheduledPtr = NULL;

/**
 * \brief function pointer for RunScheduleEditor in scheduleeditor.cpp
 */
EMBEDRETURNVOIDSCHEDIT TV::RunScheduleEditorPtr = NULL;

/**
 * \brief function pointer for RunProgramGuide in guidegrid.cpp
 */
EMBEDRETURNVOIDEPG TV::RunProgramGuidePtr = NULL;

/**
 * \brief function pointer for RunProgramFinder in progfind.cpp
 */
EMBEDRETURNVOIDFINDER TV::RunProgramFinderPtr = NULL;

/// Helper class to load channel info for channel editor
class DDLoader : public QRunnable
{
  public:
    DDLoader(TV *parent) : m_parent(parent), m_sourceid(0)
    {
        setAutoDelete(false);
    }

    void SetParent(TV *parent) { m_parent = parent; }
    void SetSourceID(uint sourceid) { m_sourceid = sourceid; }

    virtual void run(void)
    {
        if (m_parent)
            m_parent->RunLoadDDMap(m_sourceid);
        else
            SourceUtil::UpdateChannelsFromListings(m_sourceid);

        QMutexLocker locker(&m_lock);
        m_sourceid = 0;
        m_wait.wakeAll();
    }

    void wait(void)
    {
        QMutexLocker locker(&m_lock);
        while (m_sourceid)
            m_wait.wait(locker.mutex());
    }

  private:
    TV *m_parent;
    uint m_sourceid;
    QMutex m_lock;
    QWaitCondition m_wait;
};

/**
 * \brief If any cards are configured, return the number.
 */
int TV::ConfiguredTunerCards(void)
{
    int count = 0;

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT COUNT(cardid) FROM capturecard;");
    if (query.exec() && query.isActive() && query.size() && query.next())
        count = query.value(0).toInt();

    LOG(VB_RECORD, LOG_INFO,
        "ConfiguredTunerCards() = " + QString::number(count));

    return count;
}

static void multi_lock(QMutex *mutex0, ...)
{
    vector<QMutex*> mutex;
    mutex.push_back(mutex0);

    va_list argp;
    va_start(argp, mutex0);
    QMutex *cur = va_arg(argp, QMutex*);
    while (cur)
    {
        mutex.push_back(cur);
        cur = va_arg(argp, QMutex*);
    }
    va_end(argp);

    for (bool success = false; !success;)
    {
        success = true;
        for (uint i = 0; success && (i < mutex.size()); i++)
        {
            if (!(success = mutex[i]->tryLock()))
            {
                for (uint j = 0; j < i; j++)
                    mutex[j]->unlock();
                usleep(25 * 1000);
            }
        }
    }
}

QMutex* TV::gTVLock = new QMutex();
TV*     TV::gTV     = NULL;

bool TV::IsTVRunning(void)
{
    QMutexLocker locker(gTVLock);
    return gTV;
}

TV* TV::GetTV(void)
{
    QMutexLocker locker(gTVLock);
    if (gTV)
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC + "Already have a TV object.");
        return NULL;
    }
    gTV = new TV();
    return gTV;
}

void TV::ReleaseTV(TV* tv)
{
    QMutexLocker locker(gTVLock);
    if (!tv || !gTV || (gTV != tv))
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "ReleaseTV - programmer error.");
        return;
    }

    delete gTV;
    gTV = NULL;
}

/**
 * \brief returns true if the recording completed when exiting.
 */
bool TV::StartTV(ProgramInfo *tvrec, uint flags)
{
    TV *tv = GetTV();
    if (!tv)
        return false;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "StartTV() -- begin");
    bool startInGuide = flags & kStartTVInGuide;
    bool inPlaylist = flags & kStartTVInPlayList;
    bool initByNetworkCommand = flags & kStartTVByNetworkCommand;
    bool quitAll = false;
    bool showDialogs = true;
    bool playCompleted = false;
    ProgramInfo *curProgram = NULL;
    bool startSysEventSent = false;


    if (tvrec)
    {
        curProgram = new ProgramInfo(*tvrec);
        curProgram->SetIgnoreBookmark(flags & kStartTVIgnoreBookmark);
    }

    // Must be before Init() otherwise we swallow the PLAYBACK_START event
    // with the event filter
    sendPlaybackStart();

    // Initialize TV
    if (!tv->Init())
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed initializing TV");
        ReleaseTV(tv);
        sendPlaybackEnd();
        delete curProgram;
        return false;
    }

    if (!lastProgramStringList.empty())
    {
        ProgramInfo pginfo(lastProgramStringList);
        if (pginfo.HasPathname() || pginfo.GetChanID())
            tv->SetLastProgram(&pginfo);
    }

    if (curProgram)
    {
        startSysEventSent = true;
        SendMythSystemPlayEvent("PLAY_STARTED", curProgram);
    }

    QString playerError = QString::null;
    while (!quitAll)
    {
        if (curProgram)
        {
            LOG(VB_PLAYBACK, LOG_INFO, LOC + "tv->Playback() -- begin");
            if (!tv->Playback(*curProgram))
            {
                quitAll = true;
            }
            else if (!startSysEventSent)
            {
                startSysEventSent = true;
                SendMythSystemPlayEvent("PLAY_STARTED", curProgram);
            }

            LOG(VB_PLAYBACK, LOG_INFO, LOC + "tv->Playback() -- end");
        }
        else if (RemoteGetFreeRecorderCount())
        {
            LOG(VB_PLAYBACK, LOG_INFO, LOC + "tv->LiveTV() -- begin");
            if (!tv->LiveTV(showDialogs))
            {
                tv->SetExitPlayer(true, true);
                quitAll = true;
            }
            else if (!startSysEventSent)
            {
                startSysEventSent = true;
                gCoreContext->SendSystemEvent("LIVETV_STARTED");
            }

            if (!quitAll && (startInGuide || tv->StartLiveTVInGuide()))
                tv->DoEditSchedule();

            LOG(VB_PLAYBACK, LOG_INFO, LOC + "tv->LiveTV() -- end");
        }
        else
        {
            if (!ConfiguredTunerCards())
                LOG(VB_GENERAL, LOG_ERR, LOC + "No tuners configured");
            else
                LOG(VB_GENERAL, LOG_ERR, LOC + "No tuners free for live tv");
            quitAll = true;
            continue;
        }

        tv->setInPlayList(inPlaylist);
        tv->setUnderNetworkControl(initByNetworkCommand);

        // Process Events
        LOG(VB_GENERAL, LOG_INFO, LOC + "Entering main playback loop.");
        tv->PlaybackLoop();
        LOG(VB_GENERAL, LOG_INFO, LOC + "Exiting main playback loop.");

        if (tv->getJumpToProgram())
        {
            ProgramInfo *nextProgram = tv->GetLastProgram();

            tv->SetLastProgram(curProgram);
            if (curProgram)
                delete curProgram;

            curProgram = nextProgram;

            SendMythSystemPlayEvent("PLAY_CHANGED", curProgram);
            continue;
        }

        const PlayerContext *mctx =
            tv->GetPlayerReadLock(0, __FILE__, __LINE__);
        quitAll = tv->wantsToQuit || (mctx && mctx->errored);
        if (mctx)
        {
            mctx->LockDeletePlayer(__FILE__, __LINE__);
            if (mctx->player && mctx->player->IsErrored())
                playerError = mctx->player->GetError();
            mctx->UnlockDeletePlayer(__FILE__, __LINE__);
        }
        tv->ReturnPlayerLock(mctx);
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "StartTV -- process events 2 begin");
    qApp->processEvents();
    LOG(VB_PLAYBACK, LOG_INFO, LOC + "StartTV -- process events 2 end");

    // check if the show has reached the end.
    if (tvrec && tv->getEndOfRecording())
        playCompleted = true;

    bool allowrerecord = tv->getAllowRerecord();
    bool deleterecording = tv->requestDelete;

    ReleaseTV(tv);

    if (curProgram)
    {
        SendMythSystemPlayEvent("PLAY_STOPPED", curProgram);

        if (deleterecording)
        {
            QStringList list;
            list.push_back(QString::number(curProgram->GetChanID()));
            list.push_back(curProgram->GetRecordingStartTime(ISODate));
            list.push_back("0"); // do not force delete
            list.push_back(allowrerecord ? "1" : "0");
            MythEvent me("LOCAL_PBB_DELETE_RECORDINGS", list);
            gCoreContext->dispatch(me);
        }
        else if (curProgram->IsRecording())
        {
            lastProgramStringList.clear();
            curProgram->ToStringList(lastProgramStringList);
        }

        delete curProgram;
    }
    else
        gCoreContext->SendSystemEvent("PLAY_STOPPED");

    if (!playerError.isEmpty())
    {
        MythScreenStack *ss = GetMythMainWindow()->GetStack("popup stack");
        MythConfirmationDialog *dlg = new MythConfirmationDialog(
            ss, playerError, false);
        if (!dlg->Create())
            delete dlg;
        else
            ss->AddScreen(dlg);
    }

    sendPlaybackEnd();

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "StartTV -- end");

    return playCompleted;
}

/**
 * \brief Import pointers to functions used to embed the TV
 * window into other containers e.g. playbackbox
 */
void TV::SetFuncPtr(const char *string, void *lptr)
{
    QString name(string);
    if (name == "playbackbox")
        RunPlaybackBoxPtr = (EMBEDRETURNVOID)lptr;
    else if (name == "viewscheduled")
        RunViewScheduledPtr = (EMBEDRETURNVOID)lptr;
    else if (name == "programguide")
        RunProgramGuidePtr = (EMBEDRETURNVOIDEPG)lptr;
    else if (name == "programfinder")
        RunProgramFinderPtr = (EMBEDRETURNVOIDFINDER)lptr;
    else if (name == "scheduleeditor")
        RunScheduleEditorPtr = (EMBEDRETURNVOIDSCHEDIT)lptr;
}

void TV::InitKeys(void)
{
    REG_KEY("TV Frontend", ACTION_PLAYBACK, QT_TRANSLATE_NOOP("MythControls",
            "Play Program"), "P");
    REG_KEY("TV Frontend", ACTION_STOP, QT_TRANSLATE_NOOP("MythControls",
            "Stop Program"), "");
    REG_KEY("TV Frontend", ACTION_TOGGLERECORD, QT_TRANSLATE_NOOP("MythControls",
            "Toggle recording status of current program"), "R");
    REG_KEY("TV Frontend", ACTION_DAYLEFT, QT_TRANSLATE_NOOP("MythControls",
            "Page the program guide back one day"), "Home");
    REG_KEY("TV Frontend", ACTION_DAYRIGHT, QT_TRANSLATE_NOOP("MythControls",
            "Page the program guide forward one day"), "End");
    REG_KEY("TV Frontend", ACTION_PAGELEFT, QT_TRANSLATE_NOOP("MythControls",
            "Page the program guide left"), ",,<");
    REG_KEY("TV Frontend", ACTION_PAGERIGHT, QT_TRANSLATE_NOOP("MythControls",
            "Page the program guide right"), ">,.");
    REG_KEY("TV Frontend", ACTION_TOGGLEFAV, QT_TRANSLATE_NOOP("MythControls",
            "Toggle the current channel as a favorite"), "?");
    REG_KEY("TV Frontend", ACTION_TOGGLEPGORDER, QT_TRANSLATE_NOOP("MythControls",
            "Reverse the channel order in the program guide"), "");
    REG_KEY("TV Frontend", ACTION_GUIDE, QT_TRANSLATE_NOOP("MythControls",
            "Show the Program Guide"), "S");
    REG_KEY("TV Frontend", ACTION_FINDER, QT_TRANSLATE_NOOP("MythControls",
            "Show the Program Finder"), "#");
    REG_KEY("TV Frontend", "NEXTFAV", QT_TRANSLATE_NOOP("MythControls",
            "Cycle through channel groups and all channels in the "
            "program guide."), "/");
    REG_KEY("TV Frontend", "CHANUPDATE", QT_TRANSLATE_NOOP("MythControls",
            "Switch channels without exiting guide in Live TV mode."), "X");
    REG_KEY("TV Frontend", ACTION_VOLUMEDOWN, QT_TRANSLATE_NOOP("MythControls",
            "Volume down"), "[,{,F10,Volume Down");
    REG_KEY("TV Frontend", ACTION_VOLUMEUP, QT_TRANSLATE_NOOP("MythControls",
            "Volume up"), "],},F11,Volume Up");
    REG_KEY("TV Frontend", ACTION_MUTEAUDIO, QT_TRANSLATE_NOOP("MythControls",
            "Mute"), "|,\\,F9,Volume Mute");
    REG_KEY("TV Frontend", "CYCLEAUDIOCHAN", QT_TRANSLATE_NOOP("MythControls",
            "Cycle audio channels"), "");
    REG_KEY("TV Frontend", "RANKINC", QT_TRANSLATE_NOOP("MythControls",
            "Increase program or channel rank"), "Right");
    REG_KEY("TV Frontend", "RANKDEC", QT_TRANSLATE_NOOP("MythControls",
            "Decrease program or channel rank"), "Left");
    REG_KEY("TV Frontend", "UPCOMING", QT_TRANSLATE_NOOP("MythControls",
            "List upcoming episodes"), "O");
    REG_KEY("TV Frontend", ACTION_VIEWSCHEDULED, QT_TRANSLATE_NOOP("MythControls",
            "List scheduled upcoming episodes"), "");
    REG_KEY("TV Frontend", "DETAILS", QT_TRANSLATE_NOOP("MythControls",
            "Show details"), "U");
    REG_KEY("TV Frontend", "VIEWCARD", QT_TRANSLATE_NOOP("MythControls",
            "Switch Capture Card view"), "Y");
    REG_KEY("TV Frontend", "VIEWINPUT", QT_TRANSLATE_NOOP("MythControls",
            "Switch Capture Card view"), "C");
    REG_KEY("TV Frontend", "CUSTOMEDIT", QT_TRANSLATE_NOOP("MythControls",
            "Edit Custom Record Rule"), "");
    REG_KEY("TV Frontend", "CHANGERECGROUP", QT_TRANSLATE_NOOP("MythControls",
            "Change Recording Group"), "");
    REG_KEY("TV Frontend", "CHANGEGROUPVIEW", QT_TRANSLATE_NOOP("MythControls",
            "Change Group View"), "");

    REG_KEY("TV Playback", "BACK", QT_TRANSLATE_NOOP("MythControls",
            "Exit or return to DVD menu"), "");
    REG_KEY("TV Playback", ACTION_CLEAROSD, QT_TRANSLATE_NOOP("MythControls",
            "Clear OSD"), "Backspace");
    REG_KEY("TV Playback", ACTION_PAUSE, QT_TRANSLATE_NOOP("MythControls",
            "Pause"), "P");
    REG_KEY("TV Playback", ACTION_SEEKFFWD, QT_TRANSLATE_NOOP("MythControls",
            "Fast Forward"), "Right");
    REG_KEY("TV Playback", ACTION_SEEKRWND, QT_TRANSLATE_NOOP("MythControls",
            "Rewind"), "Left");
    REG_KEY("TV Playback", ACTION_SEEKARB, QT_TRANSLATE_NOOP("MythControls",
            "Arbitrary Seek"), "*");
    REG_KEY("TV Playback", ACTION_SEEKABSOLUTE, QT_TRANSLATE_NOOP("MythControls",
            "Seek to a position in seconds"), "");
    REG_KEY("TV Playback", ACTION_CHANNELUP, QT_TRANSLATE_NOOP("MythControls",
            "Channel up"), "Up");
    REG_KEY("TV Playback", ACTION_CHANNELDOWN, QT_TRANSLATE_NOOP("MythControls",
            "Channel down"), "Down");
    REG_KEY("TV Playback", "NEXTFAV", QT_TRANSLATE_NOOP("MythControls",
            "Switch to the next favorite channel"), "/");
    REG_KEY("TV Playback", "PREVCHAN", QT_TRANSLATE_NOOP("MythControls",
            "Switch to the previous channel"), "H");
    REG_KEY("TV Playback", ACTION_JUMPFFWD, QT_TRANSLATE_NOOP("MythControls",
            "Jump ahead"), "PgDown");
    REG_KEY("TV Playback", ACTION_JUMPRWND, QT_TRANSLATE_NOOP("MythControls",
            "Jump back"), "PgUp");
    REG_KEY("TV Playback", ACTION_JUMPBKMRK, QT_TRANSLATE_NOOP("MythControls",
            "Jump to bookmark"), "K");
    REG_KEY("TV Playback", "FFWDSTICKY", QT_TRANSLATE_NOOP("MythControls",
            "Fast Forward (Sticky) or Forward one frame while paused"), ">,.");
    REG_KEY("TV Playback", "RWNDSTICKY", QT_TRANSLATE_NOOP("MythControls",
            "Rewind (Sticky) or Rewind one frame while paused"), ",,<");
    REG_KEY("TV Playback", "NEXTSOURCE", QT_TRANSLATE_NOOP("MythControls",
            "Next Video Source"), "Y");
    REG_KEY("TV Playback", "PREVSOURCE", QT_TRANSLATE_NOOP("MythControls",
            "Previous Video Source"), "");
    REG_KEY("TV Playback", "NEXTINPUT", QT_TRANSLATE_NOOP("MythControls",
            "Next Input"), "C");
    REG_KEY("TV Playback", "NEXTCARD", QT_TRANSLATE_NOOP("MythControls",
            "Next Card"), "");
    REG_KEY("TV Playback", "SKIPCOMMERCIAL", QT_TRANSLATE_NOOP("MythControls",
            "Skip Commercial"), "Z,End");
    REG_KEY("TV Playback", "SKIPCOMMBACK", QT_TRANSLATE_NOOP("MythControls",
            "Skip Commercial (Reverse)"), "Q,Home");
    REG_KEY("TV Playback", ACTION_JUMPSTART, QT_TRANSLATE_NOOP("MythControls",
            "Jump to the start of the recording."), "Ctrl+B");
    REG_KEY("TV Playback", "TOGGLEBROWSE", QT_TRANSLATE_NOOP("MythControls",
            "Toggle channel browse mode"), "O");
    REG_KEY("TV Playback", ACTION_TOGGLERECORD, QT_TRANSLATE_NOOP("MythControls",
            "Toggle recording status of current program"), "R");
    REG_KEY("TV Playback", ACTION_TOGGLEFAV, QT_TRANSLATE_NOOP("MythControls",
            "Toggle the current channel as a favorite"), "?");
    REG_KEY("TV Playback", ACTION_VOLUMEDOWN, QT_TRANSLATE_NOOP("MythControls",
            "Volume down"), "[,{,F10,Volume Down");
    REG_KEY("TV Playback", ACTION_VOLUMEUP, QT_TRANSLATE_NOOP("MythControls",
            "Volume up"), "],},F11,Volume Up");
    REG_KEY("TV Playback", ACTION_MUTEAUDIO, QT_TRANSLATE_NOOP("MythControls",
            "Mute"), "|,\\,F9,Volume Mute");
    REG_KEY("TV Playback", ACTION_SETVOLUME, QT_TRANSLATE_NOOP("MythControls",
            "Set the volume"), "");
    REG_KEY("TV Playback", "CYCLEAUDIOCHAN", QT_TRANSLATE_NOOP("MythControls",
            "Cycle audio channels"), "");
    REG_KEY("TV Playback", ACTION_TOGGLEUPMIX, QT_TRANSLATE_NOOP("MythControls",
            "Toggle audio upmixer"), "Ctrl+U");
    REG_KEY("TV Playback", "TOGGLEPIPMODE", QT_TRANSLATE_NOOP("MythControls",
            "Toggle Picture-in-Picture view"), "V");
    REG_KEY("TV Playback", "TOGGLEPBPMODE", QT_TRANSLATE_NOOP("MythControls",
            "Toggle Picture-by-Picture view"), "Ctrl+V");
    REG_KEY("TV Playback", "CREATEPIPVIEW", QT_TRANSLATE_NOOP("MythControls",
            "Create Picture-in-Picture view"), "");
    REG_KEY("TV Playback", "CREATEPBPVIEW", QT_TRANSLATE_NOOP("MythControls",
            "Create Picture-by-Picture view"), "");
    REG_KEY("TV Playback", "NEXTPIPWINDOW", QT_TRANSLATE_NOOP("MythControls",
            "Toggle active PIP/PBP window"), "B");
    REG_KEY("TV Playback", "SWAPPIP", QT_TRANSLATE_NOOP("MythControls",
            "Swap PBP/PIP Windows"), "N");
    REG_KEY("TV Playback", "TOGGLEPIPSTATE", QT_TRANSLATE_NOOP("MythControls",
            "Change PxP view"), "");
    REG_KEY("TV Playback", "TOGGLEASPECT", QT_TRANSLATE_NOOP("MythControls",
            "Toggle the video aspect ratio"), "Ctrl+W");
    REG_KEY("TV Playback", "TOGGLEFILL", QT_TRANSLATE_NOOP("MythControls",
            "Next Preconfigured Zoom mode"), "W");
    REG_KEY("TV Playback", ACTION_TOGGLESUBS, QT_TRANSLATE_NOOP("MythControls",
            "Toggle any captions"), "T");
    REG_KEY("TV Playback", ACTION_ENABLESUBS, QT_TRANSLATE_NOOP("MythControls",
            "Enable any captions"), "");
    REG_KEY("TV Playback", ACTION_DISABLESUBS, QT_TRANSLATE_NOOP("MythControls",
            "Disable any captions"), "");
    REG_KEY("TV Playback", "TOGGLETTC", QT_TRANSLATE_NOOP("MythControls",
            "Toggle Teletext Captions"),"");
    REG_KEY("TV Playback", "TOGGLESUBTITLE", QT_TRANSLATE_NOOP("MythControls",
            "Toggle Subtitles"), "");
    REG_KEY("TV Playback", "TOGGLECC608", QT_TRANSLATE_NOOP("MythControls",
            "Toggle VBI CC"), "");
    REG_KEY("TV Playback", "TOGGLECC708", QT_TRANSLATE_NOOP("MythControls",
            "Toggle ATSC CC"), "");
    REG_KEY("TV Playback", "TOGGLETTM", QT_TRANSLATE_NOOP("MythControls",
            "Toggle Teletext Menu"), "");
    REG_KEY("TV Playback", ACTION_TOGGLEEXTTEXT, QT_TRANSLATE_NOOP("MythControls",
            "Toggle External Subtitles"), "");
    REG_KEY("TV Playback", ACTION_ENABLEEXTTEXT, QT_TRANSLATE_NOOP("MythControls",
            "Enable External Subtitles"), "");
    REG_KEY("TV Playback", ACTION_DISABLEEXTTEXT, QT_TRANSLATE_NOOP("MythControls",
            "Disable External Subtitles"), "");
    REG_KEY("TV Playback", "TOGGLERAWTEXT", QT_TRANSLATE_NOOP("MythControls",
            "Toggle Text Subtitles"), "");

    REG_KEY("TV Playback", "SELECTAUDIO_0", QT_TRANSLATE_NOOP("MythControls",
            "Play audio track 1"), "");
    REG_KEY("TV Playback", "SELECTAUDIO_1", QT_TRANSLATE_NOOP("MythControls",
            "Play audio track 2"), "");
    REG_KEY("TV Playback", "SELECTSUBTITLE_0",QT_TRANSLATE_NOOP("MythControls",
            "Display subtitle 1"), "");
    REG_KEY("TV Playback", "SELECTSUBTITLE_1",QT_TRANSLATE_NOOP("MythControls",
            "Display subtitle 2"), "");
    REG_KEY("TV Playback", "SELECTRAWTEXT_0",QT_TRANSLATE_NOOP("MythControls",
            "Display Text Subtitle 1"), "");
    REG_KEY("TV Playback", "SELECTCC608_0", QT_TRANSLATE_NOOP("MythControls",
            "Display VBI CC1"), "");
    REG_KEY("TV Playback", "SELECTCC608_1", QT_TRANSLATE_NOOP("MythControls",
            "Display VBI CC2"), "");
    REG_KEY("TV Playback", "SELECTCC608_2", QT_TRANSLATE_NOOP("MythControls",
            "Display VBI CC3"), "");
    REG_KEY("TV Playback", "SELECTCC608_3", QT_TRANSLATE_NOOP("MythControls",
            "Display VBI CC4"), "");
    REG_KEY("TV Playback", "SELECTCC708_0", QT_TRANSLATE_NOOP("MythControls",
            "Display ATSC CC1"), "");
    REG_KEY("TV Playback", "SELECTCC708_1", QT_TRANSLATE_NOOP("MythControls",
            "Display ATSC CC2"), "");
    REG_KEY("TV Playback", "SELECTCC708_2", QT_TRANSLATE_NOOP("MythControls",
            "Display ATSC CC3"), "");
    REG_KEY("TV Playback", "SELECTCC708_3", QT_TRANSLATE_NOOP("MythControls",
            "Display ATSC CC4"), "");
    REG_KEY("TV Playback", ACTION_ENABLEFORCEDSUBS, QT_TRANSLATE_NOOP("MythControls",
            "Enable Forced Subtitles"), "");
    REG_KEY("TV Playback", ACTION_DISABLEFORCEDSUBS, QT_TRANSLATE_NOOP("MythControls",
            "Disable Forced Subtitles"), "");

    REG_KEY("TV Playback", "NEXTAUDIO", QT_TRANSLATE_NOOP("MythControls",
            "Next audio track"), "+");
    REG_KEY("TV Playback", "PREVAUDIO", QT_TRANSLATE_NOOP("MythControls",
            "Previous audio track"), "-");
    REG_KEY("TV Playback", "NEXTSUBTITLE", QT_TRANSLATE_NOOP("MythControls",
            "Next subtitle track"), "");
    REG_KEY("TV Playback", "PREVSUBTITLE", QT_TRANSLATE_NOOP("MythControls",
            "Previous subtitle track"), "");
    REG_KEY("TV Playback", "NEXTRAWTEXT", QT_TRANSLATE_NOOP("MythControls",
            "Next Text track"), "");
    REG_KEY("TV Playback", "PREVRAWTEXT", QT_TRANSLATE_NOOP("MythControls",
            "Previous Text track"), "");
    REG_KEY("TV Playback", "NEXTCC608", QT_TRANSLATE_NOOP("MythControls",
            "Next VBI CC track"), "");
    REG_KEY("TV Playback", "PREVCC608", QT_TRANSLATE_NOOP("MythControls",
            "Previous VBI CC track"), "");
    REG_KEY("TV Playback", "NEXTCC708", QT_TRANSLATE_NOOP("MythControls",
            "Next ATSC CC track"), "");
    REG_KEY("TV Playback", "PREVCC708", QT_TRANSLATE_NOOP("MythControls",
            "Previous ATSC CC track"), "");
    REG_KEY("TV Playback", "NEXTCC", QT_TRANSLATE_NOOP("MythControls",
            "Next of any captions"), "");

    REG_KEY("TV Playback", "NEXTSCAN", QT_TRANSLATE_NOOP("MythControls",
            "Next video scan overidemode"), "");
    REG_KEY("TV Playback", "QUEUETRANSCODE", QT_TRANSLATE_NOOP("MythControls",
            "Queue the current recording for transcoding"), "X");
    REG_KEY("TV Playback", "SPEEDINC", QT_TRANSLATE_NOOP("MythControls",
            "Increase the playback speed"), "U");
    REG_KEY("TV Playback", "SPEEDDEC", QT_TRANSLATE_NOOP("MythControls",
            "Decrease the playback speed"), "J");
    REG_KEY("TV Playback", "ADJUSTSTRETCH", QT_TRANSLATE_NOOP("MythControls",
            "Turn on time stretch control"), "A");
    REG_KEY("TV Playback", "STRETCHINC", QT_TRANSLATE_NOOP("MythControls",
            "Increase time stretch speed"), "");
    REG_KEY("TV Playback", "STRETCHDEC", QT_TRANSLATE_NOOP("MythControls",
            "Decrease time stretch speed"), "");
    REG_KEY("TV Playback", "TOGGLESTRETCH", QT_TRANSLATE_NOOP("MythControls",
            "Toggle time stretch speed"), "");
    REG_KEY("TV Playback", ACTION_TOGGELAUDIOSYNC,
            QT_TRANSLATE_NOOP("MythControls",
            "Turn on audio sync adjustment controls"), "");
    REG_KEY("TV Playback", ACTION_SETAUDIOSYNC,
            QT_TRANSLATE_NOOP("MythControls",
            "Set the audio sync adjustment"), "");
    REG_KEY("TV Playback", "TOGGLEPICCONTROLS",
            QT_TRANSLATE_NOOP("MythControls", "Playback picture adjustments"),
             "F");
    REG_KEY("TV Playback", ACTION_TOGGLENIGHTMODE,
            QT_TRANSLATE_NOOP("MythControls", "Toggle night mode"), "Ctrl+F");
    REG_KEY("TV Playback", ACTION_SETBRIGHTNESS,
            QT_TRANSLATE_NOOP("MythControls", "Set the picture brightness"), "");
    REG_KEY("TV Playback", ACTION_SETCONTRAST,
            QT_TRANSLATE_NOOP("MythControls", "Set the picture contrast"), "");
    REG_KEY("TV Playback", ACTION_SETCOLOUR,
            QT_TRANSLATE_NOOP("MythControls", "Set the picture color"), "");
    REG_KEY("TV Playback", ACTION_SETHUE,
            QT_TRANSLATE_NOOP("MythControls", "Set the picture hue"), "");
    REG_KEY("TV Playback", ACTION_TOGGLESTUDIOLEVELS,
            QT_TRANSLATE_NOOP("MythControls", "Playback picture adjustments"),
             "");
    REG_KEY("TV Playback", ACTION_TOGGLECHANCONTROLS,
            QT_TRANSLATE_NOOP("MythControls", "Recording picture adjustments "
            "for this channel"), "Ctrl+G");
    REG_KEY("TV Playback", ACTION_TOGGLERECCONTROLS,
            QT_TRANSLATE_NOOP("MythControls", "Recording picture adjustments "
            "for this recorder"), "G");
    REG_KEY("TV Playback", "CYCLECOMMSKIPMODE",
            QT_TRANSLATE_NOOP("MythControls", "Cycle Commercial Skip mode"),
            "");
    REG_KEY("TV Playback", ACTION_GUIDE, QT_TRANSLATE_NOOP("MythControls",
            "Show the Program Guide"), "S");
    REG_KEY("TV Playback", ACTION_FINDER, QT_TRANSLATE_NOOP("MythControls",
            "Show the Program Finder"), "#");
    REG_KEY("TV Playback", ACTION_TOGGLESLEEP, QT_TRANSLATE_NOOP("MythControls",
            "Toggle the Sleep Timer"), "F8");
    REG_KEY("TV Playback", ACTION_PLAY, QT_TRANSLATE_NOOP("MythControls", "Play"),
            "Ctrl+P");
    REG_KEY("TV Playback", ACTION_JUMPPREV, QT_TRANSLATE_NOOP("MythControls",
            "Jump to previously played recording"), "");
    REG_KEY("TV Playback", ACTION_JUMPREC, QT_TRANSLATE_NOOP("MythControls",
            "Display menu of recorded programs to jump to"), "");
    REG_KEY("TV Playback", ACTION_VIEWSCHEDULED, QT_TRANSLATE_NOOP("MythControls",
            "Display scheduled recording list"), "");
    REG_KEY("TV Playback", ACTION_SIGNALMON, QT_TRANSLATE_NOOP("MythControls",
            "Monitor Signal Quality"), "Alt+F7");
    REG_KEY("TV Playback", ACTION_JUMPTODVDROOTMENU,
            QT_TRANSLATE_NOOP("MythControls", "Jump to the DVD Root Menu"), "");
    REG_KEY("TV Playback", ACTION_JUMPTOPOPUPMENU,
            QT_TRANSLATE_NOOP("MythControls", "Jump to the Popup Menu"), "");
    REG_KEY("TV Playback", ACTION_JUMPTODVDCHAPTERMENU,
            QT_TRANSLATE_NOOP("MythControls", "Jump to the DVD Chapter Menu"), "");
    REG_KEY("TV Playback", ACTION_JUMPTODVDTITLEMENU,
            QT_TRANSLATE_NOOP("MythControls", "Jump to the DVD Title Menu"), "");
    REG_KEY("TV Playback", ACTION_EXITSHOWNOPROMPTS,
            QT_TRANSLATE_NOOP("MythControls", "Exit Show without any prompts"),
            "");
    REG_KEY("TV Playback", ACTION_JUMPCHAPTER, QT_TRANSLATE_NOOP("MythControls",
            "Jump to a chapter"), "");
    REG_KEY("TV Playback", ACTION_SWITCHTITLE, QT_TRANSLATE_NOOP("MythControls",
            "Switch title"), "");
    REG_KEY("TV Playback", ACTION_SWITCHANGLE, QT_TRANSLATE_NOOP("MythControls",
            "Switch angle"), "");

    /* Interactive Television keys */
    REG_KEY("TV Playback", ACTION_MENURED,    QT_TRANSLATE_NOOP("MythControls",
            "Menu Red"),    "F2");
    REG_KEY("TV Playback", ACTION_MENUGREEN,  QT_TRANSLATE_NOOP("MythControls",
            "Menu Green"),  "F3");
    REG_KEY("TV Playback", ACTION_MENUYELLOW, QT_TRANSLATE_NOOP("MythControls",
            "Menu Yellow"), "F4");
    REG_KEY("TV Playback", ACTION_MENUBLUE,   QT_TRANSLATE_NOOP("MythControls",
            "Menu Blue"),   "F5");
    REG_KEY("TV Playback", ACTION_TEXTEXIT,   QT_TRANSLATE_NOOP("MythControls",
            "Menu Exit"),   "F6");
    REG_KEY("TV Playback", ACTION_MENUTEXT,   QT_TRANSLATE_NOOP("MythControls",
            "Menu Text"),   "F7");
    REG_KEY("TV Playback", ACTION_MENUEPG,    QT_TRANSLATE_NOOP("MythControls",
            "Menu EPG"),    "F12");

    /* Editing keys */
    REG_KEY("TV Editing", ACTION_CLEARMAP,    QT_TRANSLATE_NOOP("MythControls",
            "Clear editing cut points"), "C,Q,Home");
    REG_KEY("TV Editing", ACTION_INVERTMAP,   QT_TRANSLATE_NOOP("MythControls",
            "Invert Begin/End cut points"),"I");
    REG_KEY("TV Editing", ACTION_SAVEMAP,     QT_TRANSLATE_NOOP("MythControls",
            "Save cuts"),"");
    REG_KEY("TV Editing", ACTION_LOADCOMMSKIP,QT_TRANSLATE_NOOP("MythControls",
            "Load cuts from detected commercials"), "Z,End");
    REG_KEY("TV Editing", ACTION_NEXTCUT,     QT_TRANSLATE_NOOP("MythControls",
            "Jump to the next cut point"), "PgDown");
    REG_KEY("TV Editing", ACTION_PREVCUT,     QT_TRANSLATE_NOOP("MythControls",
            "Jump to the previous cut point"), "PgUp");
    REG_KEY("TV Editing", ACTION_BIGJUMPREW,  QT_TRANSLATE_NOOP("MythControls",
            "Jump back 10x the normal amount"), ",,<");
    REG_KEY("TV Editing", ACTION_BIGJUMPFWD,  QT_TRANSLATE_NOOP("MythControls",
            "Jump forward 10x the normal amount"), ">,.");

    /* Teletext keys */
    REG_KEY("Teletext Menu", ACTION_NEXTPAGE,    QT_TRANSLATE_NOOP("MythControls",
            "Next Page"),             "Down");
    REG_KEY("Teletext Menu", ACTION_PREVPAGE,    QT_TRANSLATE_NOOP("MythControls",
            "Previous Page"),         "Up");
    REG_KEY("Teletext Menu", ACTION_NEXTSUBPAGE, QT_TRANSLATE_NOOP("MythControls",
            "Next Subpage"),          "Right");
    REG_KEY("Teletext Menu", ACTION_PREVSUBPAGE, QT_TRANSLATE_NOOP("MythControls",
            "Previous Subpage"),      "Left");
    REG_KEY("Teletext Menu", ACTION_TOGGLETT,    QT_TRANSLATE_NOOP("MythControls",
            "Toggle Teletext"),       "T");
    REG_KEY("Teletext Menu", ACTION_MENURED,     QT_TRANSLATE_NOOP("MythControls",
            "Menu Red"),              "F2");
    REG_KEY("Teletext Menu", ACTION_MENUGREEN,   QT_TRANSLATE_NOOP("MythControls",
            "Menu Green"),            "F3");
    REG_KEY("Teletext Menu", ACTION_MENUYELLOW,  QT_TRANSLATE_NOOP("MythControls",
            "Menu Yellow"),           "F4");
    REG_KEY("Teletext Menu", ACTION_MENUBLUE,    QT_TRANSLATE_NOOP("MythControls",
            "Menu Blue"),             "F5");
    REG_KEY("Teletext Menu", ACTION_MENUWHITE,   QT_TRANSLATE_NOOP("MythControls",
            "Menu White"),            "F6");
    REG_KEY("Teletext Menu", ACTION_TOGGLEBACKGROUND,
            QT_TRANSLATE_NOOP("MythControls", "Toggle Background"), "F7");
    REG_KEY("Teletext Menu", ACTION_REVEAL,      QT_TRANSLATE_NOOP("MythControls",
            "Reveal hidden Text"),    "F8");

    /* Visualisations */
    REG_KEY("TV Playback", ACTION_TOGGLEVISUALISATION,
            QT_TRANSLATE_NOOP("MythControls", "Toggle audio visualisation"), "");

    /* OSD playback information screen */
    REG_KEY("TV Playback", ACTION_TOGGLEOSDDEBUG,
            QT_TRANSLATE_NOOP("MythControls", "Toggle OSD playback information"), "");

    /* 3D/Frame compatible/Stereoscopic TV */
    REG_KEY("TV Playback", ACTION_3DNONE,
            QT_TRANSLATE_NOOP("MythControls", "No 3D"), "");
    REG_KEY("TV Playback", ACTION_3DSIDEBYSIDE,
            QT_TRANSLATE_NOOP("MythControls", "3D Side by Side"), "");
    REG_KEY("TV Playback", ACTION_3DSIDEBYSIDEDISCARD,
            QT_TRANSLATE_NOOP("MythControls", "Discard 3D Side by Side"), "");
    REG_KEY("TV Playback", ACTION_3DTOPANDBOTTOM,
            QT_TRANSLATE_NOOP("MythControls", "3D Top and Bottom"), "");
    REG_KEY("TV Playback", ACTION_3DTOPANDBOTTOMDISCARD,
            QT_TRANSLATE_NOOP("MythControls", "Discard 3D Top and Bottom"), "");

/*
  keys already used:

  Global:           I   M              0123456789
  Playback: ABCDEFGH JK  NOPQRSTUVWXYZ
  Frontend:   CD          OP R  U  XY  01 3   7 9
  Editing:    C E   I       Q        Z
  Teletext:                    T

  Playback: <>,.?/|[]{}\+-*#^
  Frontend: <>,.?/
  Editing:  <>,.

  Global:   PgDown, PgUp,  Right, Left, Home, End, Up, Down,
  Playback: PgDown, PgUp,  Right, Left, Home, End, Up, Down, Backspace,
  Frontend:                Right, Left, Home, End
  Editing:  PgDown, PgUp,               Home, End
  Teletext:                Right, Left,            Up, Down,

  Global:   Return, Enter, Space, Esc

  Global:   F1,
  Playback:                   F7,F8,F9,F10,F11
  Teletext     F2,F3,F4,F5,F6,F7,F8
  ITV          F2,F3,F4,F5,F6,F7,F12

  Playback: Ctrl-B,Ctrl-G,Ctrl-Y,Ctrl-U
*/
}

void TV::ReloadKeys(void)
{
    MythMainWindow *mainWindow = GetMythMainWindow();
    mainWindow->ClearKeyContext("TV Frontend");
    mainWindow->ClearKeyContext("TV Playback");
    mainWindow->ClearKeyContext("TV Editing");
    mainWindow->ClearKeyContext("Teletext Menu");
    InitKeys();
}

/** \fn TV::TV(void)
 *  \sa Init(void)
 */
TV::TV(void)
    : // Configuration variables from database
      baseFilters(""),
      db_channel_format("<num> <sign>"),
      db_idle_timeout(0),
      db_playback_exit_prompt(0),   db_autoexpire_default(0),
      db_auto_set_watched(false),   db_end_of_rec_exit_prompt(false),
      db_jump_prefer_osd(true),     db_use_gui_size_for_tv(false),
      db_start_in_guide(false),     db_toggle_bookmark(false),
      db_run_jobs_on_remote(false), db_continue_embedded(false),
      db_use_fixed_size(true),      db_browse_always(false),
      db_browse_all_tuners(false),
      db_use_channel_groups(false), db_remember_last_channel_group(false),

      tryUnflaggedSkip(false),
      smartForward(false),
      ff_rew_repos(1.0f), ff_rew_reverse(false),
      jumped_back(false),
      vbimode(VBIMode::None),
      // State variables
      switchToInputId(0),
      wantsToQuit(true),
      stretchAdjustment(false),
      audiosyncAdjustment(false),
      editmode(false),     zoomMode(false),
      sigMonMode(false),
      endOfRecording(false),
      requestDelete(false),  allowRerecord(false),
      doSmartForward(false),
      queuedTranscode(false),
      adjustingPicture(kAdjustingPicture_None),
      adjustingPictureAttribute(kPictureAttribute_None),
      askAllowLock(QMutex::Recursive),
      // Channel Editing
      chanEditMapLock(QMutex::Recursive),
      ddMapSourceId(0), ddMapLoader(new DDLoader(this)),
      // Sleep Timer
      sleep_index(0), sleepTimerId(0), sleepDialogTimerId(0),
      // Idle Timer
      idleTimerId(0), idleDialogTimerId(0),
      // CC/Teletext input state variables
      ccInputMode(false),
      // Arbritary seek input state variables
      asInputMode(false),
      // Channel changing state variables
      queuedChanNum(""),
      lockTimerOn(false),
      // channel browsing
      browsehelper(NULL),
      // Program Info for currently playing video
      lastProgram(NULL),
      inPlaylist(false), underNetworkControl(false),
      // Jump to program stuff
      jumpToProgramPIPState(kPIPOff),
      jumpToProgram(false),
      // Video Player currently receiving UI input
      playerActive(-1),
      noHardwareDecoders(false),
      //Recorder switching info
      switchToRec(NULL),
      // LCD Info
      lcdTitle(""), lcdSubtitle(""), lcdCallsign(""),
      // Window info (GUI is optional, transcoding, preview img, etc)
      myWindow(NULL),               weDisabledGUI(false),
      disableDrawUnusedRects(false),
      isEmbedded(false),            ignoreKeyPresses(false),
      // Timers
      lcdTimerId(0),                lcdVolumeTimerId(0),
      networkControlTimerId(0),     jumpMenuTimerId(0),
      pipChangeTimerId(0),
      switchToInputTimerId(0),      ccInputTimerId(0),
      asInputTimerId(0),            queueInputTimerId(0),
      browseTimerId(0),             updateOSDPosTimerId(0),
      updateOSDDebugTimerId(0),
      endOfPlaybackTimerId(0),      embedCheckTimerId(0),
      endOfRecPromptTimerId(0),     videoExitDialogTimerId(0),
      pseudoChangeChanTimerId(0),   speedChangeTimerId(0),
      errorRecoveryTimerId(0),      exitPlayerTimerId(0),
      m_ignoreActions(NULL), m_editActions(NULL),
      m_browseActions(NULL), m_browsePassActions(NULL),
      m_manualZoomActions(NULL), m_manualZoomPassActions(NULL),
      m_picAttrActions(NULL), m_timeStretchActions(NULL),
      m_audioSyncActions(NULL), m_3dActions(NULL), m_activeActions(NULL),
      m_jumpProgramActions(NULL), m_seekActions(NULL), m_trackActions(NULL),
      m_ffrwndActions(NULL), m_toggleActions(NULL), m_pipActions(NULL),
      m_activePostQActions(NULL), m_osdActions(NULL), m_osdExitActions(NULL),
      m_osdLiveTVActions(NULL), m_osdPlayingActions(NULL),
      m_osdDialogActions(NULL), m_osdAskAllowActions(NULL),
      m_networkControlActions(NULL),
      m_actionContext(NULL)
{
    LOG(VB_GENERAL, LOG_INFO, LOC + "Creating TV object");
    ctorTime.start();

    setObjectName("TV");
    keyRepeatTimer.start();

    sleep_times.push_back(SleepTimerInfo(QObject::tr("Off"),       0));
    sleep_times.push_back(SleepTimerInfo(QObject::tr("30m"),   30*60));
    sleep_times.push_back(SleepTimerInfo(QObject::tr("1h"),    60*60));
    sleep_times.push_back(SleepTimerInfo(QObject::tr("1h30m"), 90*60));
    sleep_times.push_back(SleepTimerInfo(QObject::tr("2h"),   120*60));

    playerLock.lockForWrite();
    player.push_back(new PlayerContext(kPlayerInUseID));
    playerActive = 0;
    playerLock.unlock();

    InitFromDB();

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "Finished creating TV object");
}

void TV::InitFromDB(void)
{
    QMap<QString,QString> kv;
    kv["LiveTVIdleTimeout"]        = "0";
    kv["BrowseMaxForward"]         = "240";
    kv["PlaybackExitPrompt"]       = "0";
    kv["AutoExpireDefault"]        = "0";
    kv["AutomaticSetWatched"]      = "0";
    kv["EndOfRecordingExitPrompt"] = "0";
    kv["JumpToProgramOSD"]         = "1";
    kv["GuiSizeForTV"]             = "0";
    kv["WatchTVGuide"]             = "0";
    kv["AltClearSavedPosition"]    = "1";
    kv["JobsRunOnRecordHost"]      = "0";
    kv["ContinueEmbeddedTVPlay"]   = "0";
    kv["UseFixedWindowSize"]       = "1";
    kv["PersistentBrowseMode"]     = "0";
    kv["BrowseAllTuners"]          = "0";
    kv["ChannelOrdering"]          = "channum";

    kv["CustomFilters"]            = "";
    kv["ChannelFormat"]            = "<num> <sign>";
    kv["TimeFormat"]               = "h:mm AP";
    kv["ShortDateFormat"]          = "M/d";

    kv["TryUnflaggedSkip"]         = "0";

    kv["ChannelGroupDefault"]      = "-1";
    kv["BrowseChannelGroup"]       = "0";
    kv["SmartForward"]             = "0";
    kv["FFRewReposTime"]           = "100";
    kv["FFRewReverse"]             = "1";

    kv["BrowseChannelGroup"]       = "0";
    kv["ChannelGroupDefault"]      = "-1";
    kv["ChannelGroupRememberLast"] = "0";

    kv["VbiFormat"]                = "";
    kv["DecodeVBIFormat"]          = "";

    int ff_rew_def[8] = { 3, 5, 10, 20, 30, 60, 120, 180 };
    for (uint i = 0; i < sizeof(ff_rew_def)/sizeof(ff_rew_def[0]); i++)
        kv[QString("FFRewSpeed%1").arg(i)] = QString::number(ff_rew_def[i]);

    MythDB::getMythDB()->GetSettings(kv);

    QString db_time_format;
    QString db_short_date_format;
    QString db_channel_ordering;
    uint    db_browse_max_forward;

    // convert from minutes to ms.
    db_idle_timeout        = kv["LiveTVIdleTimeout"].toInt() * 60 * 1000;
    db_browse_max_forward  = kv["BrowseMaxForward"].toInt() * 60;
    db_playback_exit_prompt= kv["PlaybackExitPrompt"].toInt();
    db_autoexpire_default  = kv["AutoExpireDefault"].toInt();
    db_auto_set_watched    = kv["AutomaticSetWatched"].toInt();
    db_end_of_rec_exit_prompt = kv["EndOfRecordingExitPrompt"].toInt();
    db_jump_prefer_osd     = kv["JumpToProgramOSD"].toInt();
    db_use_gui_size_for_tv = kv["GuiSizeForTV"].toInt();
    db_start_in_guide      = kv["WatchTVGuide"].toInt();
    db_toggle_bookmark     = kv["AltClearSavedPosition"].toInt();
    db_run_jobs_on_remote  = kv["JobsRunOnRecordHost"].toInt();
    db_continue_embedded   = kv["ContinueEmbeddedTVPlay"].toInt();
    db_use_fixed_size      = kv["UseFixedWindowSize"].toInt();
    db_browse_always       = kv["PersistentBrowseMode"].toInt();
    db_browse_all_tuners   = kv["BrowseAllTuners"].toInt();
    db_channel_ordering    = kv["ChannelOrdering"];
    baseFilters           += kv["CustomFilters"];
    db_channel_format      = kv["ChannelFormat"];
    db_time_format         = kv["TimeFormat"];
    db_short_date_format   = kv["ShortDateFormat"];
    tryUnflaggedSkip       = kv["TryUnflaggedSkip"].toInt();
    smartForward           = kv["SmartForward"].toInt();
    ff_rew_repos           = kv["FFRewReposTime"].toFloat() * 0.01f;
    ff_rew_reverse         = kv["FFRewReverse"].toInt();

    db_use_channel_groups  = kv["BrowseChannelGroup"].toInt();
    db_remember_last_channel_group = kv["ChannelGroupRememberLast"].toInt();
    channelGroupId         = kv["ChannelGroupDefault"].toInt();

    QString beVBI          = kv["VbiFormat"];
    QString feVBI          = kv["DecodeVBIFormat"];

    if (db_use_channel_groups)
    {
        db_channel_groups  = ChannelGroup::GetChannelGroups();
        if (channelGroupId > -1)
        {
            channelGroupChannelList = ChannelUtil::GetChannels(
                0, true, "channum, callsign", channelGroupId);
            ChannelUtil::SortChannels(
                channelGroupChannelList, "channum", true);
        }
    }

    for (uint i = 0; i < sizeof(ff_rew_def)/sizeof(ff_rew_def[0]); i++)
        ff_rew_speeds.push_back(kv[QString("FFRewSpeed%1").arg(i)].toInt());

    // process it..
    browsehelper = new TVBrowseHelper(
        this,
        db_time_format,          db_short_date_format,
        db_browse_max_forward,   db_browse_all_tuners,
        db_use_channel_groups,   db_channel_ordering);

    vbimode = VBIMode::Parse(!feVBI.isEmpty() ? feVBI : beVBI);

    gCoreContext->addListener(this);

    QMutexLocker lock(&initFromDBLock);
    initFromDBDone = true;
    initFromDBWait.wakeAll();
}

/** \fn TV::Init(bool)
 *  \brief Performs instance initialization, returns true on success.
 *
 *  \param createWindow If true a MythDialog is created for display.
 *  \return Returns true on success, false on failure.
 */
bool TV::Init(bool createWindow)
{
    LOG(VB_PLAYBACK, LOG_INFO, LOC + "Init -- begin");

    if (createWindow)
    {
        bool fullscreen = !gCoreContext->GetNumSetting("GuiSizeForTV", 0);
        bool switchMode = gCoreContext->GetNumSetting("UseVideoModes", 0);

        saved_gui_bounds = QRect(GetMythMainWindow()->geometry().topLeft(),
                                 GetMythMainWindow()->size());

        // adjust for window manager wierdness.
        {
            int xbase, width, ybase, height;
            float wmult, hmult;
            GetMythUI()->GetScreenSettings(xbase, width, wmult,
                                           ybase, height, hmult);
            if ((abs(saved_gui_bounds.x()-xbase) < 3) &&
                (abs(saved_gui_bounds.y()-ybase) < 3))
            {
                saved_gui_bounds = QRect(QPoint(xbase, ybase),
                                         GetMythMainWindow()->size());
            }
        }

        // if width && height are zero users expect fullscreen playback
        if (!fullscreen)
        {
            int gui_width = 0, gui_height = 0;
            gCoreContext->GetResolutionSetting("Gui", gui_width, gui_height);
            fullscreen |= (0 == gui_width && 0 == gui_height);
        }

        player_bounds = saved_gui_bounds;
        if (fullscreen)
        {
            int xbase, width, ybase, height;
            GetMythUI()->GetScreenBounds(xbase, ybase, width, height);
            player_bounds = QRect(xbase, ybase, width, height);
        }

        // main window sizing
        int maxWidth = 1920, maxHeight = 1440;
        if (switchMode)
        {
            DisplayRes *display_res = DisplayRes::GetDisplayRes();
            if(display_res)
            {
                // The very first Resize needs to be the maximum possible
                // desired res, because X will mask off anything outside
                // the initial dimensions
                maxWidth = display_res->GetMaxWidth();
                maxHeight = display_res->GetMaxHeight();

                // bit of a hack, but it's ok if the window is too
                // big in fullscreen mode
                if (fullscreen)
                {
                    player_bounds.setSize(QSize(maxWidth, maxHeight));

                    // resize possibly avoids a bug on some systems
                    GetMythMainWindow()->setGeometry(player_bounds);
                    GetMythMainWindow()->ResizePainterWindow(player_bounds.size());
                }
            }
        }

        // player window sizing
        MythScreenStack *mainStack = GetMythMainWindow()->GetMainStack();

        myWindow = new TvPlayWindow(mainStack, "Playback");

        if (myWindow->Create())
        {
            mainStack->AddScreen(myWindow, false);
            LOG(VB_GENERAL, LOG_INFO, LOC + "Created TvPlayWindow.");
        }
        else
        {
            delete myWindow;
            myWindow = NULL;
        }

        MythMainWindow *mainWindow = GetMythMainWindow();
        if (mainWindow->GetPaintWindow())
            mainWindow->GetPaintWindow()->update();
        mainWindow->installEventFilter(this);
        qApp->processEvents();
    }

    {
        QMutexLocker locker(&initFromDBLock);
        while (!initFromDBDone)
        {
            qApp->processEvents();
            initFromDBWait.wait(&initFromDBLock, 50);
        }
    }

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    mctx->ff_rew_state = 0;
    mctx->ff_rew_index = kInitFFRWSpeed;
    mctx->ff_rew_speed = 0;
    mctx->ts_normal    = 1.0f;
    ReturnPlayerLock(mctx);

    sleep_index = 0;

    SetUpdateOSDPosition(false);

    const PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    ClearInputQueues(ctx, false);
    ReturnPlayerLock(ctx);

    switchToRec = NULL;
    SetExitPlayer(false, false);

    errorRecoveryTimerId = StartTimer(kErrorRecoveryCheckFrequency, __LINE__);
    lcdTimerId           = StartTimer(1, __LINE__);
    speedChangeTimerId   = StartTimer(kSpeedChangeCheckFrequency, __LINE__);

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "Init -- end");
    return true;
}

TV::~TV(void)
{
    LOG(VB_PLAYBACK, LOG_INFO, "TV::~TV() -- begin");

    if (browsehelper)
        browsehelper->Stop();

    gCoreContext->removeListener(this);

    if (GetMythMainWindow() && weDisabledGUI)
        GetMythMainWindow()->PopDrawDisabled();

    if (myWindow)
    {
        myWindow->Close();
        myWindow = NULL;
    }

    LOG(VB_PLAYBACK, LOG_INFO, "TV::~TV() -- lock");

    // restore window to gui size and position
    MythMainWindow* mwnd = GetMythMainWindow();
    mwnd->setGeometry(saved_gui_bounds);
    mwnd->setFixedSize(saved_gui_bounds.size());
    mwnd->ResizePainterWindow(saved_gui_bounds.size());
    mwnd->show();
    if (!db_use_gui_size_for_tv)
        mwnd->move(saved_gui_bounds.topLeft());

    if (lastProgram)
        delete lastProgram;

    if (LCD *lcd = LCD::Get())
    {
        lcd->setFunctionLEDs(FUNC_TV, false);
        lcd->setFunctionLEDs(FUNC_MOVIE, false);
        lcd->switchToTime();
    }

    if (ddMapLoader)
    {
        ddMapLoader->wait();

        if (ddMapSourceId)
        {
            ddMapLoader->SetParent(NULL);
            ddMapLoader->SetSourceID(ddMapSourceId);
            ddMapLoader->setAutoDelete(true);
            MThreadPool::globalInstance()->start(ddMapLoader, "DDLoadMapPost");
        }
        else
        {
            delete ddMapLoader;
        }

        ddMapSourceId = 0;
        ddMapLoader = NULL;
    }

    if (browsehelper)
    {
        delete browsehelper;
        browsehelper = NULL;
    }

    PlayerContext *mctx = GetPlayerWriteLock(0, __FILE__, __LINE__);
    while (!player.empty())
    {
        delete player.back();
        player.pop_back();
    }
    ReturnPlayerLock(mctx);

    if (browsehelper)
    {
        delete browsehelper;
        browsehelper = NULL;
    }

    if (m_ignoreActions)
        delete m_ignoreActions;

    if (m_editActions)
        delete m_editActions;

    if (m_browseActions)
        delete m_browseActions;

    if (m_browsePassActions)
        delete m_browsePassActions;

    if (m_manualZoomActions)
        delete m_manualZoomActions;

    if (m_manualZoomPassActions)
        delete m_manualZoomPassActions;

    if (m_picAttrActions)
        delete m_picAttrActions;

    if (m_timeStretchActions)
        delete m_timeStretchActions;

    if (m_audioSyncActions)
        delete m_audioSyncActions;

    if (m_3dActions)
        delete m_3dActions;

    if (m_activeActions)
        delete m_activeActions;

    if (m_jumpProgramActions)
        delete m_jumpProgramActions;

    if (m_seekActions)
        delete m_seekActions;

    if (m_trackActions)
        delete m_trackActions;

    if (m_ffrwndActions)
        delete m_ffrwndActions;

    if (m_toggleActions)
        delete m_toggleActions;

    if (m_pipActions)
        delete m_pipActions;

    if (m_activePostQActions)
        delete m_activePostQActions;

    if (m_osdActions)
        delete m_osdActions;

    if (m_osdExitActions)
        delete m_osdActions;

    if (m_osdLiveTVActions)
        delete m_osdLiveTVActions;

    if (m_osdPlayingActions)
        delete m_osdPlayingActions;

    if (m_osdDialogActions)
        delete m_osdDialogActions;

    if (m_osdAskAllowActions)
        delete m_osdAskAllowActions;

    if (m_networkControlActions)
        delete m_networkControlActions;

    LOG(VB_PLAYBACK, LOG_INFO, "TV::~TV() -- end");
}

/**
 * \brief The main playback loop
 */
void TV::PlaybackLoop(void)
{
    while (true)
    {
        qApp->processEvents();

        TVState state = GetState(0);
        if ((kState_Error == state) || (kState_None == state))
            return;

        if (kState_ChangingState == state)
            continue;

        int count = player.size();
        for (int i = 0; i < count; i++)
        {
            const PlayerContext *mctx = GetPlayerReadLock(i, __FILE__, __LINE__);
            if (mctx)
            {
                mctx->LockDeletePlayer(__FILE__, __LINE__);
                if (mctx->player && !mctx->player->IsErrored())
                {
                    mctx->player->EventLoop();
                    mctx->player->VideoLoop();
                }
                mctx->UnlockDeletePlayer(__FILE__, __LINE__);
            }
            ReturnPlayerLock(mctx);
        }
    }
}

/**
 * \brief update the channel list with channels from the selected channel group
 */
void TV::UpdateChannelList(int groupID)
{
    if (!db_use_channel_groups)
        return;

    QMutexLocker locker(&channelGroupLock);
    if (groupID == channelGroupId)
        return;

    DBChanList list;
    if (groupID != -1)
    {
        list = ChannelUtil::GetChannels(0, true, "channum, callsign", groupID);
        ChannelUtil::SortChannels(list, "channum", true);
    }

    channelGroupId = groupID;
    channelGroupChannelList = list;

    if (db_remember_last_channel_group)
        gCoreContext->SaveSetting("ChannelGroupDefault", channelGroupId);
}

/**
 * \brief get tv state of active player context
 */
TVState TV::GetState(int player_idx) const
{
    TVState ret = kState_ChangingState;
    const PlayerContext *ctx = GetPlayerReadLock(player_idx, __FILE__, __LINE__);
    ret = GetState(ctx);
    ReturnPlayerLock(ctx);
    return ret;
}

void TV::GetStatus(void)
{
    QVariantMap status;

    const PlayerContext *ctx = GetPlayerReadLock(-1, __FILE__, __LINE__);

    status.insert("state", StateToString(GetState(ctx)));
    ctx->LockPlayingInfo(__FILE__, __LINE__);
    if (ctx->playingInfo)
    {
        status.insert("title", ctx->playingInfo->GetTitle());
        status.insert("subtitle", ctx->playingInfo->GetSubtitle());
        status.insert("starttime",
                           ctx->playingInfo->GetRecordingStartTime()
                           .toUTC().toString(Qt::ISODate));
        status.insert("chanid",
                           QString::number(ctx->playingInfo->GetChanID()));
        status.insert("programid", ctx->playingInfo->GetProgramID());
    }
    ctx->UnlockPlayingInfo(__FILE__, __LINE__);
    osdInfo info;
    ctx->CalcPlayerSliderPosition(info);
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        if (!info.text["totalchapters"].isEmpty())
        {
            QList<long long> chapters;
            ctx->player->GetChapterTimes(chapters);
            QVariantList var;
            foreach (long long chapter, chapters)
                var << QVariant(chapter);
            status.insert("chaptertimes", var);
        }

        uint capmode = ctx->player->GetCaptionMode();
        QVariantMap tracks;

        QStringList list = ctx->player->GetTracks(kTrackTypeSubtitle);
        int currenttrack = -1;
        if (!list.isEmpty() && (kDisplayAVSubtitle == capmode))
            currenttrack = ctx->player->GetTrack(kTrackTypeSubtitle);
        for (int i = 0; i < list.size(); i++)
        {
            if (i == currenttrack)
                status.insert("currentsubtitletrack", list[i]);
            tracks.insert("SELECTSUBTITLE_" + QString::number(i), list[i]);
        }

        list = ctx->player->GetTracks(kTrackTypeTeletextCaptions);
        currenttrack = -1;
        if (!list.isEmpty() && (kDisplayTeletextCaptions == capmode))
            currenttrack = ctx->player->GetTrack(kTrackTypeTeletextCaptions);
        for (int i = 0; i < list.size(); i++)
        {
            if (i == currenttrack)
                status.insert("currentsubtitletrack", list[i]);
            tracks.insert("SELECTTTC_" + QString::number(i), list[i]);
        }

        list = ctx->player->GetTracks(kTrackTypeCC708);
        currenttrack = -1;
        if (!list.isEmpty() && (kDisplayCC708 == capmode))
            currenttrack = ctx->player->GetTrack(kTrackTypeCC708);
        for (int i = 0; i < list.size(); i++)
        {
            if (i == currenttrack)
                status.insert("currentsubtitletrack", list[i]);
            tracks.insert("SELECTCC708_" + QString::number(i), list[i]);
        }

        list = ctx->player->GetTracks(kTrackTypeCC608);
        currenttrack = -1;
        if (!list.isEmpty() && (kDisplayCC608 == capmode))
            currenttrack = ctx->player->GetTrack(kTrackTypeCC608);
        for (int i = 0; i < list.size(); i++)
        {
            if (i == currenttrack)
                status.insert("currentsubtitletrack", list[i]);
            tracks.insert("SELECTCC608_" + QString::number(i), list[i]);
        }

        list = ctx->player->GetTracks(kTrackTypeRawText);
        currenttrack = -1;
        if (!list.isEmpty() && (kDisplayRawTextSubtitle == capmode))
            currenttrack = ctx->player->GetTrack(kTrackTypeRawText);
        for (int i = 0; i < list.size(); i++)
        {
            if (i == currenttrack)
                status.insert("currentsubtitletrack", list[i]);
            tracks.insert("SELECTRAWTEXT_" + QString::number(i), list[i]);
        }

        if (ctx->player->HasTextSubtitles())
        {
            if (kDisplayTextSubtitle == capmode)
                status.insert("currentsubtitletrack", tr("External Subtitles"));
            tracks.insert(ACTION_ENABLEEXTTEXT, tr("External Subtitles"));
        }

        status.insert("totalsubtitletracks", tracks.size());
        if (!tracks.isEmpty())
            status.insert("subtitletracks", tracks);

        tracks.clear();
        list = ctx->player->GetTracks(kTrackTypeAudio);
        currenttrack = ctx->player->GetTrack(kTrackTypeAudio);
        for (int i = 0; i < list.size(); i++)
        {
            if (i == currenttrack)
                status.insert("currentaudiotrack", list[i]);
            tracks.insert("SELECTAUDIO_" + QString::number(i), list[i]);
        }

        status.insert("totalaudiotracks", tracks.size());
        if (!tracks.isEmpty())
            status.insert("audiotracks", tracks);

        status.insert("playspeed", ctx->player->GetPlaySpeed());
        status.insert("audiosyncoffset", (long long)ctx->player->GetAudioTimecodeOffset());
        if (ctx->player->GetAudio()->ControlsVolume())
        {
            status.insert("volume", ctx->player->GetVolume());
            status.insert("mute",   ctx->player->GetMuteState());
        }
        if (ctx->player->GetVideoOutput())
        {
            VideoOutput *vo = ctx->player->GetVideoOutput();
            PictureAttributeSupported supp =
                    vo->GetSupportedPictureAttributes();
            if (supp & kPictureAttributeSupported_Brightness)
            {
                status.insert("brightness",
                  vo->GetPictureAttribute(kPictureAttribute_Brightness));
            }
            if (supp & kPictureAttributeSupported_Brightness)
            {
                status.insert("contrast",
                  vo->GetPictureAttribute(kPictureAttribute_Contrast));
            }
            if (supp & kPictureAttributeSupported_Brightness)
            {
                status.insert("colour",
                  vo->GetPictureAttribute(kPictureAttribute_Colour));
            }
            if (supp & kPictureAttributeSupported_Brightness)
            {
                status.insert("hue",
                  vo->GetPictureAttribute(kPictureAttribute_Hue));
            }
            if (supp & kPictureAttributeSupported_StudioLevels)
            {
                status.insert("studiolevels",
                  vo->GetPictureAttribute(kPictureAttribute_StudioLevels));
            }
        }
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    ReturnPlayerLock(ctx);

    QHashIterator<QString,QString> tit(info.text);
    while (tit.hasNext())
    {
        tit.next();
        status.insert(tit.key(), tit.value());
    }

    QHashIterator<QString,int> vit(info.values);
    while (vit.hasNext())
    {
        vit.next();
        status.insert(vit.key(), vit.value());
    }

    MythUIStateTracker::SetState(status);
}

/**
 * \brief get tv state of active player context
 */
TVState TV::GetState(const PlayerContext *actx) const
{
    TVState ret = kState_ChangingState;
    if (!actx->InStateChange())
        ret = actx->GetState();
    return ret;
}

/** \fn TV::LiveTV(bool,bool)
 *  \brief Starts LiveTV
 *  \param showDialogs if true error dialogs are shown, if false they are not
 */
bool TV::LiveTV(bool showDialogs)
{
    requestDelete = false;
    allowRerecord = false;
    jumpToProgram = false;

    PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    if (actx->GetState() == kState_None &&
        RequestNextRecorder(actx, showDialogs))
    {
        actx->SetInitialTVState(true);
        HandleStateChange(actx, actx);
        switchToRec = NULL;

        // Start Idle Timer
        if (db_idle_timeout > 0)
        {
            idleTimerId = StartTimer(db_idle_timeout, __LINE__);
            LOG(VB_GENERAL, LOG_INFO, QString("Using Idle Timer. %1 minutes")
                    .arg(db_idle_timeout*(1.0f/60000.0f)));
        }

        ReturnPlayerLock(actx);
        return true;
    }
    ReturnPlayerLock(actx);
    return false;
}

int TV::GetLastRecorderNum(int player_idx) const
{
    const PlayerContext *ctx = GetPlayerReadLock(player_idx, __FILE__, __LINE__);
    int ret = ctx->GetCardID();
    ReturnPlayerLock(ctx);
    return ret;
}

bool TV::RequestNextRecorder(PlayerContext *ctx, bool showDialogs)
{
    if (!ctx)
        return false;

    ctx->SetRecorder(NULL);

    RemoteEncoder *testrec = NULL;
    if (switchToRec)
    {
        // If this is set we, already got a new recorder in SwitchCards()
        testrec = switchToRec;
        switchToRec = NULL;
    }
    else
    {
        // When starting LiveTV we just get the next free recorder
        testrec = RemoteRequestNextFreeRecorder(-1);
    }

    if (!testrec)
        return false;

    if (!testrec->IsValidRecorder())
    {
        if (showDialogs)
            ShowNoRecorderDialog(ctx);

        delete testrec;

        return false;
    }

    ctx->SetRecorder(testrec);

    return true;
}

void TV::FinishRecording(int player_ctx)
{
    PlayerContext *ctx = GetPlayerReadLock(player_ctx, __FILE__, __LINE__);
    if (StateIsRecording(GetState(ctx)) && ctx->recorder)
        ctx->recorder->FinishRecording();
    ReturnPlayerLock(ctx);
}

void TV::AskAllowRecording(PlayerContext *ctx,
                           const QStringList &msg, int timeuntil,
                           bool hasrec, bool haslater)
{
#if 0
    LOG(VB_GENERAL, LOG_DEBUG, LOC + "AskAllowRecording");
#endif
    if (!StateIsLiveTV(GetState(ctx)))
       return;

    ProgramInfo *info = new ProgramInfo(msg);
    if (!info->GetChanID())
    {
        delete info;
        return;
    }

    QMutexLocker locker(&askAllowLock);
    QString key = info->MakeUniqueKey();
    if (timeuntil > 0)
    {
        // add program to list
#if 0
        LOG(VB_GENERAL, LOG_DEBUG, LOC + "AskAllowRecording -- " +
            QString("adding '%1'").arg(info->title));
#endif
        QDateTime expiry = QDateTime::currentDateTime().addSecs(timeuntil);
        askAllowPrograms[key] = AskProgramInfo(expiry, hasrec, haslater, info);
    }
    else
    {
        // remove program from list
        LOG(VB_GENERAL, LOG_INFO, LOC + "AskAllowRecording -- " +
            QString("removing '%1'").arg(info->GetTitle()));
        QMap<QString,AskProgramInfo>::iterator it = askAllowPrograms.find(key);
        if (it != askAllowPrograms.end())
        {
            delete (*it).info;
            askAllowPrograms.erase(it);
        }
        delete info;
    }

    ShowOSDAskAllow(ctx);
}

void TV::ShowOSDAskAllow(PlayerContext *ctx)
{
    QMutexLocker locker(&askAllowLock);
    if (!ctx->recorder)
        return;

    uint cardid = ctx->GetCardID();

    QString single_rec =
        tr("MythTV wants to record \"%1\" on %2 in %d seconds. "
           "Do you want to:");

    QString record_watch  = tr("Record and watch while it records");
    QString let_record1   = tr("Let it record and go back to the Main Menu");
    QString let_recordm   = tr("Let them record and go back to the Main Menu");
    QString record_later1 = tr("Record it later, I want to watch TV");
    QString record_laterm = tr("Record them later, I want to watch TV");
    QString do_not_record1= tr("Don't let it record, I want to watch TV");
    QString do_not_recordm= tr("Don't let them record, I want to watch TV");

    // eliminate timed out programs
    QDateTime timeNow = QDateTime::currentDateTime();
    QMap<QString,AskProgramInfo>::iterator it = askAllowPrograms.begin();
    QMap<QString,AskProgramInfo>::iterator next = it;
    while (it != askAllowPrograms.end())
    {
        next = it; ++next;
        if ((*it).expiry <= timeNow)
        {
#if 0
            LOG(VB_GENERAL, LOG_DEBUG, LOC + "UpdateOSDAskAllowDialog -- " +
                QString("removing '%1'").arg((*it).info->title));
#endif
            delete (*it).info;
            askAllowPrograms.erase(it);
        }
        it = next;
    }
    int          timeuntil = 0;
    QString      message   = QString::null;
    uint conflict_count = askAllowPrograms.size();

    it = askAllowPrograms.begin();
    if ((1 == askAllowPrograms.size()) && ((*it).info->GetCardID() == cardid))
    {
        (*it).is_in_same_input_group = (*it).is_conflicting = true;
    }
    else if (!askAllowPrograms.empty())
    {
        // get the currently used input on our card
        bool busy_input_grps_loaded = false;
        vector<uint> busy_input_grps;
        TunedInputInfo busy_input;
        RemoteIsBusy(cardid, busy_input);

        // check if current input can conflict
        it = askAllowPrograms.begin();
        for (; it != askAllowPrograms.end(); ++it)
        {
            (*it).is_in_same_input_group =
                (cardid == (*it).info->GetCardID());

            if ((*it).is_in_same_input_group)
                continue;

            // is busy_input in same input group as recording
            if (!busy_input_grps_loaded)
            {
                busy_input_grps = CardUtil::GetInputGroups(busy_input.inputid);
                busy_input_grps_loaded = true;
            }

            vector<uint> input_grps =
                CardUtil::GetInputGroups((*it).info->GetInputID());

            for (uint i = 0; i < input_grps.size(); i++)
            {
                if (find(busy_input_grps.begin(), busy_input_grps.end(),
                         input_grps[i]) !=  busy_input_grps.end())
                {
                    (*it).is_in_same_input_group = true;
                    break;
                }
            }
        }

        // check if inputs that can conflict are ok
        conflict_count = 0;
        it = askAllowPrograms.begin();
        for (; it != askAllowPrograms.end(); ++it)
        {
            if (!(*it).is_in_same_input_group)
                (*it).is_conflicting = false;
            else if ((cardid == (uint)(*it).info->GetCardID()))
                (*it).is_conflicting = true;
            else if (!CardUtil::IsTunerShared(cardid, (*it).info->GetCardID()))
                (*it).is_conflicting = true;
            else if ((busy_input.sourceid == (uint)(*it).info->GetSourceID()) &&
                     (busy_input.mplexid  == (uint)(*it).info->QueryMplexID()))
                (*it).is_conflicting = false;
            else
                (*it).is_conflicting = true;

            conflict_count += (*it).is_conflicting ? 1 : 0;
        }
    }

    it = askAllowPrograms.begin();
    for (; it != askAllowPrograms.end() && !(*it).is_conflicting; ++it);

    if (conflict_count == 0)
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + "The scheduler wants to make "
                                        "a non-conflicting recording.");
        // TODO take down mplexid and inform user of problem
        // on channel changes.
    }
    else if (conflict_count == 1 && ((*it).info->GetCardID() == cardid))
    {
#if 0
        LOG(VB_GENERAL, LOG_DEBUG, LOC + "UpdateOSDAskAllowDialog -- " +
            "kAskAllowOneRec");
#endif

        it = askAllowPrograms.begin();

        QString channel = db_channel_format;
        channel
            .replace("<num>",  (*it).info->GetChanNum())
            .replace("<sign>", (*it).info->GetChannelSchedulingID())
            .replace("<name>", (*it).info->GetChannelName());

        message = single_rec.arg((*it).info->GetTitle()).arg(channel);

        OSD *osd = GetOSDLock(ctx);
        if (osd)
        {
            browsehelper->BrowseEnd(ctx, false);
            timeuntil = QDateTime::currentDateTime().secsTo((*it).expiry) * 1000;
            osd->DialogShow(OSD_DLG_ASKALLOW, message, timeuntil);
            osd->DialogAddButton(record_watch, "DIALOG_ASKALLOW_WATCH_0",
                                 false, !((*it).has_rec));
            osd->DialogAddButton(let_record1, "DIALOG_ASKALLOW_EXIT_0");
            osd->DialogAddButton(((*it).has_later) ? record_later1 : do_not_record1,
                                 "DIALOG_ASKALLOW_CANCELRECORDING_0",
                                 false, ((*it).has_rec));
        }
        ReturnOSDLock(ctx, osd);
    }
    else
    {
        if (conflict_count > 1)
        {
            message = QObject::tr(
                "MythTV wants to record these programs in %d seconds:");
            message += "\n";
        }

        bool has_rec = false;
        it = askAllowPrograms.begin();
        for (; it != askAllowPrograms.end(); ++it)
        {
            if (!(*it).is_conflicting)
                continue;

            QString title = (*it).info->GetTitle();
            if ((title.length() < 10) && !(*it).info->GetSubtitle().isEmpty())
                title += ": " + (*it).info->GetSubtitle();
            if (title.length() > 20)
                title = title.left(17) + "...";

            QString channel = db_channel_format;
            channel
                .replace("<num>",  (*it).info->GetChanNum())
                .replace("<sign>", (*it).info->GetChannelSchedulingID())
                .replace("<name>", (*it).info->GetChannelName());

            if (conflict_count > 1)
            {
                message += QObject::tr("\"%1\" on %2").arg(title).arg(channel);
                message += "\n";
            }
            else
            {
                message = single_rec.arg((*it).info->GetTitle()).arg(channel);
                has_rec = (*it).has_rec;
            }
        }

        if (conflict_count > 1)
        {
            message += "\n";
            message += QObject::tr("Do you want to:");
        }

        bool all_have_later = true;
        timeuntil = 9999999;
        it = askAllowPrograms.begin();
        for (; it != askAllowPrograms.end(); ++it)
        {
            if ((*it).is_conflicting)
            {
                all_have_later &= (*it).has_later;
                int tmp = QDateTime::currentDateTime().secsTo((*it).expiry);
                tmp *= 1000;
                timeuntil = min(timeuntil, max(tmp, 0));
            }
        }
        timeuntil = (9999999 == timeuntil) ? 0 : timeuntil;

        OSD *osd = GetOSDLock(ctx);
        if (osd && conflict_count > 1)
        {
            browsehelper->BrowseEnd(ctx, false);
            osd->DialogShow(OSD_DLG_ASKALLOW, message, timeuntil);
            osd->DialogAddButton(let_recordm, "DIALOG_ASKALLOW_EXIT_0",
                                 false, true);
            osd->DialogAddButton((all_have_later) ? record_laterm : do_not_recordm,
                                 "DIALOG_ASKALLOW_CANCELCONFLICTING_0");
        }
        else if (osd)
        {
            browsehelper->BrowseEnd(ctx, false);
            osd->DialogShow(OSD_DLG_ASKALLOW, message, timeuntil);
            osd->DialogAddButton(let_record1, "DIALOG_ASKALLOW_EXIT_0",
                                 false, !has_rec);
            osd->DialogAddButton((all_have_later) ? record_later1 : do_not_record1,
                                 "DIALOG_ASKALLOW_CANCELRECORDING_0",
                                 false, has_rec);
        }
        ReturnOSDLock(ctx, osd);
    }
}

static struct ActionDefStruct<TV> toaahActions[] = {
    { "CANCELRECORDING",   &TV::doOSDAskAllowCancelRecord },
    { "CANCELCONFLICTING", &TV::doOSDAskAllowCancelConflict },
    { "WATCH",             &TV::doOSDAskAllowWatch },
    { "EXIT",              &TV::doOSDAskAllowExit }
};
static int toaahActionCount = NELEMS(toaahActions);

bool TV::doOSDAskAllowCancelRecord(const QString &action)
{
    if (m_actionOSDContext->recorder)
        m_actionOSDContext->recorder->CancelNextRecording(true);
    return true;
}

bool TV::doOSDAskAllowCancelConflict(const QString &action)
{
    QMap<QString,AskProgramInfo>::iterator it = askAllowPrograms.begin();
    for (; it != askAllowPrograms.end(); ++it)
    {
        if ((*it).is_conflicting)
            RemoteCancelNextRecording((*it).info->GetCardID(), true);
    }
    return true;
}

bool TV::doOSDAskAllowWatch(const QString &action)
{
    if (m_actionOSDContext->recorder)
        m_actionOSDContext->recorder->CancelNextRecording(false);
    return true;
}

bool TV::doOSDAskAllowExit(const QString &action)
{
    PrepareToExitPlayer(m_actionOSDContext, __LINE__);
    SetExitPlayer(true, true);
    return true;
}

void TV::HandleOSDAskAllow(PlayerContext *ctx, QString action)
{
    if (!DialogIsVisible(ctx, OSD_DLG_ASKALLOW))
        return;

    if (!askAllowLock.tryLock())
    {
        LOG(VB_GENERAL, LOG_ERR, "allowrecordingbox : askAllowLock is locked");
        return;
    }

    m_actionOSDContext = ctx;
    if (!m_osdAskAllowActions)
        m_osdAskAllowActions = new MythActions<TV>(this, toaahActions,
                                                   toaahActionCount);
    bool touched;
    m_osdAskAllowActions->handleAction(action, touched);

    askAllowLock.unlock();
}

int TV::Playback(const ProgramInfo &rcinfo)
{
    wantsToQuit   = false;
    jumpToProgram = false;
    allowRerecord = false;
    requestDelete = false;

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    if (mctx->GetState() != kState_None)
    {
        ReturnPlayerLock(mctx);
        return 0;
    }

    mctx->SetPlayingInfo(&rcinfo);
    mctx->SetInitialTVState(false);
    HandleStateChange(mctx, mctx);

    ReturnPlayerLock(mctx);

    if (LCD *lcd = LCD::Get())
    {
        lcd->switchToChannel(rcinfo.GetChannelSchedulingID(),
                             rcinfo.GetTitle(), rcinfo.GetSubtitle());
        lcd->setFunctionLEDs((rcinfo.IsRecording())?FUNC_TV:FUNC_MOVIE, true);
    }

    return 1;
}

bool TV::StateIsRecording(TVState state)
{
    return (state == kState_RecordingOnly ||
            state == kState_WatchingRecording);
}

bool TV::StateIsPlaying(TVState state)
{
    return (state == kState_WatchingPreRecorded ||
            state == kState_WatchingRecording   ||
            state == kState_WatchingVideo       ||
            state == kState_WatchingDVD         ||
            state == kState_WatchingBD);
}

bool TV::StateIsLiveTV(TVState state)
{
    return (state == kState_WatchingLiveTV);
}

TVState TV::RemoveRecording(TVState state)
{
    if (StateIsRecording(state))
    {
        if (state == kState_RecordingOnly)
            return kState_None;
        return kState_WatchingPreRecorded;
    }
    return kState_Error;
}

#define TRANSITION(ASTATE,BSTATE) \
   ((ctxState == ASTATE) && (desiredNextState == BSTATE))

#define SET_NEXT() do { nextState = desiredNextState; changed = true; } while(0)
#define SET_LAST() do { nextState = ctxState; changed = true; } while(0)

static QString tv_i18n(const QString &msg)
{
    QByteArray msg_arr = msg.toLatin1();
    QString msg_i18n = QObject::tr(msg_arr.constData());
    QByteArray msg_i18n_arr = msg_i18n.toLatin1();
    return (msg_arr == msg_i18n_arr) ? msg_i18n : msg;
}

/** \fn TV::HandleStateChange(PlayerContext*,PlayerContext*)
 *  \brief Changes the state to the state on the front of the
 *         state change queue.
 *
 *   Note: There must exist a state transition from any state we can enter
 *   to  the kState_None state, as this is used to shutdown TV in RunTV.
 *
 */
void TV::HandleStateChange(PlayerContext *mctx, PlayerContext *ctx)
{
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("HandleStateChange(%1) -- begin")
            .arg(find_player_index(ctx)));

    if (ctx->IsErrored())
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            "HandleStateChange(): Called after fatal error detected.");
        return;
    }

    bool changed = false;

    ctx->LockState();
    TVState nextState = ctx->GetState();
    if (ctx->nextState.empty())
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            "HandleStateChange() Warning, called with no state to change to.");
        ctx->UnlockState();
        return;
    }

    TVState ctxState = ctx->GetState();
    TVState desiredNextState = ctx->DequeueNextState();

    LOG(VB_GENERAL, LOG_INFO, LOC +
        QString("Attempting to change from %1 to %2")
            .arg(StateToString(nextState))
            .arg(StateToString(desiredNextState)));

    if (desiredNextState == kState_Error)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "HandleStateChange(): "
                "Attempting to set to an error state!");
        SetErrored(ctx);
        ctx->UnlockState();
        return;
    }

    bool ok = false;
    if (TRANSITION(kState_None, kState_WatchingLiveTV))
    {
        QString name = "";

        ctx->lastSignalUIInfo.clear();

        ctx->recorder->Setup();

        QDateTime timerOffTime = QDateTime::currentDateTime();
        lockTimerOn = false;

        SET_NEXT();

        uint chanid = gCoreContext->GetNumSetting("DefaultChanid", 0);

        if (chanid && !IsTunable(ctx, chanid))
            chanid = 0;

        QString channum = "";

        if (chanid)
        {
            QStringList reclist;

            MSqlQuery query(MSqlQuery::InitCon());
            query.prepare("SELECT channum FROM channel "
                          "WHERE chanid = :CHANID");
            query.bindValue(":CHANID", chanid);
            if (query.exec() && query.isActive() && query.size() > 0 && query.next())
                channum = query.value(0).toString();
            else
                channum = QString::number(chanid);

            bool getit = ctx->recorder->ShouldSwitchToAnotherCard(
                QString::number(chanid));

            if (getit)
                reclist = ChannelUtil::GetValidRecorderList(chanid, channum);

            if (reclist.size())
            {
                RemoteEncoder *testrec = NULL;
                testrec = RemoteRequestFreeRecorderFromList(reclist);
                if (testrec && testrec->IsValidRecorder())
                {
                    ctx->SetRecorder(testrec);
                    ctx->recorder->Setup();
                }
            }
            else if (getit)
                chanid = 0;
        }

        LOG(VB_GENERAL, LOG_NOTICE, LOC + "Spawning LiveTV Recorder -- begin");

        if (chanid && !channum.isEmpty())
            ctx->recorder->SpawnLiveTV(ctx->tvchain->GetID(), false, channum);
        else
            ctx->recorder->SpawnLiveTV(ctx->tvchain->GetID(), false, "");

        LOG(VB_GENERAL, LOG_NOTICE, LOC + "Spawning LiveTV Recorder -- end");

        if (!ctx->ReloadTVChain())
        {
            LOG(VB_GENERAL, LOG_ERR, LOC +
                    "HandleStateChange(): LiveTV not successfully started");
            RestoreScreenSaver(ctx);
            ctx->SetRecorder(NULL);
            SetErrored(ctx);
            SET_LAST();
        }
        else
        {
            ctx->LockPlayingInfo(__FILE__, __LINE__);
            QString playbackURL = ctx->playingInfo->GetPlaybackURL(true);
            ctx->UnlockPlayingInfo(__FILE__, __LINE__);

            bool opennow = (ctx->tvchain->GetCardType(-1) != "DUMMY");

            LOG(VB_GENERAL, LOG_INFO, LOC +
                QString("playbackURL(%1) cardtype(%2)")
                    .arg(playbackURL).arg(ctx->tvchain->GetCardType(-1)));

            ctx->SetRingBuffer(
                RingBuffer::Create(
                    playbackURL, false, true,
                    opennow ? RingBuffer::kLiveTVOpenTimeout : -1));

            ctx->buffer->SetLiveMode(ctx->tvchain);
        }


        if (ctx->playingInfo && StartRecorder(ctx,-1))
        {
            ok = StartPlayer(mctx, ctx, desiredNextState);
        }
        if (!ok)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "LiveTV not successfully started");
            RestoreScreenSaver(ctx);
            ctx->SetRecorder(NULL);
            SetErrored(ctx);
            SET_LAST();
        }
        else if (!ctx->IsPIP())
        {
            if (!lastLockSeenTime.isValid() ||
                (lastLockSeenTime < timerOffTime))
            {
                lockTimer.start();
                lockTimerOn = true;
            }
        }

        if (mctx != ctx)
            SetActive(ctx, find_player_index(ctx), false);
    }
    else if (TRANSITION(kState_WatchingLiveTV, kState_None))
    {
        SET_NEXT();
        RestoreScreenSaver(ctx);
        StopStuff(mctx, ctx, true, true, true);

        if ((mctx != ctx) && (GetPlayer(ctx,-1) == ctx))
            SetActive(mctx, 0, true);
    }
    else if (TRANSITION(kState_WatchingRecording, kState_WatchingPreRecorded))
    {
        SET_NEXT();
    }
    else if (TRANSITION(kState_None, kState_WatchingPreRecorded) ||
             TRANSITION(kState_None, kState_WatchingVideo) ||
             TRANSITION(kState_None, kState_WatchingDVD)   ||
             TRANSITION(kState_None, kState_WatchingBD)    ||
             TRANSITION(kState_None, kState_WatchingRecording))
    {
        ctx->LockPlayingInfo(__FILE__, __LINE__);
        QString playbackURL = ctx->playingInfo->GetPlaybackURL(true);
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);

        ctx->SetRingBuffer(RingBuffer::Create(playbackURL, false));

        if (ctx->buffer && ctx->buffer->IsOpen())
        {
            if (desiredNextState == kState_WatchingRecording)
            {
                ctx->LockPlayingInfo(__FILE__, __LINE__);
                RemoteEncoder *rec = RemoteGetExistingRecorder(
                    ctx->playingInfo);
                ctx->UnlockPlayingInfo(__FILE__, __LINE__);

                ctx->SetRecorder(rec);

                if (!ctx->recorder ||
                    !ctx->recorder->IsValidRecorder())
                {
                    LOG(VB_GENERAL, LOG_ERR, LOC +
                        "Couldn't find recorder for in-progress recording");
                    desiredNextState = kState_WatchingPreRecorded;
                    ctx->SetRecorder(NULL);
                }
                else
                {
                    ctx->recorder->Setup();
                }
            }

            ok = StartPlayer(mctx, ctx, desiredNextState);

            if (ok)
            {
                SET_NEXT();

                ctx->LockPlayingInfo(__FILE__, __LINE__);
                if (ctx->playingInfo->IsRecording())
                {
                    QString message = "COMMFLAG_REQUEST ";
                    message += ctx->playingInfo->MakeUniqueKey();
                    gCoreContext->SendMessage(message);
                }
                ctx->UnlockPlayingInfo(__FILE__, __LINE__);
            }
        }

        if (!ok)
        {
            SET_LAST();
            SetErrored(ctx);
        }
        else if (mctx != ctx)
        {
            SetActive(ctx, find_player_index(ctx), false);
        }
    }
    else if (TRANSITION(kState_WatchingPreRecorded, kState_None) ||
             TRANSITION(kState_WatchingVideo, kState_None)       ||
             TRANSITION(kState_WatchingDVD, kState_None)         ||
             TRANSITION(kState_WatchingBD, kState_None)          ||
             TRANSITION(kState_WatchingRecording, kState_None))
    {
        SET_NEXT();

        RestoreScreenSaver(ctx);
        StopStuff(mctx, ctx, true, true, false);

        if ((mctx != ctx) && (GetPlayer(ctx,-1) == ctx))
            SetActive(mctx, 0, true);
    }
    else if (TRANSITION(kState_None, kState_None))
    {
        SET_NEXT();
    }

    // Print state changed message...
    if (!changed)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            QString("Unknown state transition: %1 to %2")
                .arg(StateToString(ctx->GetState()))
                .arg(StateToString(desiredNextState)));
    }
    else if (ctx->GetState() != nextState)
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + QString("Changing from %1 to %2")
                .arg(StateToString(ctx->GetState()))
                .arg(StateToString(nextState)));
    }

    // update internal state variable
    TVState lastState = ctx->GetState();
    ctx->playingState = nextState;
    ctx->UnlockState();

    if (mctx == ctx)
    {
        if (StateIsLiveTV(ctx->GetState()))
        {
            LOG(VB_GENERAL, LOG_INFO, LOC + "State is LiveTV & mctx == ctx");
            UpdateOSDInput(ctx);
            LOG(VB_GENERAL, LOG_INFO, LOC + "UpdateOSDInput done");
            UpdateLCD();
            LOG(VB_GENERAL, LOG_INFO, LOC + "UpdateLCD done");
            ITVRestart(ctx, true);
            LOG(VB_GENERAL, LOG_INFO, LOC + "ITVRestart done");
        }
        else if (StateIsPlaying(ctx->GetState()) && lastState == kState_None)
        {
            ctx->LockPlayingInfo(__FILE__, __LINE__);
            int count = PlayGroup::GetCount();
            QString msg = tr("%1 Settings")
                    .arg(tv_i18n(ctx->playingInfo->GetPlaybackGroup()));
            ctx->UnlockPlayingInfo(__FILE__, __LINE__);
            if (count > 0)
                SetOSDMessage(ctx, msg);
            ITVRestart(ctx, false);
        }

        if (ctx->buffer && ctx->buffer->IsDVD())
        {
            UpdateLCD();
        }

        if (ctx->recorder)
            ctx->recorder->FrontendReady();

        QMutexLocker locker(&timerIdLock);
        if (endOfRecPromptTimerId)
            KillTimer(endOfRecPromptTimerId);
        endOfRecPromptTimerId = 0;
        if (db_end_of_rec_exit_prompt && !inPlaylist && !underNetworkControl)
        {
            endOfRecPromptTimerId =
                StartTimer(kEndOfRecPromptCheckFrequency, __LINE__);
        }

        if (endOfPlaybackTimerId)
            KillTimer(endOfPlaybackTimerId);
        endOfPlaybackTimerId = 0;

        if (StateIsPlaying(ctx->GetState()))
        {
            endOfPlaybackTimerId =
                StartTimer(kEndOfPlaybackFirstCheckTimer, __LINE__);

        }

    }

    if (TRANSITION(kState_None, kState_WatchingPreRecorded) ||
             TRANSITION(kState_None, kState_WatchingVideo) ||
             TRANSITION(kState_None, kState_WatchingDVD)   ||
             TRANSITION(kState_None, kState_WatchingBD)    ||
             TRANSITION(kState_None, kState_WatchingRecording) ||
             TRANSITION(kState_None, kState_WatchingLiveTV))
    {
        if (!ctx->IsPIP())
            GetMythUI()->DisableScreensaver();
        MythMainWindow *mainWindow = GetMythMainWindow();
        mainWindow->setBaseSize(player_bounds.size());
        mainWindow->setMinimumSize(
            (db_use_fixed_size) ? player_bounds.size() : QSize(16, 16));
        mainWindow->setMaximumSize(
            (db_use_fixed_size) ? player_bounds.size() :
            QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        mainWindow->setGeometry(player_bounds);
        mainWindow->ResizePainterWindow(player_bounds.size());
        if (!weDisabledGUI)
        {
            weDisabledGUI = true;
            GetMythMainWindow()->PushDrawDisabled();
        }
        DrawUnusedRects();
        // we no longer need the contents of myWindow
        if (myWindow)
            myWindow->DeleteAllChildren();

        LOG(VB_GENERAL, LOG_INFO, LOC + "Main UI disabled.");
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("HandleStateChange(%1) -- end")
            .arg(find_player_index(ctx)));
}
#undef TRANSITION
#undef SET_NEXT
#undef SET_LAST

/** \fn TV::StartRecorder(PlayerContext*, int)
 *  \brief Starts recorder, must be called before StartPlayer().
 *  \param maxWait How long to wait for RecorderBase to start recording.
 *  \return true when successful, false otherwise.
 */
bool TV::StartRecorder(PlayerContext *ctx, int maxWait)
{
    RemoteEncoder *rec = ctx->recorder;
    maxWait = (maxWait <= 0) ? 40000 : maxWait;
    MythTimer t;
    t.start();
    bool recording = false, ok = true;
    if (!rec) {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Invalid Remote Encoder");
        SetErrored(ctx);
        return false;
    }
    while (!(recording = rec->IsRecording(&ok)) &&
           !exitPlayerTimerId && t.elapsed() < maxWait)
    {
        if (!ok)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "StartRecorder() -- "
                    "lost contact with backend");
            SetErrored(ctx);
            return false;
        }
        usleep(5000);
    }

    if (!recording || exitPlayerTimerId)
    {
        if (!exitPlayerTimerId)
            LOG(VB_GENERAL, LOG_ERR, LOC + "StartRecorder() -- "
                    "timed out waiting for recorder to start");
        return false;
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("StartRecorder(): took %1 ms to start recorder.")
            .arg(t.elapsed()));

    return true;
}

/** \fn TV::StopStuff(PlayerContext*,PlayerContext*, bool, bool, bool)
 *  \brief Can shut down the ringbuffers, the players, and in LiveTV it can
 *         shut down the recorders.
 *
 *   The player needs to be partially shutdown before the recorder,
 *   and partially shutdown after the recorder. Hence these are shutdown
 *   from within the same method. Also, shutting down things in the right
 *   order avoids spewing error messages...
 *
 *  \param stopRingBuffer Set to true if ringbuffer must be shut down.
 *  \param stopPlayer     Set to true if player must be shut down.
 *  \param stopRecorder   Set to true if recorder must be shut down.
 */
void TV::StopStuff(PlayerContext *mctx, PlayerContext *ctx,
                   bool stopRingBuffer, bool stopPlayer, bool stopRecorder)
{
    LOG(VB_PLAYBACK, LOG_INFO,
        LOC + QString("StopStuff() for player ctx %1 -- begin")
            .arg(find_player_index(ctx)));

    SetActive(mctx, 0, false);

    if (ctx->buffer)
        ctx->buffer->IgnoreWaitStates(true);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (stopPlayer)
        ctx->StopPlaying();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (stopRingBuffer)
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "StopStuff(): stopping ring buffer");
        if (ctx->buffer)
        {
            ctx->buffer->StopReads();
            ctx->buffer->Pause();
            ctx->buffer->WaitForPause();
        }
    }

    if (stopPlayer)
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "StopStuff(): stopping player");
        if (ctx == mctx)
        {
            for (uint i = 1; mctx && (i < player.size()); i++)
                StopStuff(mctx, GetPlayer(mctx,i), true, true, true);
        }
    }

    if (stopRecorder)
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "StopStuff(): stopping recorder");
        if (ctx->recorder)
            ctx->recorder->StopLiveTV();
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "StopStuff() -- end");
}

void TV::TeardownPlayer(PlayerContext *mctx, PlayerContext *ctx)
{
    int ctx_index = find_player_index(ctx);

    QString loc = LOC + QString("TeardownPlayer() player ctx %1")
        .arg(ctx_index);

    if (!mctx || !ctx || ctx_index < 0)
    {
        LOG(VB_GENERAL, LOG_ERR, loc + "-- error");
        return;
    }

    LOG(VB_PLAYBACK, LOG_INFO, loc);

    if (mctx != ctx)
    {
        if (ctx->HasPlayer())
        {
            PIPRemovePlayer(mctx, ctx);
            ctx->SetPlayer(NULL);
        }

        player.erase(player.begin() + ctx_index);
        delete ctx;
        if (mctx->IsPBP())
            PBPRestartMainPlayer(mctx);
        SetActive(mctx, playerActive, false);
        return;
    }

    ctx->TeardownPlayer();
}

void TV::timerEvent(QTimerEvent *te)
{
    const int timer_id = te->timerId();

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    if (mctx->IsErrored())
    {
        ReturnPlayerLock(mctx);
        return;
    }
    ReturnPlayerLock(mctx);

    bool ignore = false;
    {
        QMutexLocker locker(&timerIdLock);
        ignore =
            (stateChangeTimerId.size() &&
             stateChangeTimerId.find(timer_id) == stateChangeTimerId.end());
    }
    if (ignore)
        return; // Always handle state changes first...

    bool handled = true;
    if (timer_id == lcdTimerId)
        HandleLCDTimerEvent();
    else if (timer_id == lcdVolumeTimerId)
        HandleLCDVolumeTimerEvent();
    else if (timer_id == sleepTimerId)
        ShowOSDSleep();
    else if (timer_id == sleepDialogTimerId)
        SleepDialogTimeout();
    else if (timer_id == idleTimerId)
        ShowOSDIdle();
    else if (timer_id == idleDialogTimerId)
        IdleDialogTimeout();
    else if (timer_id == endOfPlaybackTimerId)
        HandleEndOfPlaybackTimerEvent();
    else if (timer_id == embedCheckTimerId)
        HandleIsNearEndWhenEmbeddingTimerEvent();
    else if (timer_id == endOfRecPromptTimerId)
        HandleEndOfRecordingExitPromptTimerEvent();
    else if (timer_id == videoExitDialogTimerId)
        HandleVideoExitDialogTimerEvent();
    else if (timer_id == pseudoChangeChanTimerId)
        HandlePseudoLiveTVTimerEvent();
    else if (timer_id == speedChangeTimerId)
        HandleSpeedChangeTimerEvent();
    else if (timer_id == pipChangeTimerId)
        HandlePxPTimerEvent();
    else
        handled = false;

    if (handled)
        return;

    // Check if it matches a stateChangeTimerId
    PlayerContext *ctx = NULL;
    {
        QMutexLocker locker(&timerIdLock);
        TimerContextMap::iterator it = stateChangeTimerId.find(timer_id);
        if (it != stateChangeTimerId.end())
        {
            KillTimer(timer_id);
            ctx = *it;
            stateChangeTimerId.erase(it);
        }
    }

    if (ctx)
    {
        PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
        bool still_exists = find_player_index(ctx) >= 0;

        while (still_exists && !ctx->nextState.empty())
        {
            HandleStateChange(mctx, ctx);
            if ((kState_None  == ctx->GetState() ||
                 kState_Error == ctx->GetState()) &&
                ((mctx != ctx) || jumpToProgram))
            {
                ReturnPlayerLock(mctx);
                mctx = GetPlayerWriteLock(0, __FILE__, __LINE__);
                TeardownPlayer(mctx, ctx);
                still_exists = false;
            }
        }
        ReturnPlayerLock(mctx);
        handled = true;
    }

    if (handled)
        return;

    // Check if it matches a signalMonitorTimerId
    ctx = NULL;
    {
        QMutexLocker locker(&timerIdLock);
        TimerContextMap::iterator it = signalMonitorTimerId.find(timer_id);
        if (it != signalMonitorTimerId.end())
        {
            KillTimer(timer_id);
            ctx = *it;
            signalMonitorTimerId.erase(it);
        }
    }

    if (ctx)
    {
        PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
        bool still_exists = find_player_index(ctx) >= 0;

        if (still_exists && !ctx->lastSignalMsg.empty())
        {   // set last signal msg, so we get some feedback...
            UpdateOSDSignal(ctx, ctx->lastSignalMsg);
            ctx->lastSignalMsg.clear();
        }
        UpdateOSDTimeoutMessage(ctx);

        ReturnPlayerLock(mctx);
        handled = true;
    }

    if (handled)
        return;

    // Check if it matches a tvchainUpdateTimerId
    ctx = NULL;
    {
        QMutexLocker locker(&timerIdLock);
        TimerContextMap::iterator it = tvchainUpdateTimerId.find(timer_id);
        if (it != tvchainUpdateTimerId.end())
        {
            KillTimer(timer_id);
            ctx = *it;
            tvchainUpdateTimerId.erase(it);
        }
    }

    if (ctx)
    {
        PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
        bool still_exists = find_player_index(ctx) >= 0;

        if (still_exists)
            ctx->UpdateTVChain();

        ReturnPlayerLock(mctx);
        handled = true;
    }

    if (handled)
        return;

    // Check if it matches networkControlTimerId
    QString netCmd = QString::null;
    {
        QMutexLocker locker(&timerIdLock);
        if (timer_id == networkControlTimerId)
        {
            if (networkControlCommands.size())
                netCmd = networkControlCommands.dequeue();
            if (networkControlCommands.empty())
            {
                KillTimer(networkControlTimerId);
                networkControlTimerId = 0;
            }
        }
    }

    if (!netCmd.isEmpty())
    {
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        LOG(VB_KEYPRESS, LOG_INFO, "networkControl Start");
        ProcessNetworkControlCommand(actx, netCmd);
        LOG(VB_KEYPRESS, LOG_INFO, "networkControl Finish");
        ReturnPlayerLock(actx);
        handled = true;
    }

    if (handled)
        return;

    // Check if it matches exitPlayerTimerId
    if (timer_id == exitPlayerTimerId)
    {
        PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);

        OSD *osd = GetOSDLock(mctx);
        if (osd)
        {
            osd->DialogQuit();
            osd->HideAll();
        }
        ReturnOSDLock(mctx, osd);

        if (jumpToProgram && lastProgram)
        {
            if (!lastProgram->IsFileReadable())
            {
                SetOSDMessage(mctx, tr("Last Program: %1 Doesn't Exist")
                                        .arg(lastProgram->GetTitle()));
                lastProgramStringList.clear();
                SetLastProgram(NULL);
                LOG(VB_PLAYBACK, LOG_ERR, LOC +
                    "Last Program File does not exist");
                jumpToProgram = false;
            }
            else
                ForceNextStateNone(mctx);
        }
        else
            ForceNextStateNone(mctx);

        ReturnPlayerLock(mctx);

        QMutexLocker locker(&timerIdLock);
        KillTimer(exitPlayerTimerId);
        exitPlayerTimerId = 0;
        handled = true;
    }

    if (handled)
        return;

    if (timer_id == jumpMenuTimerId)
    {
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        if (actx)
            FillOSDMenuJumpRec(actx);
        ReturnPlayerLock(actx);

        QMutexLocker locker(&timerIdLock);
        KillTimer(jumpMenuTimerId);
        jumpMenuTimerId = 0;
        handled = true;
    }

    if (handled)
        return;

    if (timer_id == switchToInputTimerId)
    {
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        if (switchToInputId)
        {
            uint tmp = switchToInputId;
            switchToInputId = 0;
            SwitchInputs(actx, tmp);
        }
        ReturnPlayerLock(actx);

        QMutexLocker locker(&timerIdLock);
        KillTimer(switchToInputTimerId);
        switchToInputTimerId = 0;
        handled = true;
    }

    if (handled)
        return;

    if (timer_id == ccInputTimerId)
    {
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        // Clear closed caption input mode when timer expires
        if (ccInputMode)
        {
            ccInputMode = false;
            ClearInputQueues(actx, true);
        }
        ReturnPlayerLock(actx);

        QMutexLocker locker(&timerIdLock);
        KillTimer(ccInputTimerId);
        ccInputTimerId = 0;
        handled = true;
    }

    if (handled)
        return;

    if (timer_id == asInputTimerId)
    {
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        // Clear closed caption input mode when timer expires
        if (asInputMode)
        {
            asInputMode = false;
            ClearInputQueues(actx, true);
        }
        ReturnPlayerLock(actx);

        QMutexLocker locker(&timerIdLock);
        KillTimer(asInputTimerId);
        asInputTimerId = 0;
        handled = true;
    }

    if (handled)
        return;

    if (timer_id == queueInputTimerId)
    {
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        // Commit input when the OSD fades away
        if (HasQueuedChannel())
        {
            OSD *osd = GetOSDLock(actx);
            if (osd && !osd->IsWindowVisible("osd_input"))
                CommitQueuedInput(actx);
            ReturnOSDLock(actx, osd);
        }
        ReturnPlayerLock(actx);

        QMutexLocker locker(&timerIdLock);
        if (!queuedChanID && queuedChanNum.isEmpty() && queueInputTimerId)
        {
            KillTimer(queueInputTimerId);
            queueInputTimerId = 0;
        }
        handled = true;
    }

    if (handled)
        return;

    if (timer_id == browseTimerId)
    {
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        browsehelper->BrowseEnd(actx, false);
        ReturnPlayerLock(actx);
        handled = true;
    }

    if (handled)
        return;

    if (timer_id == updateOSDDebugTimerId)
    {
        bool update = false;
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        OSD *osd = GetOSDLock(actx);
        if (osd && osd->IsWindowVisible("osd_debug") &&
            (StateIsLiveTV(actx->GetState()) ||
             StateIsPlaying(actx->GetState())))
        {
            update = true;
        }
        else
        {
            QMutexLocker locker(&timerIdLock);
            KillTimer(updateOSDDebugTimerId);
            updateOSDDebugTimerId = 0;
            actx->buffer->EnableBitrateMonitor(false);
            if (actx->player)
                actx->player->EnableFrameRateMonitor(false);
        }
        ReturnOSDLock(actx, osd);
        if (update)
            UpdateOSDDebug(actx);
        ReturnPlayerLock(actx);
        handled = true;
    }
    if (timer_id == updateOSDPosTimerId)
    {
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        OSD *osd = GetOSDLock(actx);
        if (osd && osd->IsWindowVisible("osd_status") &&
            (StateIsLiveTV(actx->GetState()) ||
             StateIsPlaying(actx->GetState())))
        {
            osdInfo info;
            if (actx->CalcPlayerSliderPosition(info))
            {
                osd->SetText("osd_status", info.text, kOSDTimeout_Ignore);
                osd->SetValues("osd_status", info.values, kOSDTimeout_Ignore);
            }
        }
        else
            SetUpdateOSDPosition(false);
        ReturnOSDLock(actx, osd);
        ReturnPlayerLock(actx);
        handled = true;
    }

    if (handled)
        return;

    if (timer_id == errorRecoveryTimerId)
    {
        bool error = false;
        PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);

        if (mctx->IsPlayerErrored())
        {
            if (mctx->IsPlayerRecoverable())
                RestartMainPlayer(mctx);

            if (mctx->IsPlayerDecoderErrored())
            {
                LOG(VB_GENERAL, LOG_EMERG, LOC +
                    QString("Serious hardware decoder error detected. "
                            "Disabling hardware decoders."));
                noHardwareDecoders = true;
                for (uint i = 0; i < player.size(); i++)
                    player[i]->SetNoHardwareDecoders();
                RestartMainPlayer(mctx);
            }
        }

        if (mctx->IsRecorderErrored() ||
            mctx->IsPlayerErrored() ||
            mctx->IsErrored())
        {
            SetExitPlayer(true, false);
            ForceNextStateNone(mctx);
            error = true;
        }

        for (uint i = 0; i < player.size(); i++)
        {
            PlayerContext *ctx = GetPlayer(mctx, i);
            if (error || ctx->IsErrored())
                ForceNextStateNone(ctx);
        }
        ReturnPlayerLock(mctx);

        QMutexLocker locker(&timerIdLock);
        if (errorRecoveryTimerId)
            KillTimer(errorRecoveryTimerId);
        errorRecoveryTimerId =
            StartTimer(kErrorRecoveryCheckFrequency, __LINE__);
    }
}

bool TV::HandlePxPTimerEvent(void)
{
    QString cmd = QString::null;

    {
        QMutexLocker locker(&timerIdLock);
        if (changePxP.empty())
        {
            if (pipChangeTimerId)
                KillTimer(pipChangeTimerId);
            pipChangeTimerId = 0;
            return true;
        }
        cmd = changePxP.dequeue();
    }

    PlayerContext *mctx = GetPlayerWriteLock(0, __FILE__, __LINE__);
    PlayerContext *actx = GetPlayer(mctx, -1);

    if (cmd == "TOGGLEPIPMODE")
        PxPToggleView(actx, false);
    else if (cmd == "TOGGLEPBPMODE")
        PxPToggleView(actx, true);
    else if (cmd == "CREATEPIPVIEW")
        PxPCreateView(actx, false);
    else if (cmd == "CREATEPBPVIEW")
        PxPCreateView(actx, true);
    else if (cmd == "SWAPPIP")
    {
        if (mctx != actx)
            PxPSwap(mctx, actx);
        else if (mctx && player.size() == 2)
            PxPSwap(mctx, GetPlayer(mctx,1));
    }
    else if (cmd == "TOGGLEPIPSTATE")
        PxPToggleType(mctx, !mctx->IsPBP());

    ReturnPlayerLock(mctx);

    QMutexLocker locker(&timerIdLock);

    if (pipChangeTimerId)
        KillTimer(pipChangeTimerId);

    if (changePxP.empty())
        pipChangeTimerId = 0;
    else
        pipChangeTimerId = StartTimer(20, __LINE__);

    return true;
}

bool TV::HandleLCDTimerEvent(void)
{
    PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    LCD *lcd = LCD::Get();
    if (lcd)
    {
        float progress = 0.0f;
        QString lcd_time_string;
        bool showProgress = true;

        if (StateIsLiveTV(GetState(actx)))
            ShowLCDChannelInfo(actx);

        if (actx->buffer && actx->buffer->IsDVD())
        {
            ShowLCDDVDInfo(actx);
            showProgress = !actx->buffer->IsInDiscMenuOrStillFrame();
        }

        if (showProgress)
        {
            osdInfo info;
            if (actx->CalcPlayerSliderPosition(info)) {
                progress = info.values["position"] * 0.001f;

                lcd_time_string = info.text["playedtime"] + " / " + info.text["totaltime"];
                // if the string is longer than the LCD width, remove all spaces
                if (lcd_time_string.length() > (int)lcd->getLCDWidth())
                    lcd_time_string.remove(' ');
            }
        }
        lcd->setChannelProgress(lcd_time_string, progress);
    }
    ReturnPlayerLock(actx);

    QMutexLocker locker(&timerIdLock);
    KillTimer(lcdTimerId);
    lcdTimerId = StartTimer(kLCDTimeout, __LINE__);

    return true;
}

void TV::HandleLCDVolumeTimerEvent()
{
    PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    LCD *lcd = LCD::Get();
    if (lcd)
    {
        ShowLCDChannelInfo(actx);
        lcd->switchToChannel(lcdCallsign, lcdTitle, lcdSubtitle);
    }
    ReturnPlayerLock(actx);

    QMutexLocker locker(&timerIdLock);
    KillTimer(lcdVolumeTimerId);
}

int TV::StartTimer(int interval, int line)
{
    int x = QObject::startTimer(interval);
    if (!x)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            QString("Failed to start timer on line %1 of %2")
                .arg(line).arg(__FILE__));
    }
    return x;
}

void TV::KillTimer(int id)
{
    QObject::killTimer(id);
}

void TV::ForceNextStateNone(PlayerContext *ctx)
{
    ctx->ForceNextStateNone();
    ScheduleStateChange(ctx);
}

void TV::ScheduleStateChange(PlayerContext *ctx)
{
    QMutexLocker locker(&timerIdLock);
    stateChangeTimerId[StartTimer(1, __LINE__)] = ctx;
}

void TV::SetErrored(PlayerContext *ctx)
{
    if (!ctx)
        return;
    QMutexLocker locker(&timerIdLock);
    ctx->errored = true;
    KillTimer(errorRecoveryTimerId);
    errorRecoveryTimerId = StartTimer(1, __LINE__);
}

void TV::PrepToSwitchToRecordedProgram(PlayerContext *ctx,
                                       const ProgramInfo &p)
{
    LOG(VB_GENERAL, LOG_INFO, LOC + QString("Switching to program: %1")
            .arg(p.toString(ProgramInfo::kTitleSubtitle)));
    SetLastProgram(&p);
    PrepareToExitPlayer(ctx,__LINE__);
    jumpToProgram = true;
    SetExitPlayer(true, true);
}

void TV::PrepareToExitPlayer(PlayerContext *ctx, int line, bool bookmark)
{
    bool bookmark_it = bookmark && IsBookmarkAllowed(ctx);
    ctx->LockDeletePlayer(__FILE__, line);
    if (ctx->player)
    {
        if (bookmark_it && (!(ctx->player->IsNearEnd()) ||
                            StateIsRecording(GetState(ctx))))
            SetBookmark(ctx);
        if (db_auto_set_watched)
            ctx->player->SetWatched();
    }
    ctx->UnlockDeletePlayer(__FILE__, line);
}

void TV::SetExitPlayer(bool set_it, bool wants_to)
{
    QMutexLocker locker(&timerIdLock);
    if (set_it)
    {
        wantsToQuit = wants_to;
        if (!exitPlayerTimerId)
            exitPlayerTimerId = StartTimer(1, __LINE__);
    }
    else
    {
        if (exitPlayerTimerId)
            KillTimer(exitPlayerTimerId);
        exitPlayerTimerId = 0;
        wantsToQuit = wants_to;
    }
}

void TV::SetUpdateOSDPosition(bool set_it)
{
    QMutexLocker locker(&timerIdLock);
    if (set_it)
    {
        if (!updateOSDPosTimerId)
            updateOSDPosTimerId = StartTimer(500, __LINE__);
    }
    else
    {
        if (updateOSDPosTimerId)
            KillTimer(updateOSDPosTimerId);
        updateOSDPosTimerId = 0;
    }
}

void TV::HandleEndOfPlaybackTimerEvent(void)
{
    {
        QMutexLocker locker(&timerIdLock);
        if (endOfPlaybackTimerId)
            KillTimer(endOfPlaybackTimerId);
        endOfPlaybackTimerId = 0;
    }

    bool is_playing = false;
    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        if (!StateIsPlaying(ctx->GetState()))
            continue;

        if (ctx->IsPlayerPlaying())
        {
            is_playing = true;
            continue;
        }

        ForceNextStateNone(ctx);
        if (mctx == ctx)
        {
            endOfRecording = true;
            PrepareToExitPlayer(mctx, __LINE__, false);
            SetExitPlayer(true, true);
        }
    }
    ReturnPlayerLock(mctx);

    if (is_playing)
    {
        QMutexLocker locker(&timerIdLock);
        endOfPlaybackTimerId =
            StartTimer(kEndOfPlaybackCheckFrequency, __LINE__);
    }
}

void TV::HandleIsNearEndWhenEmbeddingTimerEvent(void)
{
    PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    if (!StateIsLiveTV(GetState(actx)))
    {
        actx->LockDeletePlayer(__FILE__, __LINE__);
        bool toggle = actx->player && actx->player->IsEmbedding() &&
                      actx->player->IsNearEnd() && !actx->player->IsPaused();
        actx->UnlockDeletePlayer(__FILE__, __LINE__);
        if (toggle)
            DoTogglePause(actx, true);
    }
    ReturnPlayerLock(actx);
}

void TV::HandleEndOfRecordingExitPromptTimerEvent(void)
{
    if (endOfRecording || inPlaylist || editmode || underNetworkControl ||
        exitPlayerTimerId)
    {
        return;
    }

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    OSD *osd = GetOSDLock(mctx);
    if (osd && osd->DialogVisible())
    {
        ReturnOSDLock(mctx, osd);
        ReturnPlayerLock(mctx);
        return;
    }
    ReturnOSDLock(mctx, osd);

    bool do_prompt = false;
    mctx->LockDeletePlayer(__FILE__, __LINE__);
    if (mctx->GetState() == kState_WatchingPreRecorded && mctx->player)
    {
        if (!mctx->player->IsNearEnd())
            jumped_back = false;

        do_prompt = mctx->player->IsNearEnd() && !jumped_back &&
            !mctx->player->IsEmbedding() && !mctx->player->IsPaused();
    }
    mctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (do_prompt)
        ShowOSDPromptDeleteRecording(mctx, tr("End Of Recording"));

    ReturnPlayerLock(mctx);
}

void TV::HandleVideoExitDialogTimerEvent(void)
{
    {
        QMutexLocker locker(&timerIdLock);
        if (videoExitDialogTimerId)
            KillTimer(videoExitDialogTimerId);
        videoExitDialogTimerId = 0;
    }

    // disable dialog and exit playback after timeout
    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    OSD *osd = GetOSDLock(mctx);
    if (!osd || !osd->DialogVisible(OSD_DLG_VIDEOEXIT))
    {
        ReturnOSDLock(mctx, osd);
        ReturnPlayerLock(mctx);
        return;
    }
    if (osd)
        osd->DialogQuit();
    ReturnOSDLock(mctx, osd);
    DoTogglePause(mctx, true);
    ClearOSD(mctx);
    PrepareToExitPlayer(mctx, __LINE__);
    ReturnPlayerLock(mctx);

    requestDelete = false;
    SetExitPlayer(true, true);
}

void TV::HandlePseudoLiveTVTimerEvent(void)
{
    {
        QMutexLocker locker(&timerIdLock);
        KillTimer(pseudoChangeChanTimerId);
        pseudoChangeChanTimerId = 0;
    }

    bool restartTimer = false;
    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        if (kPseudoChangeChannel != ctx->pseudoLiveTVState)
            continue;

        if (ctx->InStateChange())
        {
            restartTimer = true;
            continue;
        }

        LOG(VB_CHANNEL, LOG_INFO,
            QString("REC_PROGRAM -- channel change %1").arg(i));

        uint        chanid  = ctx->pseudoLiveTVRec->GetChanID();
        QString     channum = ctx->pseudoLiveTVRec->GetChanNum();
        StringDeque tmp     = ctx->prevChan;

        ctx->prevChan.clear();
        ChangeChannel(ctx, chanid, channum);
        ctx->prevChan = tmp;
        ctx->pseudoLiveTVState = kPseudoRecording;
    }
    ReturnPlayerLock(mctx);

    if (restartTimer)
    {
        QMutexLocker locker(&timerIdLock);
        if (!pseudoChangeChanTimerId)
            pseudoChangeChanTimerId = StartTimer(25, __LINE__);
    }
}

void TV::SetSpeedChangeTimer(uint when, int line)
{
    QMutexLocker locker(&timerIdLock);
    if (speedChangeTimerId)
        KillTimer(speedChangeTimerId);
    speedChangeTimerId = StartTimer(when, line);
}

void TV::HandleSpeedChangeTimerEvent(void)
{
    {
        QMutexLocker locker(&timerIdLock);
        if (speedChangeTimerId)
            KillTimer(speedChangeTimerId);
        speedChangeTimerId = StartTimer(kSpeedChangeCheckFrequency, __LINE__);
    }

    bool update_msg = false;
    PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    for (uint i = 0; actx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(actx, i);
        update_msg |= ctx->HandlePlayerSpeedChangeFFRew() && (ctx == actx);
    }
    ReturnPlayerLock(actx);

    actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    for (uint i = 0; actx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(actx, i);
        update_msg |= ctx->HandlePlayerSpeedChangeEOF() && (ctx == actx);
    }

    if (update_msg)
    {
        UpdateOSDSeekMessage(actx, actx->GetPlayMessage(), kOSDTimeout_Med);
    }
    ReturnPlayerLock(actx);
}

/// This selectively blocks KeyPress and Resize events
bool TV::eventFilter(QObject *o, QEvent *e)
{
    // We want to intercept all resize events sent to the main window
    if ((e->type() == QEvent::Resize))
        return (GetMythMainWindow()!= o) ? false : event(e);

    // Intercept keypress events unless they need to be handled by a main UI
    // screen (e.g. GuideGrid, ProgramFinder)
    if (QEvent::KeyPress == e->type())
        return ignoreKeyPresses ? false : event(e);

    if (e->type() == MythEvent::MythEventMessage ||
        e->type() == MythEvent::MythUserMessage  ||
        e->type() == MythEvent::kUpdateTvProgressEventType)
    {
        customEvent(e);
        return true;
    }

    switch (e->type())
    {
        case QEvent::Paint:
        case QEvent::UpdateRequest:
        case QEvent::Enter:
        {
            event(e);
            return false;
        }
        default:
            return false;
    }
}

/// This handles all standard events
bool TV::event(QEvent *e)
{
    if (QEvent::Resize == e->type())
    {
        PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
        mctx->LockDeletePlayer(__FILE__, __LINE__);
        if (mctx->player)
            mctx->player->WindowResized(((const QResizeEvent*) e)->size());
        mctx->UnlockDeletePlayer(__FILE__, __LINE__);
        ReturnPlayerLock(mctx);
        return true;
    }

    if (QEvent::KeyPress == e->type())
    {
        bool handled = false;
        PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
        if (actx->HasPlayer())
        {
            LOG(VB_KEYPRESS, LOG_INFO, "keyPress Start");
            handled = ProcessKeypress(actx, (QKeyEvent *)e);
            LOG(VB_KEYPRESS, LOG_INFO, QString("keyPress Finish - Result %1")
                .arg(handled));
        }
        ReturnPlayerLock(actx);
        if (handled)
            return true;
    }

    switch (e->type())
    {
        case QEvent::Paint:
        case QEvent::UpdateRequest:
        case QEvent::Enter:
            DrawUnusedRects();
            return true;
        default:
            break;
    }

    return QObject::event(e);
}

static struct ActionDefStruct<TV> tthActions[] = {
    { ACTION_TOGGLEEXTTEXT,     &TV::doTrackToggleExtText },
    { ACTION_ENABLEEXTTEXT,     &TV::doTrackEnableExtText },
    { ACTION_DISABLEEXTTEXT,    &TV::doTrackDisableExtText },
    { ACTION_ENABLEFORCEDSUBS,  &TV::doTrackEnableForcedSubs },
    { ACTION_DISABLEFORCEDSUBS, &TV::doTrackDisableForcedSubs },
    { ACTION_ENABLESUBS,        &TV::doTrackEnableSubs },
    { ACTION_DISABLESUBS,       &TV::doTrackDisableSubs },
    { ACTION_TOGGLESUBS,        &TV::doTrackToggleSubs },
    { "^TOGGLE",                &TV::doTrackToggle },
    { "^SELECT",                &TV::doTrackSelect },
    { "^NEXT",                  &TV::doTrackNextPrev },
    { "^PREV",                  &TV::doTrackNextPrev }
};
static int tthActionCount = NELEMS(tthActions);

bool TV::doTrackToggleExtText(const QString &action)
{
    m_actionContext->player->ToggleCaptions(kTrackTypeTextSubtitle);
    return true;
}

bool TV::doTrackEnableExtText(const QString &action)
{
    m_actionContext->player->EnableCaptions(kDisplayTextSubtitle);
    return true;
}

bool TV::doTrackDisableExtText(const QString &action)
{
    m_actionContext->player->DisableCaptions(kDisplayTextSubtitle);
    return true;
}

bool TV::doTrackEnableForcedSubs(const QString &action)
{
    m_actionContext->player->SetAllowForcedSubtitles(true);
    return true;
}

bool TV::doTrackDisableForcedSubs(const QString &action)
{
    m_actionContext->player->SetAllowForcedSubtitles(false);
    return true;
}

bool TV::doTrackEnableSubs(const QString &action)
{
    m_actionContext->player->SetCaptionsEnabled(true, true);
    return true;
}

bool TV::doTrackDisableSubs(const QString &action)
{
    m_actionContext->player->SetCaptionsEnabled(false, true);
    return true;
}

bool TV::doTrackToggleSubs(const QString &action)
{
    if (browsehelper->IsBrowsing())
        return false;

    MythPlayer *play = m_actionContext->player;

    if (ccInputMode)
    {
        bool valid = false;
        int page = GetQueuedInputAsInt(&valid, 16);
        if (vbimode == VBIMode::PAL_TT && valid)
            play->SetTeletextPage(page);
        else if (vbimode == VBIMode::NTSC_CC)
            play->SetTrack(kTrackTypeCC608, max(min(page - 1, 1), 0));

        ClearInputQueues(m_actionContext, true);

        QMutexLocker locker(&timerIdLock);
        ccInputMode = false;
        if (ccInputTimerId)
        {
            KillTimer(ccInputTimerId);
            ccInputTimerId = 0;
        }
    }
    else if (play->GetCaptionMode() & kDisplayNUVTeletextCaptions)
    {
        ClearInputQueues(m_actionContext, false);
        AddKeyToInputQueue(m_actionContext, 0);

        QMutexLocker locker(&timerIdLock);
        ccInputMode        = true;
        asInputMode        = false;
        ccInputTimerId = StartTimer(kInputModeTimeout, __LINE__);
        if (asInputTimerId)
        {
            KillTimer(asInputTimerId);
            asInputTimerId = 0;
        }
    }
    else
    {
        play->ToggleCaptions();
    }
    return true;
}

bool TV::doTrackToggle(const QString &action)
{
    int type = to_track_type(action.mid(6));
    if (type == kTrackTypeTeletextMenu)
    {
        m_actionContext->player->EnableTeletext();
        return true;
    }
    
    if (type >= kTrackTypeSubtitle)
    {
        m_actionContext->player->ToggleCaptions(type);
        return true;
    }

    return false;
}

bool TV::doTrackSelect(const QString &action)
{
    int type = to_track_type(action.mid(6));
    int num = action.section("_", -1).toInt();
    if (type >= kTrackTypeAudio)
    {
        m_actionContext->player->SetTrack(type, num);
        return true;
    }
    return false;
}

bool TV::doTrackNextPrev(const QString &action)
{
    int dir = (action.left(4) == "NEXT") ? +1 : -1;
    int type = to_track_type(action.mid(4));
    if (type >= kTrackTypeAudio)
    {
        m_actionContext->player->ChangeTrack(type, dir);
        return true;
    }

    if (action.right(2) == "CC")
    {
        m_actionContext->player->ChangeCaptionTrack(dir);
        return true;
    }

    return false;
}

bool TV::HandleTrackAction(PlayerContext *ctx, const QStringList &actions)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return false;
    }

    m_actionContext = ctx;
    if (!m_trackActions)
        m_trackActions = new MythActions<TV>(this, tthActions, tthActionCount);
    bool handled = m_trackActions->handleActions(actions);

    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    return handled;
}

static struct ActionDefStruct<TV> tiActions[] = {
    { "ESCAPE",     &TV::doNoIgnoreEsc },
    { "BACK",       &TV::doNoIgnoreEsc },
    { ACTION_PLAY,  &TV::doNoIgnore },
    { ACTION_PAUSE, &TV::doNoIgnore }
};
static int tiActionCount = NELEMS(tiActions);

static struct ActionDefStruct<TV> teActions[] = {
    { "ESCAPE",  &TV::doEditEscape },
    { "MENU",    &TV::doEditMenu },
    { "SELECT",  &TV::doEditSelect }
};
static int teActionCount = NELEMS(teActions);


bool TV::doNoIgnore(const QString &action)
{
    return true;
}

bool TV::doNoIgnoreEsc(const QString &action)
{
    return !browsehelper->IsBrowsing();
}

bool TV::doEditEscape(const QString &action)
{
    if (!m_actionContext->player->IsCutListSaved())
        ShowOSDCutpoint(m_actionContext, "EXIT_EDIT_MODE");
    else
    {
        m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
        if (m_actionContext->player)
            m_actionContext->player->DisableEdit(0);
        m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);
    }
    return true;
}

bool TV::doEditMenu(const QString &action)
{
    ShowOSDCutpoint(m_actionContext, "EDIT_CUT_POINTS");
    return true;
}

bool TV::doEditSelect(const QString &action)
{
    m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
    int64_t current_frame = m_actionContext->player->GetFramesPlayed();
    m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);
    if ((m_actionContext->player->IsInDelete(current_frame)) &&
        (!(m_actionContext->player->HasTemporaryMark())))
    {
        ShowOSDCutpoint(m_actionContext, "EDIT_CUT_REGION");
        return true;
    }
    return false;
}

bool TV::ProcessKeypress(PlayerContext *actx, QKeyEvent *e)
{
    bool ignoreKeys = actx->IsPlayerChangingBuffers();

    m_actionContext = actx;
    MythPlayer *play = actx->player;

    if (idleTimerId)
    {
        KillTimer(idleTimerId);
        idleTimerId = StartTimer(db_idle_timeout, __LINE__);
    }

    QStringList actions;
    bool handled = false;

    if (ignoreKeys)
    {
        handled = GetMythMainWindow()->TranslateKeyPress("TV Playback", e,
                                                         actions);

        if (handled || actions.isEmpty())
            return true;

        if (!handled)
        {
            if (!m_ignoreActions)
                m_ignoreActions = new MythActions<TV>(this, tiActions,
                                                      tiActionCount);
            handled = m_ignoreActions->handleActions(actions);
        }

        if (!handled)
            return false;
    }

    OSD *osd = GetOSDLock(actx);
    if (osd && osd->DialogVisible())
    {
        osd->DialogHandleKeypress(e);
        handled = true;
    }
    ReturnOSDLock(actx, osd);

    if (handled)
        return true;

    if (editmode)
    {
        handled |= GetMythMainWindow()->TranslateKeyPress("TV Editing", e,
                                                          actions);

        if (!handled)
        {
            if (!m_editActions)
                m_editActions = new MythActions<TV>(this, teActions,
                                                    teActionCount);
            handled = m_editActions->handleActions(actions);
        }

        if (!handled)
        {
            actx->LockDeletePlayer(__FILE__, __LINE__);
            int64_t current_frame = play->GetFramesPlayed();
            actx->UnlockDeletePlayer(__FILE__, __LINE__);
            handled |= play->HandleProgramEditorActions(actions, current_frame);
        }

        if (handled)
            editmode = play->GetEditMode();
    }

    if (handled)
        return true;

    // If text is already queued up, be more lax on what is ok.
    // This allows hex teletext entry and minor channel entry.
    const QString txt = e->text();
    if (HasQueuedInput() && (1 == txt.length()))
    {
        bool ok = false;
        txt.toInt(&ok, 16);
        if (ok || txt=="_" || txt=="-" || txt=="#" || txt==".")
        {
            AddKeyToInputQueue(actx, txt.at(0).toLatin1());
            return true;
        }
    }

    // Teletext menu
    actx->LockDeletePlayer(__FILE__, __LINE__);
    if (play && play->GetCaptionMode() == kDisplayTeletextMenu)
    {
        QStringList tt_actions;
        handled = GetMythMainWindow()->TranslateKeyPress("Teletext Menu", e,
                                                         tt_actions);

        if (!handled && !tt_actions.isEmpty())
            handled = play->HandleTeletextActions(tt_actions);

        if (handled)
        {
            actx->UnlockDeletePlayer(__FILE__, __LINE__);
            return true;
        }
    }

    // Interactive television
    if (play && play->GetInteractiveTV())
    {
        QStringList itv_actions;
        handled = GetMythMainWindow()->TranslateKeyPress("TV Playback", e,
                                                         itv_actions);

        if (!handled && !itv_actions.isEmpty())
        {
            handled = play->ITVHandleActions(itv_actions);
        }
    }
    actx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (handled)
        return true;

    handled = GetMythMainWindow()->TranslateKeyPress("TV Playback", e, actions);

    if (handled || actions.isEmpty())
        return true;

    handled = handled || BrowseHandleAction(actx, actions);
    handled = handled || ManualZoomHandleAction(actx, actions);
    handled = handled || PictureAttributeHandleAction(actx, actions);
    handled = handled || TimeStretchHandleAction(actx, actions);
    handled = handled || AudioSyncHandleAction(actx, actions);
    handled = handled || DiscMenuHandleAction(actx, actions);
    handled = handled || ActiveHandleAction(actx, actions);
    handled = handled || ToggleHandleAction(actx, actions);
    handled = handled || PxPHandleAction(actx, actions);
    handled = handled || FFRewHandleAction(actx, actions);
    handled = handled || ActivePostQHandleAction(actx, actions);

    if (handled)
        return true;

    if (!handled)
    {
        for (int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            bool ok = false;
            int val = action.toInt(&ok);

            if (ok)
            {
                AddKeyToInputQueue(actx, '0' + val);
                handled = true;
            }
        }
    }

    return true;
}

static struct ActionDefStruct<TV> tbhActions[] = {
    { ACTION_UP,           &TV::doBrowseUp },
    { ACTION_CHANNELUP,    &TV::doBrowseUp },
    { ACTION_DOWN,         &TV::doBrowseDown },
    { ACTION_CHANNELDOWN,  &TV::doBrowseDown },
    { ACTION_LEFT,         &TV::doBrowseLeft },
    { ACTION_RIGHT,        &TV::doBrowseRight },
    { "NEXTFAV",           &TV::doBrowseNextFav },
    { ACTION_SELECT,       &TV::doBrowseSelect },
    { ACTION_CLEAROSD,     &TV::doBrowseEscape },
    { "ESCAPE",            &TV::doBrowseEscape },
    { "BACK",              &TV::doBrowseEscape },
    { "TOGGLEBROWSE",      &TV::doBrowseEscape },
    { ACTION_TOGGLERECORD, &TV::doBrowseToggleRecord },
    { "^[0123456789]$",    &TV::doBrowseDigit }
};
static int tbhActionCount = NELEMS(tbhActions);

static struct ActionDefStruct<TV> tbhpActions[] = {
    { ACTION_VOLUMEDOWN, &TV::doNoIgnore },
    { ACTION_VOLUMEUP,   &TV::doNoIgnore },
    { "STRETCHINC",      &TV::doNoIgnore },
    { "STRETCHDEC",      &TV::doNoIgnore },
    { ACTION_MUTEAUDIO,  &TV::doNoIgnore },
    { "CYCLEAUDIOCHAN",  &TV::doNoIgnore },
    { "TOGGLEASPECT",    &TV::doNoIgnore },
    { "TOGGLEPIPMODE",   &TV::doNoIgnore },
    { "TOGGLEPIPSTATE",  &TV::doNoIgnore },
    { "NEXTPIPWINDOW",   &TV::doNoIgnore },
    { "CREATEPIPVIEW",   &TV::doNoIgnore },
    { "CREATEPBPVIEW",   &TV::doNoIgnore },
    { "SWAPPIP",         &TV::doNoIgnore }
};
static int tbhpActionCount = NELEMS(tbhpActions);


bool TV::doBrowseUp(const QString &action)
{
    browsehelper->BrowseDispInfo(m_actionContext, BROWSE_UP);
    return true;
}

bool TV::doBrowseDown(const QString &action)
{
    browsehelper->BrowseDispInfo(m_actionContext, BROWSE_DOWN);
    return true;
}

bool TV::doBrowseLeft(const QString &action)
{
    browsehelper->BrowseDispInfo(m_actionContext, BROWSE_LEFT);
    return true;
}

bool TV::doBrowseRight(const QString &action)
{
    browsehelper->BrowseDispInfo(m_actionContext, BROWSE_RIGHT);
    return true;
}

bool TV::doBrowseNextFav(const QString &action)
{
    browsehelper->BrowseDispInfo(m_actionContext, BROWSE_FAVORITE);
    return true;
}

bool TV::doBrowseSelect(const QString &action)
{
    browsehelper->BrowseEnd(m_actionContext, true);
    return true;
}

bool TV::doBrowseEscape(const QString &action)
{
    browsehelper->BrowseEnd(m_actionContext, false);
    return true;
}

bool TV::doBrowseToggleRecord(const QString &action)
{
    ToggleRecord(m_actionContext);
    return true;
}

bool TV::doBrowseDigit(const QString &action)
{
    AddKeyToInputQueue(m_actionContext, '0' + action.toInt());
    return true;
}


bool TV::BrowseHandleAction(PlayerContext *ctx, const QStringList &actions)
{
    if (!browsehelper->IsBrowsing())
        return false;

    if (!m_browseActions)
        m_browseActions = new MythActions<TV>(this, tbhActions,
                                              tbhActionCount);

    m_actionContext = ctx;
    bool handled = m_browseActions->handleActions(actions);
    if (handled)
        return true;

    if (!m_browsePassActions)
        m_browsePassActions = new MythActions<TV>(this, tbhpActions,
                                                  tbhpActionCount);
    handled = m_browsePassActions->handleActions(actions);

    return !handled;
}

static struct ActionDefStruct<TV> tmzhActions[] = {
    { ACTION_UP,           &TV::doManualZoomUp },
    { ACTION_CHANNELUP,    &TV::doManualZoomUp },
    { ACTION_DOWN,         &TV::doManualZoomDown },
    { ACTION_CHANNELDOWN,  &TV::doManualZoomDown },
    { ACTION_LEFT,         &TV::doManualZoomLeft },
    { ACTION_RIGHT,        &TV::doManualZoomRight },
    { ACTION_VOLUMEUP,     &TV::doManualZoomAspectUp },
    { ACTION_VOLUMEDOWN,   &TV::doManualZoomAspectDown },
    { "ESCAPE",            &TV::doManualZoomEscape },
    { "BACK",              &TV::doManualZoomEscape },
    { ACTION_SELECT,       &TV::doManualZoomSelect },
    { ACTION_JUMPFFWD,     &TV::doManualZoomIn },
    { ACTION_JUMPRWND,     &TV::doManualZoomOut }
};
static int tmzhActionCount = NELEMS(tmzhActions);

static struct ActionDefStruct<TV> tmzhpActions[] = {
    { "STRETCHINC",      &TV::doNoIgnore },
    { "STRETCHDEC",      &TV::doNoIgnore },
    { ACTION_MUTEAUDIO,  &TV::doNoIgnore },
    { "CYCLEAUDIOCHAN",  &TV::doNoIgnore },
    { ACTION_PAUSE,      &TV::doNoIgnore },
    { ACTION_CLEAROSD,   &TV::doNoIgnore }
};
static int tmzhpActionCount = NELEMS(tmzhpActions);


bool TV::doManualZoomUp(const QString &action)
{
    m_actionContext->player->Zoom(kZoomUp);
    return true;
}

bool TV::doManualZoomDown(const QString &action)
{
    m_actionContext->player->Zoom(kZoomDown);
    return true;
}

bool TV::doManualZoomLeft(const QString &action)
{
    m_actionContext->player->Zoom(kZoomLeft);
    return true;
}

bool TV::doManualZoomRight(const QString &action)
{
    m_actionContext->player->Zoom(kZoomRight);
    return true;
}

bool TV::doManualZoomAspectUp(const QString &action)
{
    m_actionContext->player->Zoom(kZoomAspectDown);
    return true;
}

bool TV::doManualZoomAspectDown(const QString &action)
{
    m_actionContext->player->Zoom(kZoomAspectDown);
    return true;
}

bool TV::doManualZoomEscape(const QString &action)
{
    m_actionContext->player->Zoom(kZoomHome);
    m_actionEndManualZoom = true;
    return true;
}

bool TV::doManualZoomSelect(const QString &action)
{
    SetManualZoom(m_actionContext, false, tr("Zoom Committed"));
    return true;
}

bool TV::doManualZoomIn(const QString &action)
{
    m_actionContext->player->Zoom(kZoomIn);
    return true;
}

bool TV::doManualZoomOut(const QString &action)
{
    m_actionContext->player->Zoom(kZoomOut);
    return true;
}


bool TV::ManualZoomHandleAction(PlayerContext *actx, const QStringList &actions)
{
    if (!zoomMode)
        return false;

    m_actionContext = actx;

    actx->LockDeletePlayer(__FILE__, __LINE__);
    MythPlayer *play = actx->player;
    if (!play)
    {
        actx->UnlockDeletePlayer(__FILE__, __LINE__);
        return false;
    }

    if (!m_manualZoomActions)
        m_manualZoomActions = new MythActions<TV>(this, tmzhActions,
                                                  tmzhActionCount);

    m_actionEndManualZoom = false;
    bool handled = m_manualZoomActions->handleActions(actions);

    if (!handled)
    {
        if (!m_manualZoomPassActions)
            m_manualZoomPassActions = new MythActions<TV>(this, tmzhpActions,
                                                          tmzhpActionCount);
        handled = m_manualZoomPassActions->handleActions(actions);
    }

    actx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (m_actionEndManualZoom)
        SetManualZoom(actx, false, tr("Zoom Ignored"));

    return !handled;
}

static struct ActionDefStruct<TV> tpahActions[] = {
    { ACTION_LEFT,         &TV::doPicAttrLeft },
    { ACTION_RIGHT,        &TV::doPicAttrRight }
};
static int tpahActionCount = NELEMS(tpahActions);

bool TV::doPicAttrLeft(const QString &action)
{
    DoChangePictureAttribute(m_actionContext, adjustingPicture,
                             adjustingPictureAttribute, false);
    return true;
}

bool TV::doPicAttrRight(const QString &action)
{
    DoChangePictureAttribute(m_actionContext, adjustingPicture,
                             adjustingPictureAttribute, true);
    return true;
}


bool TV::PictureAttributeHandleAction(PlayerContext *ctx,
                                      const QStringList &actions)
{
    if (!adjustingPicture)
        return false;

    if (!m_picAttrActions)
        m_picAttrActions = new MythActions<TV>(this, tpahActions,
                                               tpahActionCount);

    m_actionContext = ctx;
    bool handled = m_picAttrActions->handleActions(actions);

    return handled;
}

static struct ActionDefStruct<TV> ttshActions[] = {
    { ACTION_LEFT,     &TV::doTimeStretchLeft },
    { ACTION_RIGHT,    &TV::doTimeStretchRight },
    { ACTION_DOWN,     &TV::doTimeStretchDown },
    { ACTION_UP,       &TV::doTimeStretchUp },
    { "ADJUSTSTRETCH", &TV::doTimeStretchAdjStretch },
};
static int ttshActionCount = NELEMS(ttshActions);

bool TV::doTimeStretchLeft(const QString &action)
{
    ChangeTimeStretch(m_actionContext, -1);
    return true;
}

bool TV::doTimeStretchRight(const QString &action)
{
    ChangeTimeStretch(m_actionContext, 1);
    return true;
}

bool TV::doTimeStretchDown(const QString &action)
{
    ChangeTimeStretch(m_actionContext, -5);
    return true;
}

bool TV::doTimeStretchUp(const QString &action)
{
    ChangeTimeStretch(m_actionContext, 5);
    return true;
}

bool TV::doTimeStretchAdjStretch(const QString &action)
{
    ClearOSD(m_actionContext);
    return true;
}


bool TV::TimeStretchHandleAction(PlayerContext *ctx,
                                 const QStringList &actions)
{
    if (!stretchAdjustment)
        return false;

    if (!m_timeStretchActions)
        m_timeStretchActions = new MythActions<TV>(this, ttshActions,
                                                   ttshActionCount);

    m_actionContext = ctx;
    bool handled = m_timeStretchActions->handleActions(actions);

    return handled;
}

static struct ActionDefStruct<TV> tashActions[] = {
    { ACTION_LEFT,            &TV::doAudioSyncLeft },
    { ACTION_RIGHT,           &TV::doAudioSyncRight },
    { ACTION_DOWN,            &TV::doAudioSyncDown },
    { ACTION_UP,              &TV::doAudioSyncUp },
    { ACTION_TOGGELAUDIOSYNC, &TV::doAudioSyncToggle },
};
static int tashActionCount = NELEMS(tashActions);

bool TV::doAudioSyncLeft(const QString &action)
{
    ChangeAudioSync(m_actionContext, -1);
    return true;
}

bool TV::doAudioSyncRight(const QString &action)
{
    ChangeAudioSync(m_actionContext, 1);
    return true;
}

bool TV::doAudioSyncUp(const QString &action)
{
    ChangeAudioSync(m_actionContext, -10);
    return true;
}

bool TV::doAudioSyncDown(const QString &action)
{
    ChangeAudioSync(m_actionContext, 10);
    return true;
}

bool TV::doAudioSyncToggle(const QString &action)
{
    ClearOSD(m_actionContext);
    return true;
}

bool TV::AudioSyncHandleAction(PlayerContext *ctx,
                               const QStringList &actions)
{
    if (!audiosyncAdjustment)
        return false;

    if (!m_audioSyncActions)
        m_audioSyncActions = new MythActions<TV>(this, tashActions,
                                                 tashActionCount);

    m_actionContext = ctx;
    bool handled = m_audioSyncActions->handleActions(actions);

    return handled;
}

bool TV::DiscMenuHandleAction(PlayerContext *ctx, const QStringList &actions)
{
    int64_t pts = 0;
    VideoOutput *output = ctx->player->GetVideoOutput();
    if (output)
    {
        VideoFrame *frame = output->GetLastShownFrame();
        if (frame)
        {
            // convert timecode (msec) to pts (90kHz)
            pts = (int64_t)(frame->timecode  * 90);
        }
    }
    return ctx->buffer->HandleAction(actions, pts);
}

static struct ActionDefStruct<TV> t3dhActions[] = {
    { ACTION_3DSIDEBYSIDE,          &TV::do3dSideBySide },
    { ACTION_3DSIDEBYSIDEDISCARD,   &TV::do3dSideBySideDiscard },
    { ACTION_3DTOPANDBOTTOM,        &TV::do3dTopAndBottom },
    { ACTION_3DTOPANDBOTTOMDISCARD, &TV::do3dTopAndBottomDiscard }
};
static int t3dhActionCount = NELEMS(t3dhActions);

bool TV::do3dSideBySide(const QString &action)
{
    m_action3dMode = kStereoscopicModeSideBySide;
    return true;
}

bool TV::do3dSideBySideDiscard(const QString &action)
{
    m_action3dMode = kStereoscopicModeSideBySideDiscard;
    return true;
}

bool TV::do3dTopAndBottom(const QString &action)
{
    m_action3dMode = kStereoscopicModeTopAndBottom;
    return true;
}

bool TV::do3dTopAndBottomDiscard(const QString &action)
{
    m_action3dMode = kStereoscopicModeTopAndBottomDiscard;
    return true;
}

bool TV::Handle3D(PlayerContext *ctx, const QString &action)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player || !ctx->player->GetVideoOutput() ||
        !ctx->player->GetVideoOutput()->StereoscopicModesAllowed())
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return false;
    }

    if (!m_3dActions)
        m_3dActions = new MythActions<TV>(this, t3dhActions, t3dhActionCount);

    bool handled = m_3dActions->handleActions(QStringList(action));

    if (!handled)
        m_action3dMode = kStereoscopicModeNone;

    ctx->player->GetVideoOutput()->SetStereoscopicMode(m_action3dMode);
    SetOSDMessage(ctx, StereoscopictoString(m_action3dMode));
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return true;
}

static struct ActionDefStruct<TV> tahActions[] = {
    { "SKIPCOMMERCIAL",         &TV::doActiveSkipCommercial },
    { "SKIPCOMMBACK",           &TV::doActiveSkipCommBack },
    { "QUEUETRANSCODE",         &TV::doActiveQueueTranscode },
    { "QUEUETRANSCODE_AUTO",    &TV::doActiveQueueTranscodeAuto },
    { "QUEUETRANSCODE_HIGH",    &TV::doActiveQueueTranscodeHigh },
    { "QUEUETRANSCODE_MEDIUM",  &TV::doActiveQueueTranscodeMed },
    { "QUEUETRANSCODE_LOW",     &TV::doActiveQueueTranscodeLow },
    { ACTION_PLAY,              &TV::doActivePlay },
    { ACTION_PAUSE,             &TV::doActivePause },
    { "SPEEDINC",               &TV::doActiveSpeedInc },
    { "SPEEDDEC",               &TV::doActiveSpeedDec },
    { "ADJUSTSTRETCH",          &TV::doActiveAdjStretch },
    { "CYCLECOMMSKIPMODE",      &TV::doActiveCycleCommSkipMode },
    { "NEXTSCAN",               &TV::doActiveNextScan },
    { ACTION_SEEKARB,           &TV::doActiveSeekArb },
    { ACTION_JUMPRWND,          &TV::doActiveJumpRwnd },
    { ACTION_JUMPFFWD,          &TV::doActiveJumpFfwd },
    { ACTION_JUMPBKMRK,         &TV::doActiveJumpBkmrk },
    { ACTION_JUMPSTART,         &TV::doActiveJumpStart },
    { ACTION_CLEAROSD,          &TV::doActiveClearOsd },
    { ACTION_VIEWSCHEDULED,     &TV::doActiveViewScheduled },
    { ACTION_SIGNALMON,         &TV::doActiveSignalMon },
    { ACTION_SCREENSHOT,        &TV::doActiveScreenshot },
    { ACTION_STOP,              &TV::doActiveStop },
    { ACTION_EXITSHOWNOPROMPTS, &TV::doActiveExitNoPrompt },
    { "ESCAPE",                 &TV::doActiveEscape },
    { "BACK",                   &TV::doActiveEscape },
    { ACTION_ENABLEUPMIX,       &TV::doActiveEnableUpmix },
    { ACTION_DISABLEUPMIX,      &TV::doActiveDisableUpmix },
    { ACTION_VOLUMEDOWN,        &TV::doActiveVolDown },
    { ACTION_VOLUMEUP,          &TV::doActiveVolUp },
    { "CYCLEAUDIOCHAN",         &TV::doActiveCycleAudioChan },
    { ACTION_MUTEAUDIO,         &TV::doActiveMuteAudio },
    { "STRETCHINC",             &TV::doActiveStretchInc },
    { "STRETCHDEC",             &TV::doActiveStretchDec },
    { "MENU",                   &TV::doActiveMenu },
    { "INFO",                   &TV::doActiveInfo },
    { ACTION_TOGGLEOSDDEBUG,    &TV::doActiveToggleOsdDebug }
};
static int tahActionCount = NELEMS(tahActions);

bool TV::doActiveSkipCommercial(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();

    if (!isDVD)
        return false;

    DoSkipCommercials(m_actionContext, 1);
    return true;
}

bool TV::doActiveSkipCommBack(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();

    if (!isDVD)
        return false;

    DoSkipCommercials(m_actionContext, -1);
    return true;
}

bool TV::doActiveQueueTranscode(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();

    if (!isDVD)
        return false;

    DoQueueTranscode(m_actionContext, "Default");
    return true;
}

bool TV::doActiveQueueTranscodeAuto(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();

    if (!isDVD)
        return false;

    DoQueueTranscode(m_actionContext, "Autodetect");
    return true;
}

bool TV::doActiveQueueTranscodeHigh(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();

    if (!isDVD)
        return false;

    DoQueueTranscode(m_actionContext, "High Quality");
    return true;
}

bool TV::doActiveQueueTranscodeMed(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();

    if (!isDVD)
        return false;

    DoQueueTranscode(m_actionContext, "Medium Quality");
    return true;
}

bool TV::doActiveQueueTranscodeLow(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();

    if (!isDVD)
        return false;

    DoQueueTranscode(m_actionContext, "Low Quality");
    return true;
}

bool TV::doActivePlay(const QString &action)
{
    DoPlay(m_actionContext);
    return true;
}

bool TV::doActivePause(const QString &action)
{
    DoTogglePause(m_actionContext, true);
    return true;
}

bool TV::doActiveSpeedInc(const QString &action)
{
    ChangeSpeed(m_actionContext, 1);
    return true;
}

bool TV::doActiveSpeedDec(const QString &action)
{
    ChangeSpeed(m_actionContext, -1);
    return true;
}

bool TV::doActiveAdjStretch(const QString &action)
{
    ChangeTimeStretch(m_actionContext, 0);   // just display
    return true;
}

bool TV::doActiveCycleCommSkipMode(const QString &action)
{
    SetAutoCommercialSkip(m_actionContext, kCommSkipIncr);
    return true;
}

bool TV::doActiveNextScan(const QString &action)
{
    QString msg = QString::null;
    m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
    MythPlayer *play = m_actionContext->player;
    if (play)
    {
        play->NextScanType();
        msg = toString(play->GetScanType());
    }
    m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);

    if (!msg.isEmpty())
        SetOSDMessage(m_actionContext, msg);
    return true;
}

bool TV::doActiveSeekArb(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();

    if (isDVD)
        return false;

    if (asInputMode)
    {
        ClearInputQueues(m_actionContext, true);
        SetOSDText(m_actionContext, "osd_input", "osd_number_entry",
                   tr("Seek:"), kOSDTimeout_Med);

        QMutexLocker locker(&timerIdLock);
        asInputMode = false;
        if (asInputTimerId)
        {
            KillTimer(asInputTimerId);
            asInputTimerId = 0;
        }
    }
    else
    {
        ClearInputQueues(m_actionContext, false);
        AddKeyToInputQueue(m_actionContext, 0);

        QMutexLocker locker(&timerIdLock);
        asInputMode        = true;
        ccInputMode        = false;
        asInputTimerId = StartTimer(kInputModeTimeout, __LINE__);
        if (ccInputTimerId)
        {
            KillTimer(ccInputTimerId);
            ccInputTimerId = 0;
        }
    }
    return true;
}

bool TV::doActiveJumpRwnd(const QString &action)
{
    DoJumpRWND(m_actionContext);
    return true;
}

bool TV::doActiveJumpFfwd(const QString &action)
{
    DoJumpFFWD(m_actionContext);
    return true;
}

bool TV::doActiveJumpBkmrk(const QString &action)
{
    m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
    MythPlayer *play   = m_actionContext->player;
    long long bookmark = play->GetBookmark();
    long long curloc   = play->GetFramesPlayed();
    float     rate     = play->GetFrameRate();
    long long seekloc = (long long) ((bookmark - curloc) / rate);
    m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);

    if (bookmark > rate)
        DoSeek(m_actionContext, seekloc, tr("Jump to Bookmark"));
    return true;
}

bool TV::doActiveJumpStart(const QString &action)
{
    long long seekloc = +1;
    m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
    MythPlayer *play = m_actionContext->player;
    seekloc = (int64_t) (-1.0 * play->GetFramesPlayed() / play->GetFrameRate());
    m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);

    if (seekloc <= 0)
        DoSeek(m_actionContext, seekloc, tr("Jump to Beginning"));
    return true;
}

bool TV::doActiveClearOsd(const QString &action)
{
    ClearOSD(m_actionContext);
    return true;
}

bool TV::doActiveViewScheduled(const QString &action)
{
    EditSchedule(m_actionContext, kViewSchedule);
    return true;
}

bool TV::doActiveSignalMon(const QString &action)
{
    if ((GetState(m_actionContext) == kState_WatchingLiveTV) &&
        m_actionContext->recorder)
    {
        QString input = m_actionContext->recorder->GetInput();
        uint timeout  = m_actionContext->recorder->GetSignalLockTimeout(input);

        if (timeout == 0xffffffff)
        {
            SetOSDMessage(m_actionContext, "No Signal Monitor");
            return false;
        }

        int rate   = sigMonMode ? 0 : 100;
        int notify = sigMonMode ? 0 : 1;

        PauseLiveTV(m_actionContext);
        m_actionContext->recorder->SetSignalMonitoringRate(rate, notify);
        UnpauseLiveTV(m_actionContext);

        lockTimerOn = false;
        sigMonMode  = !sigMonMode;
    }
    return true;
}

bool TV::doActiveScreenshot(const QString &action)
{
    m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
    if (!m_actionContext->player || !m_actionContext->player->GetScreenShot())
        GetMythMainWindow()->ScreenShot();

    // otherwise VideoOutput has saved screenshot
    m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);
    return true;
}

bool TV::doActiveStop(const QString &action)
{
    PrepareToExitPlayer(m_actionContext, __LINE__, false);
    SetExitPlayer(true, true);
    return true;
}

bool TV::doActiveExitNoPrompt(const QString &action)
{
    requestDelete = false;
    PrepareToExitPlayer(m_actionContext, __LINE__);
    SetExitPlayer(true, true);
    return true;
}

bool TV::doActiveEscape(const QString &action)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();
    bool isDVDStill = m_actionContext->buffer &&
                      m_actionContext->buffer->IsInDiscMenuOrStillFrame();

    if (StateIsLiveTV(m_actionContext->GetState()) &&
        (m_actionContext->lastSignalMsgTime.elapsed() <
         (int)PlayerContext::kSMExitTimeout))
    {
        ClearOSD(m_actionContext);
    }
    else
    {
        OSD *osd = GetOSDLock(m_actionContext);
        if (osd && osd->IsVisible())
        {
            ClearOSD(m_actionContext);
            ReturnOSDLock(m_actionContext, osd);
            return true;
        }
        ReturnOSDLock(m_actionContext, osd);
    }

    NormalSpeed(m_actionContext);

    StopFFRew(m_actionContext);

    bool do_exit = false;

    if (StateIsLiveTV(GetState(m_actionContext)))
    {
        if (m_actionContext->HasPlayer() && (db_playback_exit_prompt & 0x0C))
        {
            ShowOSDStopWatchingRecording(m_actionContext);
            return true;
        }
        else
        {
            do_exit = true;
        }
    }
    else
    {
        if (m_actionContext->HasPlayer() && (db_playback_exit_prompt & 0x05) &&
            !underNetworkControl && !isDVDStill)
        {
            ShowOSDStopWatchingRecording(m_actionContext);
            return true;
        }
        PrepareToExitPlayer(m_actionContext, __LINE__,
                            (db_playback_exit_prompt == 0x02));
        requestDelete = false;
        do_exit = true;
    }

    if (do_exit)
    {
        PlayerContext *mctx = GetPlayer(m_actionContext, 0);
        if (mctx != m_actionContext)
        { // A PIP is active, just tear it down..
            PxPTeardownView(m_actionContext);
            return true;
        }
        else
        {
            // If it's a DVD, and we're not trying to execute a
            // jumppoint, and it's not in a menu, then first try
            // jumping to the title or root menu.
            MythPlayer *play = m_actionContext->player;
            if (isDVD && !GetMythMainWindow()->IsExitingToMain() &&
                action == "BACK" &&
                !m_actionContext->buffer->DVD()->IsInMenu() &&
                (play->GoToMenu("title") || play->GoToMenu("root")))
            {
                return true;
            }
            SetExitPlayer(true, true);
        }
    }

    SetActive(m_actionContext, 0, false);
    return true;
}

bool TV::doActiveEnableUpmix(const QString &action)
{
    EnableUpmix(m_actionContext, true);
    return true;
}

bool TV::doActiveDisableUpmix(const QString &action)
{
    EnableUpmix(m_actionContext, false);
    return true;
}

bool TV::doActiveVolDown(const QString &action)
{
    ChangeVolume(m_actionContext, false);
    return true;
}

bool TV::doActiveVolUp(const QString &action)
{
    ChangeVolume(m_actionContext, true);
    return true;
}

bool TV::doActiveCycleAudioChan(const QString &action)
{
    ToggleMute(m_actionContext, true);
    return true;
}

bool TV::doActiveMuteAudio(const QString &action)
{
    ToggleMute(m_actionContext);
    return true;
}

bool TV::doActiveStretchInc(const QString &action)
{
    ChangeTimeStretch(m_actionContext, 1);
    return true;
}

bool TV::doActiveStretchDec(const QString &action)
{
    ChangeTimeStretch(m_actionContext, -1);
    return true;
}

bool TV::doActiveMenu(const QString &action)
{
    ShowOSDMenu(m_actionContext);
    return true;
}

bool TV::doActiveInfo(const QString &action)
{
    if (HasQueuedInput())
    {
        DoArbSeek(m_actionContext, ARBSEEK_SET);
    }
    else
        ToggleOSD(m_actionContext, true);
    return true;
}

bool TV::doActiveToggleOsdDebug(const QString &action)
{
    ToggleOSDDebug(m_actionContext);
    return true;
}


bool TV::ActiveHandleAction(PlayerContext *ctx, const QStringList &actions)
{
    if (!m_activeActions)
        m_activeActions = new MythActions<TV>(this, tahActions, tahActionCount);

    bool handled = m_activeActions->handleActions(actions);

    if (!handled)
        handled = HandleJumpToProgramAction(ctx, actions);

    if (!handled && (!m_actionContext->buffer ||
                     !m_actionContext->buffer->IsInDiscMenuOrStillFrame()))
        handled = SeekHandleAction(ctx, actions);

    if (!handled)
        handled = HandleTrackAction(ctx, actions);

    return handled;
}

static struct ActionDefStruct<TV> tfrhActions[] = {
    { "^[0123456789]$", &TV::doFFRwndDigit }
};
static int tfrhActionCount = NELEMS(tfrhActions);

bool TV::doFFRwndDigit(const QString &action)
{
    int val = action.toInt();

    if (val < (int)ff_rew_speeds.size())
    {
        SetFFRew(m_actionContext, val);
        return true;
    }
    return false;
}


bool TV::FFRewHandleAction(PlayerContext *ctx, const QStringList &actions)
{
    bool handled = false;

    if (ctx->ff_rew_state)
    {
        m_actionContext = ctx;
        if (!m_ffrwndActions)
            m_ffrwndActions = new MythActions<TV>(this, tfrhActions,
                                                  tfrhActionCount);
        handled = m_ffrwndActions->handleActions(actions);

        if (!handled)
        {
            DoPlayerSeek(ctx, StopFFRew(ctx));
            UpdateOSDSeekMessage(ctx, ctx->GetPlayMessage(), kOSDTimeout_Med);
            handled = true;
        }
    }

    if (ctx->ff_rew_speed)
    {
        NormalSpeed(ctx);
        UpdateOSDSeekMessage(ctx, ctx->GetPlayMessage(), kOSDTimeout_Med);
        handled = true;
    }

    return handled;
}

static struct ActionDefStruct<TV> ttoghActions[] = {
    { "TOGGLEASPECT",              &TV::doToggleAspect },
    { "TOGGLEFILL",                &TV::doToggleFill },
    { ACTION_TOGGELAUDIOSYNC,      &TV::doToggleAudioSync },
    { ACTION_TOGGLEVISUALISATION,  &TV::doToggleVisualisation },
    { ACTION_ENABLEVISUALISATION,  &TV::doToggleEnableVis },
    { ACTION_DISABLEVISUALISATION, &TV::doToggleDisableVis },
    { "TOGGLEPICCONTROLS",         &TV::doTogglePicControls },
    { ACTION_TOGGLESTUDIOLEVELS,   &TV::doToggleStudioLevels },
    { ACTION_TOGGLENIGHTMODE,      &TV::doToggleNightMode },
    { "TOGGLESTRETCH",             &TV::doToggleStretch },
    { ACTION_TOGGLEUPMIX,          &TV::doToggleUpMix },
    { ACTION_TOGGLESLEEP,          &TV::doToggleSleep },
    { ACTION_TOGGLERECORD,         &TV::doToggleRecord },
    { ACTION_TOGGLEFAV,            &TV::doToggleFav },
    { ACTION_TOGGLECHANCONTROLS,   &TV::doToggleChanControls },
    { ACTION_TOGGLERECCONTROLS,    &TV::doToggleRecControls },
    { ACTION_TOGGLEINPUTS,         &TV::doToggleInputs },
    { "TOGGLEBROWSE",              &TV::doToggleBrowse },
    { "EDIT",                      &TV::doToggleEdit }
};
static int ttoghActionCount = NELEMS(ttoghActions);

bool TV::doToggleAspect(const QString &action)
{
    ToggleAspectOverride(m_actionContext);
    return true;
}

bool TV::doToggleFill(const QString &action)
{
    ToggleAdjustFill(m_actionContext);
    return true;
}

bool TV::doToggleAudioSync(const QString &action)
{
    ChangeAudioSync(m_actionContext, 0);   // just display
    return true;
}

bool TV::doToggleVisualisation(const QString &action)
{
    EnableVisualisation(m_actionContext, false, true /*toggle*/);
    return true;
}

bool TV::doToggleEnableVis(const QString &action)
{
    EnableVisualisation(m_actionContext, true);
    return true;
}

bool TV::doToggleDisableVis(const QString &action)
{
    EnableVisualisation(m_actionContext, false);
    return true;
}

bool TV::doTogglePicControls(const QString &action)
{
    DoTogglePictureAttribute(m_actionContext, kAdjustingPicture_Playback);
    return true;
}

bool TV::doToggleStudioLevels(const QString &action)
{
    DoToggleStudioLevels(m_actionContext);
    return true;
}

bool TV::doToggleNightMode(const QString &action)
{
    DoToggleNightMode(m_actionContext);
    return true;
}

bool TV::doToggleStretch(const QString &action)
{
    ToggleTimeStretch(m_actionContext);
    return true;
}

bool TV::doToggleUpMix(const QString &action)
{
    EnableUpmix(m_actionContext, false, true);
    return true;
}

bool TV::doToggleSleep(const QString &action)
{
    ToggleSleepTimer(m_actionContext);
    return true;
}

bool TV::doToggleRecord(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        ToggleRecord(m_actionContext);
    return islivetv;
}

bool TV::doToggleFav(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        ToggleChannelFavorite(m_actionContext);
    return islivetv;
}

bool TV::doToggleChanControls(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        DoTogglePictureAttribute(m_actionContext, kAdjustingPicture_Channel);
    return islivetv;
}

bool TV::doToggleRecControls(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        DoTogglePictureAttribute(m_actionContext, kAdjustingPicture_Recording);
    return islivetv;
}

bool TV::doToggleInputs(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv && !m_actionContext->pseudoLiveTVState)
    {
        ToggleInputs(m_actionContext);
        return true;
    }
    return false;
}

bool TV::doToggleBrowse(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
    {
        browsehelper->BrowseStart(m_actionContext);
        return true;
    }

    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();
    if (!isDVD)
    {
        ShowOSDMenu(m_actionContext);
        return true;
    }

    return false;
}

bool TV::doToggleEdit(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
    {
        StartChannelEditMode(m_actionContext);
        return true;
    }

    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();
    if (!isDVD)
    {
        StartProgramEditMode(m_actionContext);
        return true;
    }

    return false;
}

bool TV::ToggleHandleAction(PlayerContext *ctx, const QStringList &actions)
{
    m_actionContext = ctx;
    if (!m_toggleActions)
        m_toggleActions = new MythActions<TV>(this, ttoghActions,
                                              ttoghActionCount);
    bool handled = m_toggleActions->handleActions(actions);

    return handled;
}

void TV::EnableVisualisation(const PlayerContext *ctx, bool enable,
                            bool toggle, const QString &action)
{
    QString visualiser = QString("");
    if (action.startsWith("VISUALISER"))
        visualiser = action.mid(11);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player && ctx->player->CanVisualise())
    {
        bool want = enable || !visualiser.isEmpty();
        if (toggle && visualiser.isEmpty())
            want = !ctx->player->IsVisualising();
        bool on = ctx->player->EnableVisualisation(want, visualiser);
        SetOSDMessage(ctx, on ? ctx->player->GetVisualiserName() :
                                tr("Visualisation Off"));
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

static struct ActionDefStruct<TV> tpiphActions[] = {
    { "TOGGLEPIPMODE",  &TV::doPipEnqueueAction },
    { "TOGGLEPBPMODE",  &TV::doPipEnqueueAction },
    { "CREATEPIPVIEW",  &TV::doPipEnqueueAction },
    { "CREATEPBPVIEW",  &TV::doPipEnqueueAction },
    { "SWAPPIP",        &TV::doPipEnqueueAction },
    { "TOGGLEPIPSTATE", &TV::doPipEnqueueAction },
    { "NEXTPIPWINDOW",  &TV::doPipNextPipWindow }
};
static int tpiphActionCount = NELEMS(tpiphActions);

bool TV::doPipEnqueueAction(const QString &action)
{
    changePxP.enqueue(action);
    return true;
}

bool TV::doPipNextPipWindow(const QString &action)
{
    SetActive(m_actionContext, -1, true);
    return true;
}

bool TV::PxPHandleAction(PlayerContext *ctx, const QStringList &actions)
{
    if (!IsPIPSupported(ctx) && !IsPBPSupported(ctx))
        return false;

    QMutexLocker locker(&timerIdLock);

    m_actionContext = ctx;
    if (!m_pipActions)
        m_pipActions = new MythActions<TV>(this, tpiphActions,
                                           tpiphActionCount);
    bool handled = m_pipActions->handleActions(actions);

    if (!changePxP.empty() && !pipChangeTimerId)
        pipChangeTimerId = StartTimer(1, __LINE__);

    return handled;
}

void TV::SetBookmark(PlayerContext *ctx, bool clear)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        if (clear)
        {
            ctx->player->SetBookmark(true);
            SetOSDMessage(ctx, QObject::tr("Bookmark Cleared"));
        }
        else if (IsBookmarkAllowed(ctx))
        {
            ctx->player->SetBookmark();
            osdInfo info;
            ctx->CalcPlayerSliderPosition(info);
            info.text["title"] = QObject::tr("Position");
            UpdateOSDStatus(ctx, info, kOSDFunctionalType_Default,
                            kOSDTimeout_Med);
            SetOSDMessage(ctx, QObject::tr("Bookmark Saved"));
        }
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

static struct ActionDefStruct<TV> tapqhActions[] = {
    { ACTION_SELECT,            &TV::doPostQSelect },
    { "NEXTFAV",                &TV::doPostQNextFav },
    { "NEXTSOURCE",             &TV::doPostQNextSource },
    { "PREVSOURCE",             &TV::doPostQPrevSource },
    { "NEXTINPUT",              &TV::doPostQNextInput },
    { "NEXTCARD",               &TV::doPostQNextCard },
    { ACTION_GUIDE,             &TV::doPostQGuide },
    { "PREVCHAN",               &TV::doPostQPrevChan },
    { ACTION_CHANNELUP,         &TV::doPostQChanUp },
    { ACTION_CHANNELDOWN,       &TV::doPostQChanDown },
    { "DELETE",                 &TV::doPostQDelete },
    { ACTION_JUMPTODVDROOTMENU, &TV::doPostQDVDRootMenu },
    { ACTION_JUMPTOPOPUPMENU,   &TV::doPostQPopupMenu },
    { ACTION_FINDER,            &TV::doPostQFinder }
};
static int tapqhActionCount = NELEMS(tapqhActions);

bool TV::doPostQSelect(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));

    if (!islivetv || !CommitQueuedInput(m_actionContext))
    {
        m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
        SetBookmark(m_actionContext, (db_toggle_bookmark &&
                                      m_actionContext->player->GetBookmark()));
        m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);
    }
    return true;
}

bool TV::doPostQNextFav(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));

    if (islivetv)
        ChangeChannel(m_actionContext, CHANNEL_DIRECTION_FAVORITE);
    return islivetv;
}

bool TV::doPostQNextSource(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        SwitchSource(m_actionContext, kNextSource);
    return islivetv;
}

bool TV::doPostQPrevSource(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        SwitchSource(m_actionContext, kPreviousSource);
    return islivetv;
}

bool TV::doPostQNextInput(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        ToggleInputs(m_actionContext);
    return islivetv;
}

bool TV::doPostQNextCard(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        SwitchCards(m_actionContext);
    return islivetv;
}

bool TV::doPostQGuide(const QString &action)
{
    EditSchedule(m_actionContext, kScheduleProgramGuide);
    return true;
}

bool TV::doPostQPrevChan(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
        PopPreviousChannel(m_actionContext, false);
    return islivetv;
}

bool TV::doPostQChanUp(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
    {
        if (db_browse_always)
            browsehelper->BrowseDispInfo(m_actionContext, BROWSE_UP);
        else
            ChangeChannel(m_actionContext, CHANNEL_DIRECTION_UP);
    }
    else
        DoJumpRWND(m_actionContext);
    return true;
}

bool TV::doPostQChanDown(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (islivetv)
    {
        if (db_browse_always)
            browsehelper->BrowseDispInfo(m_actionContext, BROWSE_DOWN);
        else
            ChangeChannel(m_actionContext, CHANNEL_DIRECTION_DOWN);
    }
    else
        DoJumpFFWD(m_actionContext);
    return true;
}

bool TV::doPostQDelete(const QString &action)
{
    bool islivetv = StateIsLiveTV(GetState(m_actionContext));
    if (!islivetv)
    {
        NormalSpeed(m_actionContext);
        StopFFRew(m_actionContext);
        SetBookmark(m_actionContext);
        ShowOSDPromptDeleteRecording(m_actionContext,
                                     tr("Are you sure you want to delete:"));
    }
    return !islivetv;
}

bool TV::doPostQDVDRootMenu(const QString &action)
{
    TVState state = GetState(m_actionContext);
    bool isdisc  = (state == kState_WatchingDVD) ||
                   (state == kState_WatchingBD);
    if (isdisc)
    {
        m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
        if (m_actionContext->player)
            m_actionContext->player->GoToMenu("root");
        m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);
    }
    return isdisc;
}

bool TV::doPostQPopupMenu(const QString &action)
{
    TVState state = GetState(m_actionContext);
    bool isdisc  = (state == kState_WatchingDVD) ||
                   (state == kState_WatchingBD);
    if (isdisc)
    {
        m_actionContext->LockDeletePlayer(__FILE__, __LINE__);
        if (m_actionContext->player)
            m_actionContext->player->GoToMenu("popup");
        m_actionContext->UnlockDeletePlayer(__FILE__, __LINE__);
    }
    return isdisc;
}

bool TV::doPostQFinder(const QString &action)
{
    EditSchedule(m_actionContext, kScheduleProgramFinder);
    return true;
}


bool TV::ActivePostQHandleAction(PlayerContext *ctx, const QStringList &actions)
{
    m_actionContext = ctx;
    if (!m_activePostQActions)
        m_activePostQActions = new MythActions<TV>(this, tapqhActions,
                                                   tapqhActionCount);
    bool handled = m_activePostQActions->handleActions(actions);

    return handled;
}

static struct ActionDefStruct<TV> tncActions[] = {
    { "CHANID",  &TV::doNCChanId },
    { "CHANNEL", &TV::doNCChannel },
    { "SPEED",   &TV::doNCSpeed },
    { "STOP",    &TV::doNCStop },
    { "SEEK",    &TV::doNCSeek },
    { "VOLUME",  &TV::doNCVolume },
    { "QUERY",   &TV::doNCQuery }
};
static int tncActionCount = NELEMS(tncActions);

bool TV::doNCChanId(const QString &action)
{
    if (m_actionNCArgs.size() != 3)
        return false;

    queuedChanID = m_actionNCArgs[2].toUInt();
    queuedChanNum = QString::null;
    CommitQueuedInput(m_actionNCContext);
    return true;
}

bool TV::doNCChannel(const QString &action)
{
    if (m_actionNCArgs.size() != 3)
        return false;

    if (StateIsLiveTV(GetState(m_actionNCContext)))
    {
        QString dir(m_actionNCArgs[2]);
        if (dir == "UP")
            ChangeChannel(m_actionNCContext, CHANNEL_DIRECTION_UP);
        else if (dir == "DOWN")
            ChangeChannel(m_actionNCContext, CHANNEL_DIRECTION_DOWN);
        else if (dir.contains(QRegExp("^[-\\.\\d_#]+$")))
            ChangeChannel(m_actionNCContext, 0, dir);
    }
    return true;
}

bool TV::doNCSpeed(const QString &action)
{
    if (m_actionNCArgs.size() != 3)
        return false;

    bool paused = ContextIsPaused(m_actionNCContext, __FILE__, __LINE__);
    QString speed = m_actionNCArgs[2];

    if (speed == "0x")
    {
        NormalSpeed(m_actionNCContext);
        StopFFRew(m_actionNCContext);
        if (!paused)
            DoTogglePause(m_actionNCContext, true);
        return true;
    }

    float tmpSpeed = 1.0f;
    bool ok = false;

    if (speed.contains(QRegExp("^\\-*\\d+x$")))
    {
        speed.chop(1);
        tmpSpeed = speed.toFloat(&ok);
    }
    else if (speed.contains(QRegExp("^\\-*\\d*\\.\\d+x$")))
    {
        speed.chop(1);
        tmpSpeed = speed.toFloat(&ok);
    }
    else
    {
        QRegExp re = QRegExp("^(\\-*\\d+)\\/(\\d+)x$");
        if (speed.contains(re))
        {
            QStringList matches = re.capturedTexts();

            int numerator, denominator;
            numerator = matches[1].toInt(&ok);
            denominator = matches[2].toInt(&ok);

            if (ok && denominator != 0)
                tmpSpeed = ((float)numerator) / ((float)denominator);
            else
                ok = false;
        }
    }

    if (!ok)
    {
        LOG(VB_GENERAL, LOG_ERR,
            QString("Found an unknown speed of %1").arg(speed));
        return true;
    }

    float searchSpeed = fabs(tmpSpeed);
    unsigned int index;

    if (paused)
        DoTogglePause(m_actionNCContext, true);

    if (tmpSpeed == 0.0f)
    {
        NormalSpeed(m_actionNCContext);
        StopFFRew(m_actionNCContext);

        if (!paused)
            DoTogglePause(m_actionNCContext, true);
    }
    else if (tmpSpeed == 1.0f)
    {
        StopFFRew(m_actionNCContext);
        m_actionNCContext->ts_normal = 1.0f;
        ChangeTimeStretch(m_actionNCContext, 0, false);

        ReturnPlayerLock(m_actionNCContext);
        return true;
    }

    NormalSpeed(m_actionNCContext);

    int searchSpdInt = (int)searchSpeed;
    for (index = 0; index < ff_rew_speeds.size(); index++)
        if (ff_rew_speeds[index] == searchSpdInt)
            break;

    if (index < ff_rew_speeds.size() && ff_rew_speeds[index] == searchSpdInt)
    {
        if (tmpSpeed < 0)
            m_actionNCContext->ff_rew_state = -1;
        else if (tmpSpeed > 1)
            m_actionNCContext->ff_rew_state = 1;
        else
            StopFFRew(m_actionNCContext);

        if (m_actionNCContext->ff_rew_state)
            SetFFRew(m_actionNCContext, index);
    }
    else if (0.48 <= tmpSpeed && tmpSpeed <= 2.0) {
        StopFFRew(m_actionNCContext);

        // alter speed before display
        m_actionNCContext->ts_normal = tmpSpeed;
        ChangeTimeStretch(m_actionNCContext, 0, false);
    }
    else
    {
        LOG(VB_GENERAL, LOG_WARNING,
            QString("Couldn't find %1 speed. Setting Speed to 1x")
                .arg(searchSpeed));

        m_actionNCContext->ff_rew_state = 0;
        SetFFRew(m_actionNCContext, kInitFFRWSpeed);
    }
    return true;
}

bool TV::doNCStop(const QString &action)
{
    if (m_actionNCArgs.size() != 2)
        return false;

    SetBookmark(m_actionNCContext);
    m_actionNCContext->LockDeletePlayer(__FILE__, __LINE__);
    if (m_actionNCContext->player && db_auto_set_watched)
        m_actionNCContext->player->SetWatched();
    m_actionNCContext->UnlockDeletePlayer(__FILE__, __LINE__);
    SetExitPlayer(true, true);
    return true;
}

bool TV::doNCSeek(const QString &action)
{
    if (m_actionNCArgs.size() < 3 || !m_actionNCContext->HasPlayer())
        return false;

    if (m_actionNCContext->buffer &&
        m_actionNCContext->buffer->IsInDiscMenuOrStillFrame())
        return true;

    m_actionNCContext->LockDeletePlayer(__FILE__, __LINE__);
    long long fplay = 0;
    QString where(m_actionNCArgs[2]);
    if (m_actionNCContext->player &&
        (where == "BEGINNING" || where == "POSITION"))
    {
        fplay = m_actionNCContext->player->GetFramesPlayed();
    }
    m_actionNCContext->UnlockDeletePlayer(__FILE__, __LINE__);

    if (where == "BEGINNING")
        DoSeek(m_actionNCContext, -fplay, tr("Jump to Beginning"));
    else if (where == "FORWARD")
        DoSeek(m_actionNCContext, m_actionNCContext->fftime, tr("Skip Ahead"));
    else if (where == "BACKWARD")
        DoSeek(m_actionNCContext, -m_actionNCContext->rewtime, tr("Skip Back"));
    else if ((where == "POSITION") && (m_actionNCArgs.size() == 4) &&
             (m_actionNCArgs[3].contains(QRegExp("^\\d+$"))))
    {
        DoSeekAbsolute(m_actionNCContext, m_actionNCArgs[3].toInt());
    }
    return true;
}

bool TV::doNCVolume(const QString &action)
{
    if (m_actionNCArgs.size() < 3)
        return false;

    QRegExp re = QRegExp("(\\d+)%");
    if (!m_actionNCArgs[2].contains(re))
        return true;

    QStringList matches = re.capturedTexts();

    bool ok = false;
    int vol = matches[1].toInt(&ok);
    if (!ok || vol < 0 || vol > 100)
        return true;

    LOG(VB_GENERAL, LOG_INFO, QString("Set Volume to %1%") .arg(vol));

    m_actionNCContext->LockDeletePlayer(__FILE__, __LINE__);
    if (!m_actionNCContext->player)
    {
        m_actionNCContext->UnlockDeletePlayer(__FILE__, __LINE__);
        return true;
    }

    vol -= m_actionNCContext->player->GetVolume();
    vol = m_actionNCContext->player->AdjustVolume(vol);
    m_actionNCContext->UnlockDeletePlayer(__FILE__, __LINE__);

    if (!browsehelper->IsBrowsing() && !editmode)
    {
        UpdateOSDStatus(m_actionNCContext, tr("Adjust Volume"), tr("Volume"),
                        QString::number(vol),
                        kOSDFunctionalType_PictureAdjust, "%", vol * 10,
                        kOSDTimeout_Med);
        SetUpdateOSDPosition(false);
    }
    return true;
}

bool TV::doNCQuery(const QString &action)
{
    if (m_actionNCArgs.size() < 3)
        return false;

    QString param(m_actionNCArgs[2]);
    if (param == "POSITION")
    {
        QString speedStr;
        if (ContextIsPaused(m_actionNCContext, __FILE__, __LINE__))
        {
            speedStr = "pause";
        }
        else if (m_actionNCContext->ff_rew_state)
        {
            speedStr = QString("%1x").arg(m_actionNCContext->ff_rew_speed);
        }
        else
        {
            QRegExp re = QRegExp("Play (.*)x");
            if (QString(m_actionNCContext->GetPlayMessage()).contains(re))
            {
                QStringList matches = re.capturedTexts();
                speedStr = QString("%1x").arg(matches[1]);
            }
            else
            {
                speedStr = "1x";
            }
        }

        osdInfo info;
        m_actionNCContext->CalcPlayerSliderPosition(info, true);

        QDateTime respDate = mythCurrentDateTime();
        QString infoStr = "";

        m_actionNCContext->LockDeletePlayer(__FILE__, __LINE__);
        long long fplay = 0;
        float     rate  = 30.0f;
        if (m_actionNCContext->player)
        {
            fplay = m_actionNCContext->player->GetFramesPlayed();
            rate  = m_actionNCContext->player->GetFrameRate();
        }
        m_actionNCContext->UnlockDeletePlayer(__FILE__, __LINE__);

        m_actionNCContext->LockPlayingInfo(__FILE__, __LINE__);
        if (m_actionNCContext->GetState() == kState_WatchingLiveTV)
        {
            infoStr = "LiveTV";
            if (m_actionNCContext->playingInfo)
                respDate =
                    m_actionNCContext->playingInfo->GetScheduledStartTime();
        }
        else
        {
            if (m_actionNCContext->buffer->IsDVD())
                infoStr = "DVD";
            else if (m_actionNCContext->playingInfo->IsRecording())
                infoStr = "Recorded";
            else
                infoStr = "Video";

            if (m_actionNCContext->playingInfo)
                respDate =
                    m_actionNCContext->playingInfo->GetRecordingStartTime();
        }

        if ((infoStr == "Recorded") || (infoStr == "LiveTV"))
        {
            infoStr += QString(" %1 %2 %3 %4 %5 %6 %7")
                .arg(info.text["description"])
                .arg(speedStr)
                .arg(m_actionNCContext->playingInfo ?
                     m_actionNCContext->playingInfo->GetChanID() : 0)
                .arg(respDate.toString(Qt::ISODate))
                .arg(fplay)
                .arg(m_actionNCContext->buffer->GetFilename())
                .arg(rate);
        }
        else
        {
            QString position = info.text["description"].section(" ",0,0);
            infoStr += QString(" %1 %2 %3 %4 %5")
                .arg(position)
                .arg(speedStr)
                .arg(m_actionNCContext->buffer->GetFilename())
                .arg(fplay)
                .arg(rate);
        }

        m_actionNCContext->UnlockPlayingInfo(__FILE__, __LINE__);

        QString message = QString("NETWORK_CONTROL ANSWER %1") .arg(infoStr);
        MythEvent me(message);
        gCoreContext->dispatch(me);
    }
    else if (param == "VOLUME")
    {
        QString infoStr = QString("%1%")
                              .arg(m_actionNCContext->player->GetVolume());

        QString message = QString("NETWORK_CONTROL ANSWER %1") .arg(infoStr);
        MythEvent me(message);
        gCoreContext->dispatch(me);
    }
    return true;
}


void TV::ProcessNetworkControlCommand(PlayerContext *ctx,
                                      const QString &command)
{
    bool ignoreKeys = ctx->IsPlayerChangingBuffers();
#ifdef DEBUG_ACTIONS
    LOG(VB_GENERAL, LOG_DEBUG, LOC + "ProcessNetworkControlCommand(" +
            QString("%1) ignoreKeys: %2").arg(command).arg(ignoreKeys));
#endif

    if (ignoreKeys)
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
                "Ignoring network control command"
                "\n\t\t\tbecause ignoreKeys is set");
        return;
    }

    QStringList m_actionNCArgs = command.split(" ", QString::SkipEmptyParts);
    if (m_actionNCArgs.size() < 2)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Not enough tokens"
                "in network control command" + "\n\t\t\t" +
                QString("'%1'").arg(command));
        return;
    }

    OSD *osd = GetOSDLock(ctx);
    bool dlg = false;
    if (osd)
        dlg = osd->DialogVisible();
    ReturnOSDLock(ctx, osd);

    if (dlg)
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            "Ignoring network control command\n\t\t\t" +
            QString("because dialog is waiting for a response"));
        return;
    }

    if (!m_networkControlActions)
        m_networkControlActions = new MythActions<TV>(this, tncActions,
                                                      tncActionCount);

    if (m_actionNCArgs[1] != "QUERY")
        ClearOSD(ctx);

    bool touched;
    m_actionNCContext = ctx;
    m_networkControlActions->handleAction(m_actionNCArgs[1], touched);
}

/**
 * \brief Setup Picture by Picture. right side will be the current video.
 * \param info programinfo for PBP to use for left Picture. is NULL for Live TV
 */
bool TV::CreatePBP(PlayerContext *ctx, const ProgramInfo *info)
{
    LOG(VB_PLAYBACK, LOG_INFO, LOC + "CreatePBP() -- begin");

    if (player.size() > 1)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "CreatePBP() -- end : "
                "only allowed when player.size() == 1");
        return false;
    }

    PlayerContext *mctx = GetPlayer(ctx, 0);
    if (!IsPBPSupported(mctx))
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "CreatePBP() -- end : "
                "PBP not supported by video method.");
        return false;
    }

    if (!mctx->player)
        return false;
    mctx->LockDeletePlayer(__FILE__, __LINE__);
    long long mctx_frame = mctx->player->GetFramesPlayed();
    mctx->UnlockDeletePlayer(__FILE__, __LINE__);

    // This is safe because we are already holding lock for a ctx
    player.push_back(new PlayerContext(kPBPPlayerInUseID));
    PlayerContext *pbpctx = player.back();
    if (noHardwareDecoders)
        pbpctx->SetNoHardwareDecoders();
    pbpctx->SetPIPState(kPBPRight);

    if (info)
    {
        pbpctx->SetPlayingInfo(info);
        pbpctx->SetInitialTVState(false);
        ScheduleStateChange(pbpctx);
    }
    else if (RequestNextRecorder(pbpctx, false))
    {
        pbpctx->SetInitialTVState(true);
        ScheduleStateChange(pbpctx);
    }
    else
    {
        delete player.back();
        player.pop_back();
        return false;
    }

    mctx->PIPTeardown();
    mctx->SetPIPState(kPBPLeft);
    mctx->buffer->Seek(0, SEEK_SET);

    if (StateIsLiveTV(mctx->GetState()))
        mctx->buffer->Unpause();

    bool ok = mctx->CreatePlayer(
        this, GetMythMainWindow(), mctx->GetState(), false);

    if (ok)
    {
        ScheduleStateChange(mctx);
        mctx->LockDeletePlayer(__FILE__, __LINE__);
        if (mctx->player)
            mctx->player->JumpToFrame(mctx_frame);
        mctx->UnlockDeletePlayer(__FILE__, __LINE__);
        SetSpeedChangeTimer(25, __LINE__);
    }
    else
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to restart new main context");
        // Make putative PBP context the main context
        swap(player[0],player[1]);
        player[0]->SetPIPState(kPIPOff);
        // End the old main context..
        ForceNextStateNone(mctx);
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("CreatePBP() -- end : %1").arg(ok));
    return ok;
}

/**
 * \brief create PIP.
 * \param info programinfo for PIP to create. is NULL for LiveTV PIP
 */
bool TV::CreatePIP(PlayerContext *ctx, const ProgramInfo *info)
{
    PlayerContext *mctx = GetPlayer(ctx, 0);
    if (!mctx)
        return false;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "CreatePIP -- begin");

    if (mctx->IsPBP())
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            "CreatePIP called, but we're in PBP mode already, ignoring.");
        return false;
    }

    if (!IsPIPSupported(mctx))
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "PiP not supported by video method.");
        return false;
    }

    PlayerContext *pipctx = new PlayerContext(kPIPPlayerInUseID);
    if (noHardwareDecoders)
        pipctx->SetNoHardwareDecoders();
    pipctx->SetNullVideo(true);
    pipctx->SetPIPState(kPIPonTV);
    if (info)
    {
        pipctx->SetPlayingInfo(info);
        pipctx->SetInitialTVState(false);
        ScheduleStateChange(pipctx);
    }
    else if (RequestNextRecorder(pipctx, false))
    {
        pipctx->SetInitialTVState(true);
        ScheduleStateChange(pipctx);
    }
    else
    {
        delete pipctx;
        return false;
    }

    // this is safe because we are already holding lock for ctx
    player.push_back(pipctx);

    return true;
}

int TV::find_player_index(const PlayerContext *ctx) const
{
    for (uint i = 0; i < player.size(); i++)
        if (GetPlayer(ctx, i) == ctx)
            return i;
    return -1;
}

bool TV::StartPlayer(PlayerContext *mctx, PlayerContext *ctx,
                     TVState desiredState)
{
    bool wantPiP = ctx->IsPIP();

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("StartPlayer(%1, %2, %3) -- begin")
            .arg(find_player_index(ctx)).arg(StateToString(desiredState))
            .arg((wantPiP) ? "PiP" : "main"));

    LOG(VB_PLAYBACK, LOG_INFO, LOC +
            QString("Elapsed time since TV constructor was called: %1 ms")
            .arg(ctorTime.elapsed()));

    if (wantPiP)
    {
        if (mctx->HasPlayer() && ctx->StartPIPPlayer(this, desiredState) &&
            ctx->HasPlayer() && PIPAddPlayer(mctx, ctx))
        {
            ScheduleStateChange(ctx);
            LOG(VB_GENERAL, LOG_INFO, "StartPlayer PiP -- end : ok");
            return true;
        }

        ForceNextStateNone(ctx);
        LOG(VB_GENERAL, LOG_INFO, "StartPlayer PiP -- end : !ok");
        return false;
    }

    bool ok = false;
    if (ctx->IsNullVideoDesired())
    {
        ok = ctx->CreatePlayer(this, NULL, desiredState, false);
        ScheduleStateChange(ctx);
        if (ok)
            ok = PIPAddPlayer(mctx, ctx);
    }
    else
    {
        ok = ctx->CreatePlayer(this, GetMythMainWindow(), desiredState, false);
        ScheduleStateChange(ctx);
    }

    if (ok)
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + QString("Created player."));
        SetSpeedChangeTimer(25, __LINE__);
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("StartPlayer(%1, %2, %3) -- end %4")
            .arg(find_player_index(ctx)).arg(StateToString(desiredState))
            .arg((wantPiP) ? "PiP" : "main").arg((ok) ? "ok" : "error"));

    return ok;
}

/// \brief Maps Player of software scaled PIP to the main player
bool TV::PIPAddPlayer(PlayerContext *mctx, PlayerContext *pipctx)
{
    if (!mctx || !pipctx)
        return false;

    if (!mctx->IsPlayerPlaying())
        return false;

    bool ok = false, addCondition = false;
    pipctx->LockDeletePlayer(__FILE__, __LINE__);
    if (pipctx->player)
    {
        bool is_using_null = pipctx->player->UsingNullVideo();
        pipctx->UnlockDeletePlayer(__FILE__, __LINE__);

        if (is_using_null)
        {
            addCondition = true;
            multi_lock(&mctx->deletePlayerLock, &pipctx->deletePlayerLock, NULL);
            if (mctx->player && pipctx->player)
            {
                PIPLocation loc = mctx->player->GetNextPIPLocation();
                if (loc != kPIP_END)
                    ok = mctx->player->AddPIPPlayer(pipctx->player, loc, 4000);
            }
            mctx->deletePlayerLock.unlock();
            pipctx->deletePlayerLock.unlock();
        }
        else if (pipctx->IsPIP())
        {
            ok = ResizePIPWindow(pipctx);
        }
    }
    else
        pipctx->UnlockDeletePlayer(__FILE__, __LINE__);

    LOG(VB_GENERAL, LOG_ERR,
        QString("AddPIPPlayer null: %1 IsPIP: %2 addCond: %3 ok: %4")
            .arg(pipctx->player->UsingNullVideo())
            .arg(pipctx->IsPIP()).arg(addCondition).arg(ok));

    return ok;
}

/// \brief Unmaps Player of software scaled PIP from the main player
bool TV::PIPRemovePlayer(PlayerContext *mctx, PlayerContext *pipctx)
{
    if (!mctx || !pipctx)
        return false;

    bool ok = false;
    multi_lock(&mctx->deletePlayerLock, &pipctx->deletePlayerLock, NULL);
    if (mctx->player && pipctx->player)
        ok = mctx->player->RemovePIPPlayer(pipctx->player, 4000);
    mctx->deletePlayerLock.unlock();
    pipctx->deletePlayerLock.unlock();

    LOG(VB_GENERAL, LOG_INFO, QString("PIPRemovePlayer ok: %1").arg(ok));

    return ok;
}

/// \brief start/stop PIP/PBP
void TV::PxPToggleView(PlayerContext *actx, bool wantPBP)
{
    if (wantPBP && !IsPBPSupported(actx))
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            "PxPToggleView() -- end: PBP not supported by video method.");
        return;
    }

    if (player.size() <= 1)
        PxPCreateView(actx, wantPBP);
    else
        PxPTeardownView(actx);
}

/// \brief start PIP/PBP
void TV::PxPCreateView(PlayerContext *actx, bool wantPBP)
{
    if (!actx)
        return;

    QString err_msg = QString::null;
    if ((player.size() > kMaxPBPCount) && (wantPBP || actx->IsPBP()))
    {
        err_msg = tr("Sorry, PBP only supports %n video stream(s)", "",
                     kMaxPBPCount);
    }

    if ((player.size() > kMaxPIPCount) &&
        (!wantPBP || GetPlayer(actx,1)->IsPIP()))
    {
        err_msg = tr("Sorry, PIP only supports %n video stream(s)", "",
                     kMaxPIPCount);
    }

    if ((player.size() > 1) && (wantPBP ^ actx->IsPBP()))
        err_msg = tr("Sorry, cannot mix PBP and PIP views");

    if (!err_msg.isEmpty())
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + err_msg);
        SetOSDMessage(actx, err_msg);
        return;
    }

    bool ok = false;
    if (wantPBP)
        ok = CreatePBP(actx, NULL);
    else
        ok = CreatePIP(actx, NULL);
    actx = GetPlayer(actx, -1); // CreatePBP/PIP mess with ctx's

    QString msg = (ok) ?
        ((wantPBP) ? tr("Creating PBP")      : tr("Creating PIP")) :
        ((wantPBP) ? tr("Cannot create PBP") : tr("Cannot create PIP"));

    SetOSDMessage(actx, msg);
}

/// \brief stop PIP/PBP
void TV::PxPTeardownView(PlayerContext *actx)
{
    LOG(VB_GENERAL, LOG_INFO, "PxPTeardownView()");

    QString msg;
    PlayerContext *mctx = GetPlayer(actx, 0);
    PlayerContext *dctx = NULL;
    dctx = (mctx != actx)       ? actx               : dctx;
    dctx = (2 == player.size()) ? GetPlayer(actx, 1) : dctx;

    SetActive(actx, 0, false);

    PlayerContext *ctx1 = GetPlayer(actx, 1);
    msg = (ctx1->IsPIP()) ? tr("Stopping PIP") : tr("Stopping PBP");
    if (dctx)
    {
        ForceNextStateNone(dctx);
    }
    else
    {
        if (player.size() > 2)
        {
            msg = (ctx1->IsPIP()) ?
                tr("Stopping all PIPs") : tr("Stopping all PBPs");
        }

        for (uint i = player.size() - 1; i > 0; i--)
            ForceNextStateNone(GetPlayer(actx,i));
    }

    SetOSDMessage(mctx, msg);
}

/**
* \brief Change PIP View from PIP to PBP and visa versa
*/
void TV::PxPToggleType(PlayerContext *mctx, bool wantPBP)
{
    const QString before = (mctx->IsPBP()) ? "PBP" : "PIP";
    const QString after  = (wantPBP)       ? "PBP" : "PIP";

    // TODO renderer may change depending on display profile
    //      check for support in new renderer
    if (wantPBP && !IsPBPSupported(mctx))
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            "PxPToggleType() -- end: PBP not supported by video method.");
        return;
    }


    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("PxPToggleType() converting from %1 to %2 -- begin")
            .arg(before).arg(after));

    if (mctx->IsPBP() == wantPBP)
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            "PxPToggleType() -- end: already in desired mode");
        return;
    }

    uint max_cnt = min(kMaxPBPCount, kMaxPIPCount+1);
    if (player.size() > max_cnt)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            QString("PxPToggleType() -- end: # player contexts must be %1 or "
                    "less, but it is currently %1")
                .arg(max_cnt).arg(player.size()));

        QString err_msg = tr("Too many views to switch");

        PlayerContext *actx = GetPlayer(mctx, -1);
        SetOSDMessage(actx, err_msg);
        return;
    }

    for (uint i = 0; i < player.size(); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        if (!ctx->IsPlayerPlaying())
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "PxPToggleType() -- end: " +
                    QString("player #%1 is not active, exiting without "
                            "doing anything to avoid danger").arg(i));
            return;
        }
    }

    MuteState mctx_mute = kMuteOff;
    mctx->LockDeletePlayer(__FILE__, __LINE__);
    if (mctx->player)
        mctx_mute = mctx->player->GetMuteState();
    mctx->UnlockDeletePlayer(__FILE__, __LINE__);

    vector<long long> pos = TeardownAllPlayers(mctx);

    if (wantPBP)
    {
        GetPlayer(mctx, 0)->SetPIPState(kPBPLeft);
        GetPlayer(mctx, 1)->SetPIPState(kPBPRight);
    }
    else
    {
        GetPlayer(mctx, 0)->SetPIPState(kPIPOff);
        for (uint i = 1; i < player.size(); i++)
        {
            GetPlayer(mctx, i)->SetPIPState(kPIPonTV);
            GetPlayer(mctx, i)->SetNullVideo(true);
        }
    }

    RestartAllPlayers(mctx, pos, mctx_mute);

    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("PxPToggleType() converting from %1 to %2 -- end")
            .arg(before).arg(after));
}

/**
 * \brief resize PIP Window. done when changing channels or swapping PIP
 */
bool TV::ResizePIPWindow(PlayerContext *ctx)
{
    LOG(VB_PLAYBACK, LOG_INFO, LOC + "ResizePIPWindow -- begin");
    PlayerContext *mctx = GetPlayer(ctx, 0);
    if (mctx->HasPlayer() && ctx->HasPlayer())
    {
        QRect rect;

        multi_lock(&mctx->deletePlayerLock, &ctx->deletePlayerLock, (QMutex*)NULL);
        if (mctx->player && ctx->player)
        {
            PIPLocation loc = mctx->player->GetNextPIPLocation();
            LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("ResizePIPWindow -- loc %1")
                    .arg(loc));
            if (loc != kPIP_END)
            {
                rect = mctx->player->GetVideoOutput()->GetPIPRect(
                    loc, ctx->player, false);
            }
        }
        mctx->UnlockDeletePlayer(__FILE__, __LINE__);
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);

        if (rect.isValid())
        {
            ctx->ResizePIPWindow(rect);
            LOG(VB_PLAYBACK, LOG_INFO, LOC + "ResizePIPWindow -- end : ok");
            return true;
        }
    }
    LOG(VB_PLAYBACK, LOG_ERR, LOC + "ResizePIPWindow -- end : !ok");
    return false;
}

bool TV::IsPBPSupported(const PlayerContext *ctx) const
{
    const PlayerContext *mctx = NULL;
    if (ctx)
        mctx = GetPlayer(ctx, 0);
    else
        mctx = GetPlayerReadLock(0, __FILE__, __LINE__);

    bool yes = mctx->IsPBPSupported();

    if (!ctx)
        ReturnPlayerLock(mctx);

    return yes;
}

bool TV::IsPIPSupported(const PlayerContext *ctx) const
{
    const PlayerContext *mctx = NULL;
    if (ctx)
        mctx = GetPlayer(ctx, 0);
    else
        mctx = GetPlayerReadLock(0, __FILE__, __LINE__);

    bool yes = mctx->IsPIPSupported();

    if (!ctx)
        ReturnPlayerLock(mctx);

    return yes;
}

/** \brief Teardown all Player's in preparation for PxP Swap or
 *         change from PIP -> PBP or PBP -> PIP
 */
vector<long long> TV::TeardownAllPlayers(PlayerContext *lctx)
{
    vector<long long> pos;
    for (uint i = 0; i < player.size(); i++)
    {
        const PlayerContext *ctx = GetPlayer(lctx, i);
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        pos.push_back((ctx->player) ? ctx->player->GetFramesPlayed() : 0);
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    }

    for (uint i = 0; i < player.size(); i++)
    {
        PlayerContext *ctx = GetPlayer(lctx, i);
        ctx->PIPTeardown();
    }

    return pos;
}

/**
 * \brief tear down remaining PBP video and restore
 * fullscreen display
 */
void TV::PBPRestartMainPlayer(PlayerContext *mctx)
{
    LOG(VB_PLAYBACK, LOG_INFO, LOC  + "PBPRestartMainPlayer -- begin");

    if (!mctx->IsPlayerPlaying() ||
        mctx->GetPIPState() != kPBPLeft || exitPlayerTimerId)
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            "PBPRestartMainPlayer -- end !ok !valid");
        return;
    }

    mctx->LockDeletePlayer(__FILE__, __LINE__);
    long long mctx_frame = (mctx->player) ? mctx->player->GetFramesPlayed() : 0;
    mctx->UnlockDeletePlayer(__FILE__, __LINE__);

    mctx->PIPTeardown();
    mctx->SetPIPState(kPIPOff);
    mctx->buffer->Seek(0, SEEK_SET);

    if (mctx->CreatePlayer(this, GetMythMainWindow(), mctx->GetState(), false))
    {
        ScheduleStateChange(mctx);
        mctx->LockDeletePlayer(__FILE__, __LINE__);
        if (mctx->player)
            mctx->player->JumpToFrame(mctx_frame);
        mctx->UnlockDeletePlayer(__FILE__, __LINE__);
        SetSpeedChangeTimer(25, __LINE__);
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "PBPRestartMainPlayer -- end ok");
        return;
    }

    ForceNextStateNone(mctx);
    LOG(VB_PLAYBACK, LOG_ERR, LOC +
            "PBPRestartMainPlayer -- end !ok Player did not restart");
}

/**
* \brief Recreate Main and PIP windows. Could be either PIP or PBP views.
*/
void TV::RestartAllPlayers(PlayerContext *lctx,
                        const vector<long long> &pos,
                        MuteState mctx_mute)
{
    QString loc = LOC + QString("RestartAllPlayers(): ");

    PlayerContext *mctx = GetPlayer(lctx, 0);

    if (!mctx)
        return;

    mctx->buffer->Seek(0, SEEK_SET);

    if (StateIsLiveTV(mctx->GetState()))
        mctx->buffer->Unpause();

    bool ok = StartPlayer(mctx, mctx, mctx->GetState());

    if (ok)
    {
        mctx->LockDeletePlayer(__FILE__, __LINE__);
        if (mctx->player)
            mctx->player->JumpToFrame(pos[0]);
        mctx->UnlockDeletePlayer(__FILE__, __LINE__);
    }
    else
    {
        LOG(VB_GENERAL, LOG_ERR, loc +
                "Failed to restart new main context (was pip context)");
        ForceNextStateNone(mctx);
        return;
    }

    for (uint i = 1; i < player.size(); i++)
    {
        PlayerContext *pipctx = GetPlayer(lctx, i);

        pipctx->buffer->Seek(0, SEEK_SET);

        if (StateIsLiveTV(pipctx->GetState()))
            pipctx->buffer->Unpause();

        ok = StartPlayer(mctx, pipctx, pipctx->GetState());

        if (ok)
        {
            pipctx->LockDeletePlayer(__FILE__, __LINE__);
            if (pipctx->player)
            {
                pipctx->player->SetMuted(true);
                pipctx->player->JumpToFrame(pos[i]);
            }
            pipctx->UnlockDeletePlayer(__FILE__, __LINE__);
        }
        else
        { // TODO print OSD informing user of Swap failure ?
            LOG(VB_GENERAL, LOG_ERR, loc +
                "Failed to restart new pip context (was main context)");
            ForceNextStateNone(pipctx);
        }
    }

    // If old main player had a kMuteAll | kMuteOff setting,
    // apply old main player's mute setting to new main player.
    mctx->LockDeletePlayer(__FILE__, __LINE__);
    if (mctx->player && ((kMuteAll == mctx_mute) || (kMuteOff == mctx_mute)))
        mctx->player->SetMuteState(mctx_mute);
    mctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::PxPSwap(PlayerContext *mctx, PlayerContext *pipctx)
{
    if (!mctx || !pipctx)
        return;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "PxPSwap -- begin");
    if (mctx == pipctx)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "PxPSwap -- need two contexts");
        return;
    }

    lockTimerOn = false;

    multi_lock(&mctx->deletePlayerLock, &pipctx->deletePlayerLock, NULL);
    if (!mctx->player   || !mctx->player->IsPlaying() ||
        !pipctx->player || !pipctx->player->IsPlaying())
    {
        mctx->deletePlayerLock.unlock();
        pipctx->deletePlayerLock.unlock();
        LOG(VB_GENERAL, LOG_ERR, LOC + "PxPSwap -- a player is not playing");
        return;
    }

    MuteState mctx_mute = mctx->player->GetMuteState();
    mctx->deletePlayerLock.unlock();
    pipctx->deletePlayerLock.unlock();

    int ctx_index = find_player_index(pipctx);

    vector<long long> pos = TeardownAllPlayers(mctx);

    swap(player[0],           player[ctx_index]);
    swap(pos[0],              pos[ctx_index]);
    swap(player[0]->pipState, player[ctx_index]->pipState);
    playerActive = (ctx_index == playerActive) ?
        0 : ((ctx_index == 0) ? ctx_index : playerActive);

    RestartAllPlayers(mctx, pos, mctx_mute);

    SetActive(mctx, playerActive, false);

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "PxPSwap -- end");
}

void TV::RestartMainPlayer(PlayerContext *mctx)
{
    if (!mctx)
        return;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "Restart main player -- begin");
    lockTimerOn = false;

    mctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!mctx->player)
    {
        mctx->deletePlayerLock.unlock();
        return;
    }

    MuteState mctx_mute = mctx->player->GetMuteState();

    // HACK - FIXME
    // workaround muted audio when Player is re-created
    mctx_mute = kMuteOff;
    // FIXME - end
    mctx->deletePlayerLock.unlock();

    vector<long long> pos = TeardownAllPlayers(mctx);
    RestartAllPlayers(mctx, pos, mctx_mute);
    SetActive(mctx, playerActive, false);

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "Restart main player -- end");
}

void TV::DoPlay(PlayerContext *ctx)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }

    float time = 0.0;

    if (ctx->ff_rew_state || (ctx->ff_rew_speed != 0) ||
        ctx->player->IsPaused())
    {
        if (ctx->ff_rew_state)
            time = StopFFRew(ctx);
        else if (ctx->player->IsPaused())
            SendMythSystemPlayEvent("PLAY_UNPAUSED", ctx->playingInfo);

        ctx->player->Play(ctx->ts_normal, true);
        ctx->ff_rew_speed = 0;
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    DoPlayerSeek(ctx, time);
    UpdateOSDSeekMessage(ctx, ctx->GetPlayMessage(), kOSDTimeout_Med);

    GetMythUI()->DisableScreensaver();

    SetSpeedChangeTimer(0, __LINE__);
}

float TV::DoTogglePauseStart(PlayerContext *ctx)
{
    if (!ctx)
        return 0.0f;

    if (ctx->buffer && ctx->buffer->IsInDiscMenuOrStillFrame())
        return 0.0f;

    ctx->ff_rew_speed = 0;
    float time = 0.0f;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return 0.0f;
    }
    if (ctx->player->IsPaused())
    {
        ctx->player->Play(ctx->ts_normal, true);
    }
    else
    {
        if (ctx->ff_rew_state)
            time = StopFFRew(ctx);
        ctx->player->Pause();
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return time;
}

void TV::DoTogglePauseFinish(PlayerContext *ctx, float time, bool showOSD)
{
    if (!ctx || !ctx->HasPlayer())
        return;

    if (ctx->buffer && ctx->buffer->IsInDiscMenuOrStillFrame())
        return;

    if (ContextIsPaused(ctx, __FILE__, __LINE__))
    {
        if (ctx->buffer)
            ctx->buffer->WaitForPause();

        DoPlayerSeek(ctx, time);

        if (showOSD && ctx == player[0])
            UpdateOSDSeekMessage(ctx, tr("Paused"), kOSDTimeout_None);
        else if (showOSD)
            UpdateOSDSeekMessage(ctx, tr("Aux Paused"), kOSDTimeout_None);

        RestoreScreenSaver(ctx);
    }
    else
    {
        DoPlayerSeek(ctx, time);
        if (showOSD)
            UpdateOSDSeekMessage(ctx, ctx->GetPlayMessage(), kOSDTimeout_Med);
        GetMythUI()->DisableScreensaver();
    }

    SetSpeedChangeTimer(0, __LINE__);
}

void TV::DoTogglePause(PlayerContext *ctx, bool showOSD)
{
    bool ignore = false;
    bool paused = false;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        ignore = ctx->player->GetEditMode();
        paused = ctx->player->IsPaused();
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (paused)
        SendMythSystemPlayEvent("PLAY_UNPAUSED", ctx->playingInfo);
    else
        SendMythSystemPlayEvent("PLAY_PAUSED", ctx->playingInfo);

    if (!ignore)
        DoTogglePauseFinish(ctx, DoTogglePauseStart(ctx), showOSD);
}

bool TV::DoPlayerSeek(PlayerContext *ctx, float time)
{
    if (time > -0.001f && time < +0.001f)
        return false;

    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("DoPlayerSeek (%1 seconds)").arg(time));

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return false;
    }

    if (!ctx->buffer->IsSeekingAllowed())
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return false;
    }

    if (ctx == GetPlayer(ctx, 0))
        PauseAudioUntilBuffered(ctx);

    bool res = false;

    if (time > 0.0f)
        res = ctx->player->FastForward(time);
    else if (time < 0.0)
        res = ctx->player->Rewind(-time);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    return res;
}

static struct ActionDefStruct<TV> tshActions[] = {
    { ACTION_SEEKFFWD, &TV::doSeekFfwd },
    { "FFWDSTICKY",    &TV::doSeekFfwdSticky },
    { ACTION_RIGHT,    &TV::doSeekRight },
    { ACTION_SEEKRWND, &TV::doSeekRwnd },
    { "RWNDSTICKY",    &TV::doSeekRwndSticky },
    { ACTION_LEFT,     &TV::doSeekLeft }
};
static int tshActionCount = NELEMS(tshActions);

bool TV::doSeekFfwd(const QString &action)
{
    m_actionSeekFlags = (SeekFlags)(ARBSEEK_FORWARD | kForward | kSlippery |
                                    kRelative);
    return true;
}

bool TV::doSeekFfwdSticky(const QString &action)
{
    m_actionSeekFlags = (SeekFlags)(ARBSEEK_END     | kForward | kSticky   |
                                    kAbsolute);
    return true;
}

bool TV::doSeekRight(const QString &action)
{
    m_actionSeekFlags = (SeekFlags)(ARBSEEK_FORWARD | kForward | kSticky   |
                                    kRelative);
    return true;
}

bool TV::doSeekRwnd(const QString &action)
{
    m_actionSeekFlags = (SeekFlags)(ARBSEEK_REWIND  | kRewind  | kSlippery |
                                    kRelative);
    return true;
}

bool TV::doSeekRwndSticky(const QString &action)
{
    m_actionSeekFlags = (SeekFlags)(ARBSEEK_SET     | kRewind  | kSticky   |
                                    kAbsolute);
    return true;
}

bool TV::doSeekLeft(const QString &action)
{
    m_actionSeekFlags = (SeekFlags)(ARBSEEK_REWIND  | kRewind  | kSticky   |
                                    kRelative);
    return true;
}

bool TV::SeekHandleAction(PlayerContext *actx, const QStringList &actions)
{
    bool isDVD = m_actionContext->buffer && m_actionContext->buffer->IsDVD();
    m_actionSeekFlags = kNoSeekFlags;
    m_actionContext = actx;

    if (!m_seekActions)
        m_seekActions = new MythActions<TV>(this, tshActions, tshActionCount);
    bool handled = m_seekActions->handleActions(actions);

    if (!handled)
        return false;

    SeekFlags flags = m_actionSeekFlags;

    int direction = (flags & kRewind) ? -1 : 1;
    if (HasQueuedInput())
    {
        DoArbSeek(actx, static_cast<ArbSeekWhence>(flags & kWhenceMask));
    }
    else if (ContextIsPaused(actx, __FILE__, __LINE__))
    {
        if (!isDVD)
        {
            float rate = 30.0f;
            actx->LockDeletePlayer(__FILE__, __LINE__);
            if (actx->player)
                rate = actx->player->GetFrameRate();
            actx->UnlockDeletePlayer(__FILE__, __LINE__);
            float time = (flags & kAbsolute) ?  direction :
                             direction * (1.001 / rate);
            QString message = (flags & kRewind) ? QString(tr("Rewind")) :
                                                 QString(tr("Forward"));
            DoSeek(actx, time, message);
        }
    }
    else if (flags & kSticky)
    {
        ChangeFFRew(actx, direction);
    }
    else if (flags & kRewind)
    {
            if (smartForward)
                doSmartForward = true;
            DoSeek(actx, -actx->rewtime, tr("Skip Back"));
    }
    else
    {
        if (smartForward & doSmartForward)
            DoSeek(actx, actx->rewtime, tr("Skip Ahead"));
        else
            DoSeek(actx, actx->fftime, tr("Skip Ahead"));
    }
    return true;
}

void TV::DoSeek(PlayerContext *ctx, float time, const QString &mesg)
{
    bool limitkeys = false;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player && ctx->player->GetLimitKeyRepeat())
        limitkeys = true;
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (!limitkeys || (keyRepeatTimer.elapsed() > (int)kKeyRepeatTimeout))
    {
        keyRepeatTimer.start();
        NormalSpeed(ctx);
        time += StopFFRew(ctx);
        DoPlayerSeek(ctx, time);
        UpdateOSDSeekMessage(ctx, mesg, kOSDTimeout_Med);
    }
}

void TV::DoSeekAbsolute(PlayerContext *ctx, long long seconds)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }
    seconds -= (ctx->player->GetFramesPlayed() - 1) / ctx->player->GetFrameRate();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    DoSeek(ctx, seconds, tr("Jump To"));
}

void TV::DoArbSeek(PlayerContext *ctx, ArbSeekWhence whence)
{
    bool ok = false;
    int seek = GetQueuedInputAsInt(&ok);
    ClearInputQueues(ctx, true);
    if (!ok)
        return;

    float time = ((seek / 100) * 3600) + ((seek % 100) * 60);

    if (whence == ARBSEEK_FORWARD)
        DoSeek(ctx, time, tr("Jump Ahead"));
    else if (whence == ARBSEEK_REWIND)
        DoSeek(ctx, -time, tr("Jump Back"));
    else if (whence == ARBSEEK_END)
    {
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        if (!ctx->player)
        {
            ctx->UnlockDeletePlayer(__FILE__, __LINE__);
            return;
        }
        time = (ctx->player->CalcMaxFFTime(LONG_MAX, false) /
                ctx->player->GetFrameRate()) - time;
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        DoSeek(ctx, time, tr("Jump To"));
    }
    else
        DoSeekAbsolute(ctx, time);
}

void TV::NormalSpeed(PlayerContext *ctx)
{
    if (!ctx->ff_rew_speed)
        return;

    ctx->ff_rew_speed = 0;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->Play(ctx->ts_normal, true);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    SetSpeedChangeTimer(0, __LINE__);
}

void TV::ChangeSpeed(PlayerContext *ctx, int direction)
{
    int old_speed = ctx->ff_rew_speed;

    if (ContextIsPaused(ctx, __FILE__, __LINE__))
        ctx->ff_rew_speed = -4;

    ctx->ff_rew_speed += direction;

    float time = StopFFRew(ctx);
    float speed;
    QString mesg;

    switch (ctx->ff_rew_speed)
    {
        case  4: speed = 16.0;     mesg = QString(tr("Speed 16X"));   break;
        case  3: speed = 8.0;      mesg = QString(tr("Speed 8X"));    break;
        case  2: speed = 3.0;      mesg = QString(tr("Speed 3X"));    break;
        case  1: speed = 2.0;      mesg = QString(tr("Speed 2X"));    break;
        case  0: speed = 1.0;      mesg = ctx->GetPlayMessage();      break;
        case -1: speed = 1.0 / 3;  mesg = QString(tr("Speed 1/3X"));  break;
        case -2: speed = 1.0 / 8;  mesg = QString(tr("Speed 1/8X"));  break;
        case -3: speed = 1.0 / 16; mesg = QString(tr("Speed 1/16X")); break;
        case -4:
            DoTogglePause(ctx, true);
            return;
        default:
            ctx->ff_rew_speed = old_speed;
            return;
    }

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player && !ctx->player->Play(
            (!ctx->ff_rew_speed) ? ctx->ts_normal: speed, !ctx->ff_rew_speed))
    {
        ctx->ff_rew_speed = old_speed;
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    DoPlayerSeek(ctx, time);
    UpdateOSDSeekMessage(ctx, mesg, kOSDTimeout_Med);

    SetSpeedChangeTimer(0, __LINE__);
}

float TV::StopFFRew(PlayerContext *ctx)
{
    float time = 0.0;

    if (!ctx->ff_rew_state)
        return time;

    if (ctx->ff_rew_state > 0)
        time = -ff_rew_speeds[ctx->ff_rew_index] * ff_rew_repos;
    else
        time = ff_rew_speeds[ctx->ff_rew_index] * ff_rew_repos;

    ctx->ff_rew_state = 0;
    ctx->ff_rew_index = kInitFFRWSpeed;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->Play(ctx->ts_normal, true);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    SetSpeedChangeTimer(0, __LINE__);

    return time;
}

void TV::ChangeFFRew(PlayerContext *ctx, int direction)
{
    if (ctx->ff_rew_state == direction)
    {
        while (++ctx->ff_rew_index < (int)ff_rew_speeds.size())
            if (ff_rew_speeds[ctx->ff_rew_index])
                break;
        if (ctx->ff_rew_index >= (int)ff_rew_speeds.size())
            ctx->ff_rew_index = kInitFFRWSpeed;
        SetFFRew(ctx, ctx->ff_rew_index);
    }
    else if (!ff_rew_reverse && ctx->ff_rew_state == -direction)
    {
        while (--ctx->ff_rew_index >= kInitFFRWSpeed)
            if (ff_rew_speeds[ctx->ff_rew_index])
                break;
        if (ctx->ff_rew_index >= kInitFFRWSpeed)
            SetFFRew(ctx, ctx->ff_rew_index);
        else
        {
            float time = StopFFRew(ctx);
            DoPlayerSeek(ctx, time);
            UpdateOSDSeekMessage(ctx, ctx->GetPlayMessage(), kOSDTimeout_Med);
        }
    }
    else
    {
        NormalSpeed(ctx);
        ctx->ff_rew_state = direction;
        SetFFRew(ctx, kInitFFRWSpeed);
    }
}

void TV::SetFFRew(PlayerContext *ctx, int index)
{
    if (!ctx->ff_rew_state)
    {
        return;
    }

    if (!ff_rew_speeds[index])
    {
        return;
    }

    int speed;
    QString mesg;
    if (ctx->ff_rew_state > 0)
    {
        speed = ff_rew_speeds[index];
        // Don't allow ffwd if seeking is needed but not available
        if (!ctx->buffer->IsSeekingAllowed() && speed > 3)
            return;

        ctx->ff_rew_index = index;
        mesg = tr("Forward %1X").arg(ff_rew_speeds[ctx->ff_rew_index]);
        ctx->ff_rew_speed = speed;
    }
    else
    {
        // Don't rewind if we cannot seek
        if (!ctx->buffer->IsSeekingAllowed())
            return;

        ctx->ff_rew_index = index;
        mesg = tr("Rewind %1X").arg(ff_rew_speeds[ctx->ff_rew_index]);
        speed = -ff_rew_speeds[ctx->ff_rew_index];
        ctx->ff_rew_speed = speed;
    }

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->Play((float)speed, (speed == 1) && (ctx->ff_rew_state > 0));
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    UpdateOSDSeekMessage(ctx, mesg, kOSDTimeout_None);

    SetSpeedChangeTimer(0, __LINE__);
}

void TV::DoQueueTranscode(PlayerContext *ctx, QString profile)
{
    ctx->LockPlayingInfo(__FILE__, __LINE__);

    if (ctx->GetState() == kState_WatchingPreRecorded)
    {
        bool stop = false;
        if (queuedTranscode)
            stop = true;
        else if (JobQueue::IsJobQueuedOrRunning(
                     JOB_TRANSCODE,
                     ctx->playingInfo->GetChanID(),
                     ctx->playingInfo->GetRecordingStartTime()))
        {
            stop = true;
        }

        if (stop)
        {
            JobQueue::ChangeJobCmds(
                JOB_TRANSCODE,
                ctx->playingInfo->GetChanID(),
                ctx->playingInfo->GetRecordingStartTime(), JOB_STOP);
            queuedTranscode = false;
            SetOSDMessage(ctx, tr("Stopping Transcode"));
        }
        else
        {
            const RecordingInfo recinfo(*ctx->playingInfo);
            recinfo.ApplyTranscoderProfileChange(profile);
            QString jobHost = "";

            if (db_run_jobs_on_remote)
                jobHost = ctx->playingInfo->GetHostname();

            QString msg = tr("Try Again");
            if (JobQueue::QueueJob(JOB_TRANSCODE,
                       ctx->playingInfo->GetChanID(),
                       ctx->playingInfo->GetRecordingStartTime(),
                       jobHost, "", "", JOB_USE_CUTLIST))
            {
                queuedTranscode = true;
                msg = tr("Transcoding");
            }
            SetOSDMessage(ctx, msg);
        }
    }
    ctx->UnlockPlayingInfo(__FILE__, __LINE__);
}

int TV::GetNumChapters(const PlayerContext *ctx) const
{
    int num_chapters = 0;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        num_chapters = ctx->player->GetNumChapters();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return num_chapters;
}

void TV::GetChapterTimes(const PlayerContext *ctx, QList<long long> &times) const
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->GetChapterTimes(times);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

int TV::GetCurrentChapter(const PlayerContext *ctx) const
{
    int chapter = 0;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        chapter = ctx->player->GetCurrentChapter();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return chapter;
}

void TV::DoJumpChapter(PlayerContext *ctx, int chapter)
{
    NormalSpeed(ctx);
    StopFFRew(ctx);

    PauseAudioUntilBuffered(ctx);

    UpdateOSDSeekMessage(ctx, tr("Jump Chapter"), kOSDTimeout_Med);
    SetUpdateOSDPosition(true);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->JumpChapter(chapter);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

int TV::GetNumTitles(const PlayerContext *ctx) const
{
    int num_titles = 0;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        num_titles = ctx->player->GetNumTitles();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return num_titles;
}

int TV::GetCurrentTitle(const PlayerContext *ctx) const
{
    int currentTitle = 0;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        currentTitle = ctx->player->GetCurrentTitle();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return currentTitle;
}

int TV::GetNumAngles(const PlayerContext *ctx) const
{
    int num_angles = 0;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        num_angles = ctx->player->GetNumAngles();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return num_angles;
}

int TV::GetCurrentAngle(const PlayerContext *ctx) const
{
    int currentAngle = 0;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        currentAngle = ctx->player->GetCurrentAngle();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return currentAngle;
}

QString TV::GetAngleName(const PlayerContext *ctx, int angle) const
{
    QString name;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        name = ctx->player->GetAngleName(angle);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return name;
}

int TV::GetTitleDuration(const PlayerContext *ctx, int title) const
{
    int seconds = 0;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        seconds = ctx->player->GetTitleDuration(title);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return seconds;
}


QString TV::GetTitleName(const PlayerContext *ctx, int title) const
{
    QString name;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        name = ctx->player->GetTitleName(title);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    return name;
}

void TV::DoSwitchTitle(PlayerContext *ctx, int title)
{
    NormalSpeed(ctx);
    StopFFRew(ctx);

    PauseAudioUntilBuffered(ctx);

    UpdateOSDSeekMessage(ctx, tr("Switch Title"), kOSDTimeout_Med);
    SetUpdateOSDPosition(true);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->SwitchTitle(title);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::DoSwitchAngle(PlayerContext *ctx, int angle)
{
    NormalSpeed(ctx);
    StopFFRew(ctx);

    PauseAudioUntilBuffered(ctx);

    UpdateOSDSeekMessage(ctx, tr("Switch Angle"), kOSDTimeout_Med);
    SetUpdateOSDPosition(true);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->SwitchAngle(angle);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::DoSkipCommercials(PlayerContext *ctx, int direction)
{
    NormalSpeed(ctx);
    StopFFRew(ctx);

    if (StateIsLiveTV(GetState(ctx)))
        return;

    PauseAudioUntilBuffered(ctx);

    osdInfo info;
    ctx->CalcPlayerSliderPosition(info);
    info.text["title"] = tr("Skip");
    info.text["description"] = tr("Searching");
    UpdateOSDStatus(ctx, info, kOSDFunctionalType_Default, kOSDTimeout_Med);
    SetUpdateOSDPosition(true);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->SkipCommercials(direction);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::SwitchSource(PlayerContext *ctx, uint source_direction)
{
    QMap<uint,InputInfo> sources;
    vector<uint> cardids = RemoteRequestFreeRecorderList();
    uint         cardid  = ctx->GetCardID();
    cardids.push_back(cardid);
    stable_sort(cardids.begin(), cardids.end());

    vector<uint> excluded_cardids;
    excluded_cardids.push_back(cardid);

    InfoMap info;
    ctx->recorder->GetChannelInfo(info);
    uint sourceid = info["sourceid"].toUInt();

    vector<uint>::const_iterator it = cardids.begin();
    for (; it != cardids.end(); ++it)
    {
        vector<InputInfo> inputs = RemoteRequestFreeInputList(
            *it, excluded_cardids);

        if (inputs.empty())
            continue;

        for (uint i = 0; i < inputs.size(); i++)
        {
            // prefer the current card's input in sources list
            if ((sources.find(inputs[i].sourceid) == sources.end()) ||
                ((cardid == inputs[i].cardid) &&
                 (cardid != sources[inputs[i].sourceid].cardid)))
            {
                sources[inputs[i].sourceid] = inputs[i];
            }
        }
    }

    // Source switching
    QMap<uint,InputInfo>::const_iterator beg = sources.find(sourceid);
    QMap<uint,InputInfo>::const_iterator sit = beg;

    if (sit == sources.end())
    {
        return;
    }

    if (kNextSource == source_direction)
    {
        ++sit;
        if (sit == sources.end())
            sit = sources.begin();
    }

    if (kPreviousSource == source_direction)
    {
        if (sit != sources.begin())
            --sit;
        else
        {
            QMap<uint,InputInfo>::const_iterator tmp = sources.begin();
            while (tmp != sources.end())
            {
                sit = tmp;
                ++tmp;
            }
        }
    }

    if (sit == beg)
    {
        return;
    }

    switchToInputId = (*sit).inputid;

    QMutexLocker locker(&timerIdLock);
    if (!switchToInputTimerId)
        switchToInputTimerId = StartTimer(1, __LINE__);
}

void TV::SwitchInputs(PlayerContext *ctx, uint inputid)
{
    if (!ctx->recorder)
    {
        return;
    }

    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("SwitchInputs(%1)").arg(inputid));

    if ((uint)ctx->GetCardID() == CardUtil::GetCardID(inputid))
    {
        ToggleInputs(ctx, inputid);
    }
    else
    {
        SwitchCards(ctx, 0, QString::null, inputid);
    }
}

void TV::SwitchCards(PlayerContext *ctx,
                     uint chanid, QString channum, uint inputid)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("SwitchCards(%1,'%2',%3)")
            .arg(chanid).arg(channum).arg(inputid));

    RemoteEncoder *testrec = NULL;

    if (!StateIsLiveTV(GetState(ctx)))
    {
        return;
    }

    uint input_cardid = 0;
    QStringList reclist;
    if (inputid)
    {
        // If we are switching to a specific input..
        input_cardid = CardUtil::GetCardID(inputid);
        if (input_cardid)
            reclist.push_back(QString::number(input_cardid));
    }
    else if (chanid || !channum.isEmpty())
    {
        // If we are switching to a channel not on the current recorder
        // we need to find the next free recorder with that channel.
        reclist = ChannelUtil::GetValidRecorderList(chanid, channum);
    }

    if (!reclist.empty())
        testrec = RemoteRequestFreeRecorderFromList(reclist);

    if (testrec && testrec->IsValidRecorder())
    {
        uint cardid = testrec->GetRecorderNumber();
        int cardinputid = (int) inputid;
        QString inputname;

        // We are switching to a specific input..
        if (inputid)
            inputname = CardUtil::GetInputName(inputid);

        // We are switching to a specific channel...
        if (inputname.isEmpty() && (chanid || !channum.isEmpty()))
        {
            if (chanid && channum.isEmpty())
                channum = ChannelUtil::GetChanNum(chanid);

            cardinputid = CardUtil::GetCardInputID(
                cardid, channum, inputname);
        }

        if (cardid && cardinputid>0 && !inputname.isEmpty())
        {
            if (!channum.isEmpty())
                CardUtil::SetStartChannel(cardinputid, channum);
        }
        else
        {
            LOG(VB_GENERAL, LOG_WARNING, LOC +
                QString("SwitchCards(%1,'%2',%3)")
                    .arg(chanid).arg(channum).arg(inputid) +
                "\n\t\t\tWe should have been able to set a start "
                "channel or input but failed to do so.");
        }
    }

    // If we are just switching recorders find first available recorder.
    if (!testrec)
        testrec = RemoteRequestNextFreeRecorder(ctx->GetCardID());

    if (testrec && testrec->IsValidRecorder())
    {
        // Switching cards so clear the pseudoLiveTVState.
        ctx->SetPseudoLiveTV(NULL, kPseudoNormalLiveTV);

        PlayerContext *mctx = GetPlayer(ctx, 0);
        if (mctx != ctx)
            PIPRemovePlayer(mctx, ctx);

        bool muted = false;
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        if (ctx->player && ctx->player->IsMuted())
            muted = true;
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);

        // pause the decoder first, so we're not reading too close to the end.
        ctx->buffer->IgnoreLiveEOF(true);
        ctx->buffer->StopReads();
        ctx->player->PauseDecoder();

        // shutdown stuff
        ctx->buffer->Pause();
        ctx->buffer->WaitForPause();
        ctx->StopPlaying();
        ctx->recorder->StopLiveTV();
        ctx->SetPlayer(NULL);

        // now restart stuff
        ctx->lastSignalUIInfo.clear();
        lockTimerOn = false;

        ctx->SetRecorder(testrec);
        ctx->recorder->Setup();
        // We need to set channum for SpawnLiveTV..
        if (channum.isEmpty() && chanid)
            channum = ChannelUtil::GetChanNum(chanid);
        if (channum.isEmpty() && inputid)
            channum = CardUtil::GetStartingChannel(inputid);
        ctx->recorder->SpawnLiveTV(ctx->tvchain->GetID(), false, channum);

        if (!ctx->ReloadTVChain())
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "LiveTV not successfully restarted");
            RestoreScreenSaver(ctx);
            ctx->SetRecorder(NULL);
            SetErrored(ctx);
            SetExitPlayer(true, false);
        }
        else
        {
            ctx->LockPlayingInfo(__FILE__, __LINE__);
            QString playbackURL = ctx->playingInfo->GetPlaybackURL(true);
            bool opennow = (ctx->tvchain->GetCardType(-1) != "DUMMY");
            ctx->SetRingBuffer(
                RingBuffer::Create(
                    playbackURL, false, true,
                    opennow ? RingBuffer::kLiveTVOpenTimeout : -1));

            ctx->tvchain->SetProgram(*ctx->playingInfo);
            ctx->buffer->SetLiveMode(ctx->tvchain);
            ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        }

        bool ok = false;
        if (ctx->playingInfo && StartRecorder(ctx,-1))
        {
            PlayerContext *mctx = GetPlayer(ctx, 0);
            QRect dummy = QRect();
            if (ctx->CreatePlayer(
                    this, GetMythMainWindow(), ctx->GetState(),
                    false, dummy, muted))
            {
                ScheduleStateChange(ctx);
                ok = true;
                ctx->PushPreviousChannel();
                for (uint i = 1; i < player.size(); i++)
                    PIPAddPlayer(mctx, GetPlayer(ctx, i));

                SetSpeedChangeTimer(25, __LINE__);
            }
            else
                StopStuff(mctx, ctx, true, true, true);
        }

        if (!ok)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "LiveTV not successfully started");
            RestoreScreenSaver(ctx);
            ctx->SetRecorder(NULL);
            SetErrored(ctx);
            SetExitPlayer(true, false);
        }
        else
        {
            lockTimer.start();
            lockTimerOn = true;
        }
    }
    else
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "No recorder to switch to...");
        delete testrec;
    }

    UpdateOSDInput(ctx);
    UnpauseLiveTV(ctx);

    ITVRestart(ctx, true);
}

void TV::ToggleInputs(PlayerContext *ctx, uint inputid)
{
    if (!ctx->recorder)
    {
        return;
    }

    // If MythPlayer is paused, unpause it
    if (ContextIsPaused(ctx, __FILE__, __LINE__))
    {
        HideOSDWindow(ctx, "osd_status");
        GetMythUI()->DisableScreensaver();
    }

    const QString curinputname = ctx->recorder->GetInput();
    QString inputname = curinputname;

    uint cardid = ctx->GetCardID();
    vector<uint> excluded_cardids;
    excluded_cardids.push_back(cardid);
    vector<InputInfo> inputs = RemoteRequestFreeInputList(
        cardid, excluded_cardids);

    vector<InputInfo>::const_iterator it = inputs.end();

    if (inputid)
    {
        it = find(inputs.begin(), inputs.end(), inputid);
    }
    else
    {
        it = find(inputs.begin(), inputs.end(), inputname);
        if (it != inputs.end())
            ++it;
    }

    if (it == inputs.end())
        it = inputs.begin();

    if (it != inputs.end())
        inputname = (*it).name;

    if (curinputname != inputname)
    {
        // Pause the backend recorder, send command, and then unpause..
        PauseLiveTV(ctx);
        lockTimerOn = false;
        inputname = ctx->recorder->SetInput(inputname);
        UnpauseLiveTV(ctx);
    }

    UpdateOSDInput(ctx, inputname);
}

void TV::ToggleChannelFavorite(PlayerContext *ctx)
{
    // TOGGLEFAV was broken in [20523], this just prints something
    // out so as not to cause further confusion. See #8948.
    LOG(VB_GENERAL, LOG_ERR,
        "TV::ToggleChannelFavorite() -- currently disabled");
}

void TV::ToggleChannelFavorite(PlayerContext *ctx, QString changroup_name)
{
    if (ctx->recorder)
        ctx->recorder->ToggleChannelFavorite(changroup_name);
}

QString TV::GetQueuedInput(void) const
{
    QMutexLocker locker(&timerIdLock);
    QString ret = queuedInput;
    ret.detach();
    return ret;
}

int TV::GetQueuedInputAsInt(bool *ok, int base) const
{
    QMutexLocker locker(&timerIdLock);
    return queuedInput.toInt(ok, base);
}

QString TV::GetQueuedChanNum(void) const
{
    QMutexLocker locker(&timerIdLock);

    if (queuedChanNum.isEmpty())
        return "";

    // strip initial zeros and other undesirable characters
    int i = 0;
    for (; i < queuedChanNum.length(); i++)
    {
        if ((queuedChanNum[i] > '0') && (queuedChanNum[i] <= '9'))
            break;
    }
    queuedChanNum = queuedChanNum.right(queuedChanNum.length() - i);

    // strip whitespace at end of string
    queuedChanNum = queuedChanNum.trimmed();

    QString ret = queuedChanNum;
    ret.detach();
    return ret;
}

/** \fn TV::ClearInputQueues(const PlayerContext*,bool)
 *  \brief Clear channel key buffer of input keys.
 *  \param hideosd if true, hides "channel_number" OSDSet.
 */
void TV::ClearInputQueues(const PlayerContext *ctx, bool hideosd)
{
    if (hideosd)
        HideOSDWindow(ctx, "osd_input");

    QMutexLocker locker(&timerIdLock);
    queuedInput   = "";
    queuedChanNum = "";
    queuedChanID  = 0;
    if (queueInputTimerId)
    {
        KillTimer(queueInputTimerId);
        queueInputTimerId = 0;
    }
}

void TV::AddKeyToInputQueue(PlayerContext *ctx, char key)
{
    if (key)
    {
        QMutexLocker locker(&timerIdLock);
        queuedInput   = queuedInput.append(key).right(kInputKeysMax);
        queuedChanNum = queuedChanNum.append(key).right(kInputKeysMax);
        if (!queueInputTimerId)
            queueInputTimerId = StartTimer(10, __LINE__);
    }

    bool commitSmart = false;
    QString inputStr = GetQueuedInput();

    // Always use immediate channel change when channel numbers are entered
    // in browse mode because in browse mode space/enter exit browse
    // mode and change to the currently browsed channel.
    if (StateIsLiveTV(GetState(ctx)) && !ccInputMode && !asInputMode &&
        browsehelper->IsBrowsing())
    {
        commitSmart = ProcessSmartChannel(ctx, inputStr);
    }

    // Handle OSD...
    inputStr = inputStr.isEmpty() ? "?" : inputStr;
    if (ccInputMode)
    {
        QString entryStr = (vbimode==VBIMode::PAL_TT) ? tr("TXT:") : tr("CC:");
        inputStr = entryStr + " " + inputStr;
    }
    else if (asInputMode)
        inputStr = tr("Seek:", "seek to location") + " " + inputStr;
    SetOSDText(ctx, "osd_input", "osd_number_entry", inputStr,
               kOSDTimeout_Med);

    // Commit the channel if it is complete and smart changing is enabled.
    if (commitSmart)
        CommitQueuedInput(ctx);
}

static QString add_spacer(const QString &chan, const QString &spacer)
{
    if ((chan.length() >= 2) && !spacer.isEmpty())
        return chan.left(chan.length()-1) + spacer + chan.right(1);
    return chan;
}

bool TV::ProcessSmartChannel(const PlayerContext *ctx, QString &inputStr)
{
    QString chan = GetQueuedChanNum();

    if (chan.isEmpty())
        return false;

    // Check for and remove duplicate separator characters
    if ((chan.length() > 2) && (chan.right(1) == chan.right(2).left(1)))
    {
        bool ok;
        chan.right(1).toUInt(&ok);
        if (!ok)
        {
            chan = chan.left(chan.length()-1);

            QMutexLocker locker(&timerIdLock);
            queuedChanNum = chan;
            if (!queueInputTimerId)
                queueInputTimerId = StartTimer(10, __LINE__);
        }
    }

    // Look for channel in line-up
    QString needed_spacer;
    uint    pref_cardid;
    bool    is_not_complete = true;

    bool valid_prefix = false;
    if (ctx->recorder)
    {
        valid_prefix = ctx->recorder->CheckChannelPrefix(
            chan, pref_cardid, is_not_complete, needed_spacer);
    }

#if DEBUG_CHANNEL_PREFIX
    LOG(VB_GENERAL, LOG_DEBUG, QString("valid_pref(%1) cardid(%2) chan(%3) "
                                       "pref_cardid(%4) complete(%5) sp(%6)")
            .arg(valid_prefix).arg(0).arg(chan)
            .arg(pref_cardid).arg(is_not_complete).arg(needed_spacer));
#endif

    if (!valid_prefix)
    {
        // not a valid prefix.. reset...
        QMutexLocker locker(&timerIdLock);
        queuedChanNum = "";
    }
    else if (!needed_spacer.isEmpty())
    {
        // need a spacer..
        QMutexLocker locker(&timerIdLock);
        queuedChanNum = add_spacer(chan, needed_spacer);
    }

#if DEBUG_CHANNEL_PREFIX
    LOG(VB_GENERAL, LOG_DEBUG, QString(" ValidPref(%1) CardId(%2) Chan(%3) "
                                       " PrefCardId(%4) Complete(%5) Sp(%6)")
            .arg(valid_prefix).arg(0).arg(GetQueuedChanNum())
            .arg(pref_cardid).arg(is_not_complete).arg(needed_spacer));
#endif

    QMutexLocker locker(&timerIdLock);
    inputStr = queuedChanNum;
    inputStr.detach();
    if (!queueInputTimerId)
        queueInputTimerId = StartTimer(10, __LINE__);

    return !is_not_complete;
}

bool TV::CommitQueuedInput(PlayerContext *ctx)
{
    bool commited = false;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "CommitQueuedInput() " +
        QString("livetv(%1) qchannum(%2) qchanid(%3)")
            .arg(StateIsLiveTV(GetState(ctx)))
            .arg(GetQueuedChanNum())
            .arg(GetQueuedChanID()));

    if (ccInputMode)
    {
        commited = true;
        if (HasQueuedInput())
            HandleTrackAction(ctx, QStringList(ACTION_TOGGLESUBS));
    }
    else if (asInputMode)
    {
        commited = true;
        if (HasQueuedInput())
            DoArbSeek(ctx, ARBSEEK_FORWARD);
    }
    else if (StateIsLiveTV(GetState(ctx)))
    {
        QString channum = GetQueuedChanNum();
        QString chaninput = GetQueuedInput();
        if (browsehelper->IsBrowsing())
        {
            uint sourceid = 0;
            ctx->LockPlayingInfo(__FILE__, __LINE__);
            if (ctx->playingInfo)
                sourceid = ctx->playingInfo->GetSourceID();
            ctx->UnlockPlayingInfo(__FILE__, __LINE__);

            commited = true;
            if (channum.isEmpty())
                channum = browsehelper->GetBrowsedInfo().m_channum;
            uint chanid = browsehelper->GetChanId(
                channum, ctx->GetCardID(), sourceid);
            if (chanid)
                browsehelper->BrowseChannel(ctx, channum);

            HideOSDWindow(ctx, "osd_input");
        }
        else if (GetQueuedChanID() || !channum.isEmpty())
        {
            commited = true;
            ChangeChannel(ctx, GetQueuedChanID(), channum);
        }
    }

    ClearInputQueues(ctx, true);
    return commited;
}

void TV::ChangeChannel(PlayerContext *ctx, int direction)
{
    if (db_use_channel_groups || (direction == CHANNEL_DIRECTION_FAVORITE))
    {
        uint old_chanid = 0;
        if (channelGroupId > -1)
        {
            ctx->LockPlayingInfo(__FILE__, __LINE__);
            if (!ctx->playingInfo)
            {
                LOG(VB_GENERAL, LOG_ERR, LOC +
                    "ChangeChannel(): no active ctx playingInfo.");
                ctx->UnlockPlayingInfo(__FILE__, __LINE__);
                ReturnPlayerLock(ctx);
                return;
            }
            // Collect channel info
            old_chanid = ctx->playingInfo->GetChanID();
            ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        }

        if (old_chanid)
        {
            QMutexLocker locker(&channelGroupLock);
            if (channelGroupId > -1)
            {
                uint chanid = ChannelUtil::GetNextChannel(
                    channelGroupChannelList, old_chanid, 0, direction);
                if (chanid)
                    ChangeChannel(ctx, chanid, "");
                return;
            }
        }
    }

    if (direction == CHANNEL_DIRECTION_FAVORITE)
        direction = CHANNEL_DIRECTION_UP;

    QString oldinputname = ctx->recorder->GetInput();

    if (ContextIsPaused(ctx, __FILE__, __LINE__))
    {
        HideOSDWindow(ctx, "osd_status");
        GetMythUI()->DisableScreensaver();
    }

    // Save the current channel if this is the first time
    if (ctx->prevChan.empty())
        ctx->PushPreviousChannel();

    PauseAudioUntilBuffered(ctx);
    PauseLiveTV(ctx);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        ctx->player->ResetCaptions();
        ctx->player->ResetTeletext();
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    ctx->recorder->ChangeChannel(direction);
    ClearInputQueues(ctx, false);

    if (ctx->player)
        ctx->player->GetAudio()->Reset();

    UnpauseLiveTV(ctx);

    if (oldinputname != ctx->recorder->GetInput())
        UpdateOSDInput(ctx);
}

static uint get_chanid(const PlayerContext *ctx,
                       uint cardid, const QString &channum)
{
    uint chanid = 0, cur_sourceid = 0;
    // try to find channel on current input
    if (ctx && ctx->playingInfo && ctx->playingInfo->GetSourceID())
    {
        cur_sourceid = ctx->playingInfo->GetSourceID();
        chanid = max(ChannelUtil::GetChanID(cur_sourceid, channum), 0);
        if (chanid)
            return chanid;
    }
    // try to find channel on all inputs
    vector<uint> inputs = CardUtil::GetInputIDs(cardid);
    for (vector<uint>::const_iterator it = inputs.begin();
         it != inputs.end(); ++it)
    {
        uint sourceid = CardUtil::GetSourceID(*it);
        if (cur_sourceid == sourceid)
            continue; // already tested above
        if (sourceid)
        {
            chanid = max(ChannelUtil::GetChanID(sourceid, channum), 0);
            if (chanid)
                return chanid;
        }
    }
    return chanid;
}

void TV::ChangeChannel(PlayerContext *ctx, uint chanid, const QString &chan)
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("ChangeChannel(%1, '%2') ")
            .arg(chanid).arg(chan));

    if ((!chanid && chan.isEmpty()) || !ctx || !ctx->recorder)
        return;

    QString channum = chan;
    QStringList reclist;
    QSet<uint> tunable_on;

    QString oldinputname = ctx->recorder->GetInput();

    if (channum.isEmpty() && chanid)
    {
        channum = ChannelUtil::GetChanNum(chanid);
    }

    bool getit = false;
    if (ctx->recorder)
    {
        if ((ctx->pseudoLiveTVState == kPseudoRecording) || chanid)
        {
            getit = true;
        }
        else if (chanid)
        {
            tunable_on = IsTunableOn(ctx, chanid, true, false);
            getit = !tunable_on.contains(ctx->GetCardID());
        }
        else
        {
            QString needed_spacer;
            uint pref_cardid;
            uint cardid = ctx->GetCardID();
            bool dummy;

            ctx->recorder->CheckChannelPrefix(chan,  pref_cardid,
                                              dummy, needed_spacer);

            LOG(VB_CHANNEL, LOG_INFO, LOC +
                QString("CheckChannelPrefix(%1, pref_cardid %2, %3, '%4') "
                        "cardid %5")
                .arg(chan).arg(pref_cardid).arg(dummy).arg(needed_spacer)
                .arg(cardid));

            channum = add_spacer(chan, needed_spacer);
            if (pref_cardid != cardid)
            {
                getit = true;
            }
            else
            {
                if (!chanid)
                    chanid = get_chanid(ctx, cardid, chan);
                tunable_on = IsTunableOn(ctx, chanid, true, false);
                getit = !tunable_on.contains(cardid);
            }
        }

        if (getit)
        {
            QStringList tmp =
                ChannelUtil::GetValidRecorderList(chanid, channum);
            if (tunable_on.empty())
            {
                if (!chanid)
                    chanid = get_chanid(ctx, ctx->GetCardID(), chan);
                tunable_on = IsTunableOn(ctx, chanid, true, false);
            }
            QStringList::const_iterator it = tmp.begin();
            for (; it != tmp.end(); ++it)
            {
                if (!chanid || tunable_on.contains((*it).toUInt()))
                    reclist.push_back(*it);
            }
        }
    }

    if (reclist.size())
    {
        RemoteEncoder *testrec = NULL;
        testrec = RemoteRequestFreeRecorderFromList(reclist);
        if (!testrec || !testrec->IsValidRecorder())
        {
            ClearInputQueues(ctx, true);
            ShowNoRecorderDialog(ctx);
            if (testrec)
                delete testrec;
            return;
        }

        if (!ctx->prevChan.empty() && ctx->prevChan.back() == channum)
        {
            // need to remove it if the new channel is the same as the old.
            ctx->prevChan.pop_back();
        }

        uint new_cardid = testrec->GetRecorderNumber();
        uint sourceid = ChannelUtil::GetSourceIDForChannel(chanid);
        uint inputid = CardUtil::GetInputID(new_cardid, sourceid);

        // found the card on a different recorder.
        delete testrec;
        // Save the current channel if this is the first time
        if (ctx->prevChan.empty())
            ctx->PushPreviousChannel();
        SwitchCards(ctx, chanid, channum, inputid);
        return;
    }

    if (getit || !ctx->recorder || !ctx->recorder->CheckChannel(channum))
        return;

    if (ContextIsPaused(ctx, __FILE__, __LINE__))
    {
        HideOSDWindow(ctx, "osd_status");
        GetMythUI()->DisableScreensaver();
    }

    // Save the current channel if this is the first time
    if (ctx->prevChan.empty())
        ctx->PushPreviousChannel();

    PauseAudioUntilBuffered(ctx);
    PauseLiveTV(ctx);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        ctx->player->ResetCaptions();
        ctx->player->ResetTeletext();
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    ctx->recorder->SetChannel(channum);

    if (ctx->player)
        ctx->player->GetAudio()->Reset();

    UnpauseLiveTV(ctx, chanid && GetQueuedChanID());

    if (oldinputname != ctx->recorder->GetInput())
        UpdateOSDInput(ctx);
}

void TV::ChangeChannel(const PlayerContext *ctx, const DBChanList &options)
{
    for (uint i = 0; i < options.size(); i++)
    {
        uint    chanid  = options[i].chanid;
        QString channum = options[i].channum;

        if (chanid && !channum.isEmpty() && IsTunable(ctx, chanid))
        {
            // hide the channel number, activated by certain signal monitors
            HideOSDWindow(ctx, "osd_input");

            QMutexLocker locker(&timerIdLock);
            queuedInput   = channum; queuedInput.detach();
            queuedChanNum = channum; queuedChanNum.detach();
            queuedChanID  = chanid;
            if (!queueInputTimerId)
                queueInputTimerId = StartTimer(10, __LINE__);
            break;
        }
    }
}

void TV::ShowPreviousChannel(PlayerContext *ctx)
{
    QString channum = ctx->GetPreviousChannel();

    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("ShowPreviousChannel: '%1'")
            .arg(channum));

    if (channum.isEmpty())
        return;

    SetOSDText(ctx, "osd_input", "osd_number_entry", channum, kOSDTimeout_Med);
}

void TV::PopPreviousChannel(PlayerContext *ctx, bool immediate_change)
{
    if (!ctx->tvchain)
        return;

    if (!immediate_change)
        ShowPreviousChannel(ctx);

    QString prev_channum = ctx->PopPreviousChannel();
    QString cur_channum  = ctx->tvchain->GetChannelName(-1);

    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("PopPreviousChannel: '%1'->'%2'")
            .arg(cur_channum).arg(prev_channum));

    // Only change channel if previous channel != current channel
    if (cur_channum != prev_channum && !prev_channum.isEmpty())
    {
        QMutexLocker locker(&timerIdLock);
        queuedInput   = prev_channum; queuedInput.detach();
        queuedChanNum = prev_channum; queuedChanNum.detach();
        queuedChanID  = 0;
        if (!queueInputTimerId)
            queueInputTimerId = StartTimer(10, __LINE__);
    }

    if (immediate_change)
    {
        // Turn off OSD Channel Num so the channel changes right away
        HideOSDWindow(ctx, "osd_input");
    }
}

bool TV::ClearOSD(const PlayerContext *ctx)
{
    bool res = false;

    if (HasQueuedInput() || HasQueuedChannel())
    {
        ClearInputQueues(ctx, true);
        res = true;
    }

    OSD *osd = GetOSDLock(ctx);
    if (osd)
    {
        osd->DialogQuit();
        osd->HideAll();
        res = true;
    }
    ReturnOSDLock(ctx, osd);

    if (browsehelper->IsBrowsing())
        browsehelper->BrowseEnd(NULL, false);

    return res;
}

/** \fn TV::ToggleOSD(const PlayerContext*, bool includeStatus)
 *  \brief Cycle through the available Info OSDs.
 */
void TV::ToggleOSD(PlayerContext *ctx, bool includeStatusOSD)
{
    OSD *osd = GetOSDLock(ctx);
    if (!osd)
    {
        ReturnOSDLock(ctx, osd);
        return;
    }

    bool hideAll    = false;
    bool showStatus = false;
    bool paused     = ContextIsPaused(ctx, __FILE__, __LINE__);
    bool is_status_disp    = osd->IsWindowVisible("osd_status");
    bool has_prog_info     = osd->HasWindow("program_info");
    bool is_prog_info_disp = osd->IsWindowVisible("program_info");

    ReturnOSDLock(ctx, osd);

    if (is_status_disp)
    {
        if (has_prog_info)
            UpdateOSDProgInfo(ctx, "program_info");
        else
            hideAll = true;
    }
    else if (is_prog_info_disp && !paused)
    {
        hideAll = true;
    }
    else if (includeStatusOSD)
    {
        showStatus = true;
    }
    else
    {
        if (has_prog_info)
            UpdateOSDProgInfo(ctx, "program_info");
    }

    if (hideAll || showStatus)
    {
        OSD *osd = GetOSDLock(ctx);
        if (osd)
            osd->HideAll();
        ReturnOSDLock(ctx, osd);
    }

    if (showStatus)
    {
        osdInfo info;
        if (ctx->CalcPlayerSliderPosition(info))
        {
            info.text["title"] = paused ? tr("Paused") : tr("Position");
            UpdateOSDStatus(ctx, info, kOSDFunctionalType_Default,
                            paused ? kOSDTimeout_None : kOSDTimeout_Med);
            SetUpdateOSDPosition(true);
        }
        else
        {
            SetUpdateOSDPosition(false);
        }
    }
    else
    {
        SetUpdateOSDPosition(false);
    }
}

void TV::ToggleOSDDebug(PlayerContext *ctx)
{
    bool show = false;
    OSD *osd = GetOSDLock(ctx);
    if (osd && osd->IsWindowVisible("osd_debug"))
    {
        ctx->buffer->EnableBitrateMonitor(false);
        if (ctx->player)
            ctx->player->EnableFrameRateMonitor(false);
        osd->HideWindow("osd_debug");
    }
    else if (osd)
    {
        ctx->buffer->EnableBitrateMonitor(true);
        if (ctx->player)
            ctx->player->EnableFrameRateMonitor(true);
        show = true;
        QMutexLocker locker(&timerIdLock);
        if (!updateOSDDebugTimerId)
            updateOSDDebugTimerId = StartTimer(250, __LINE__);
    }
    ReturnOSDLock(ctx, osd);
    if (show)
        UpdateOSDDebug(ctx);
}

void TV::UpdateOSDDebug(const PlayerContext *ctx)
{
    OSD *osd = GetOSDLock(ctx);
    if (osd && ctx->player)
    {
        InfoMap infoMap;
        ctx->player->GetPlaybackData(infoMap);
        osd->ResetWindow("osd_debug");
        osd->SetText("osd_debug", infoMap, kOSDTimeout_None);
    }
    ReturnOSDLock(ctx, osd);
}

/** \fn TV::UpdateOSDProgInfo(const PlayerContext*, const char *whichInfo)
 *  \brief Update and display the passed OSD set with programinfo
 */
void TV::UpdateOSDProgInfo(const PlayerContext *ctx, const char *whichInfo)
{
    InfoMap infoMap;
    ctx->GetPlayingInfoMap(infoMap);

    QString nightmode = gCoreContext->GetNumSetting("NightModeEnabled", 0)
                            ? "yes" : "no";
    infoMap["nightmode"] = nightmode;

    // Clear previous osd and add new info
    OSD *osd = GetOSDLock(ctx);
    if (osd)
    {
        osd->HideAll();
        osd->SetText(whichInfo, infoMap, kOSDTimeout_Long);
    }
    ReturnOSDLock(ctx, osd);
}

void TV::UpdateOSDStatus(const PlayerContext *ctx, osdInfo &info,
                         int type, OSDTimeout timeout)
{
    OSD *osd = GetOSDLock(ctx);
    if (osd)
    {
        osd->ResetWindow("osd_status");
        QString nightmode = gCoreContext->GetNumSetting("NightModeEnabled", 0)
                                ? "yes" : "no";
        info.text.insert("nightmode", nightmode);
        osd->SetValues("osd_status", info.values, timeout);
        osd->SetText("osd_status",   info.text, timeout);
        if (type != kOSDFunctionalType_Default)
            osd->SetFunctionalWindow("osd_status", (OSDFunctionalType)type);
    }
    ReturnOSDLock(ctx, osd);
}

void TV::UpdateOSDStatus(const PlayerContext *ctx, QString title, QString desc,
                         QString value, int type, QString units,
                         int position, int prev, int next, OSDTimeout timeout)
{
    osdInfo info;
    info.values.insert("position", position);
    info.values.insert("previous", prev);
    info.values.insert("next",     next);
    info.text.insert("title", title);
    info.text.insert("description", desc);
    info.text.insert("value", value);
    info.text.insert("units", units);
    UpdateOSDStatus(ctx, info, type, timeout);
}

void TV::UpdateOSDSeekMessage(const PlayerContext *ctx,
                              const QString &mesg, enum OSDTimeout timeout)
{
    LOG(VB_PLAYBACK, LOG_INFO, QString("UpdateOSDSeekMessage(%1, %2)")
            .arg(mesg).arg(timeout));

    osdInfo info;
    if (ctx->CalcPlayerSliderPosition(info))
    {
        int osdtype = (doSmartForward) ? kOSDFunctionalType_SmartForward :
            kOSDFunctionalType_Default;
        info.text["title"] = mesg;
        UpdateOSDStatus(ctx, info, osdtype, timeout);
        SetUpdateOSDPosition(true);
    }
}

void TV::UpdateOSDInput(const PlayerContext *ctx, QString inputname)
{
    if (!ctx->recorder || !ctx->tvchain)
        return;

    int cardid = ctx->GetCardID();

    if (inputname.isEmpty())
        inputname = ctx->tvchain->GetInputName(-1);

    QString displayName = CardUtil::GetDisplayName(cardid, inputname);
    // If a display name doesn't exist use cardid and inputname
    if (displayName.isEmpty())
        displayName = QString("%1: %2").arg(cardid).arg(inputname);

    SetOSDMessage(ctx, displayName);
}

/** \fn TV::UpdateOSDSignal(const PlayerContext*, const QStringList&)
 *  \brief Updates Signal portion of OSD...
 */
void TV::UpdateOSDSignal(const PlayerContext *ctx, const QStringList &strlist)
{
    OSD *osd = GetOSDLock(ctx);
    if (!osd || browsehelper->IsBrowsing() || !queuedChanNum.isEmpty())
    {
        if (&ctx->lastSignalMsg != &strlist)
        {
            ctx->lastSignalMsg = strlist;
            ctx->lastSignalMsg.detach();
        }
        ReturnOSDLock(ctx, osd);

        QMutexLocker locker(&timerIdLock);
        signalMonitorTimerId[StartTimer(1, __LINE__)] =
            const_cast<PlayerContext*>(ctx);
        return;
    }
    ReturnOSDLock(ctx, osd);

    SignalMonitorList slist = SignalMonitorValue::Parse(strlist);

    InfoMap infoMap = ctx->lastSignalUIInfo;
    if (ctx->lastSignalUIInfoTime.elapsed() > 5000 ||
        infoMap["callsign"].isEmpty())
    {
        ctx->lastSignalUIInfo.clear();
        ctx->GetPlayingInfoMap(ctx->lastSignalUIInfo);

        infoMap = ctx->lastSignalUIInfo;
        ctx->lastSignalUIInfoTime.start();
    }

    int i = 0;
    SignalMonitorList::const_iterator it;
    for (it = slist.begin(); it != slist.end(); ++it)
        if ("error" == it->GetShortName())
            infoMap[QString("error%1").arg(i++)] = it->GetName();
    i = 0;
    for (it = slist.begin(); it != slist.end(); ++it)
        if ("message" == it->GetShortName())
            infoMap[QString("message%1").arg(i++)] = it->GetName();

    uint  sig  = 0;
    float snr  = 0.0f;
    uint  ber  = 0xffffffff;
    int   pos  = -1;
    int   tuned = -1;
    QString pat(""), pmt(""), mgt(""), vct(""), nit(""), sdt(""), crypt("");
    QString err = QString::null, msg = QString::null;
    for (it = slist.begin(); it != slist.end(); ++it)
    {
        if ("error" == it->GetShortName())
        {
            err = it->GetName();
            continue;
        }

        if ("message" == it->GetShortName())
        {
            msg = it->GetName();
            LOG(VB_GENERAL, LOG_INFO, "msg: " + msg);
            continue;
        }

        infoMap[it->GetShortName()] = QString::number(it->GetValue());
        if ("signal" == it->GetShortName())
            sig = it->GetNormalizedValue(0, 100);
        else if ("snr" == it->GetShortName())
            snr = it->GetValue();
        else if ("ber" == it->GetShortName())
            ber = it->GetValue();
        else if ("pos" == it->GetShortName())
            pos = it->GetValue();
        else if ("tuned" == it->GetShortName())
            tuned = it->GetValue();
        else if ("seen_pat" == it->GetShortName())
            pat = it->IsGood() ? "a" : "_";
        else if ("matching_pat" == it->GetShortName())
            pat = it->IsGood() ? "A" : pat;
        else if ("seen_pmt" == it->GetShortName())
            pmt = it->IsGood() ? "m" : "_";
        else if ("matching_pmt" == it->GetShortName())
            pmt = it->IsGood() ? "M" : pmt;
        else if ("seen_mgt" == it->GetShortName())
            mgt = it->IsGood() ? "g" : "_";
        else if ("matching_mgt" == it->GetShortName())
            mgt = it->IsGood() ? "G" : mgt;
        else if ("seen_vct" == it->GetShortName())
            vct = it->IsGood() ? "v" : "_";
        else if ("matching_vct" == it->GetShortName())
            vct = it->IsGood() ? "V" : vct;
        else if ("seen_nit" == it->GetShortName())
            nit = it->IsGood() ? "n" : "_";
        else if ("matching_nit" == it->GetShortName())
            nit = it->IsGood() ? "N" : nit;
        else if ("seen_sdt" == it->GetShortName())
            sdt = it->IsGood() ? "s" : "_";
        else if ("matching_sdt" == it->GetShortName())
            sdt = it->IsGood() ? "S" : sdt;
        else if ("seen_crypt" == it->GetShortName())
            crypt = it->IsGood() ? "c" : "_";
        else if ("matching_crypt" == it->GetShortName())
            crypt = it->IsGood() ? "C" : crypt;
    }
    if (sig)
        infoMap["signal"] = QString::number(sig); // use normalized value

    bool    allGood = SignalMonitorValue::AllGood(slist);
    char    tuneCode;
    QString slock   = ("1" == infoMap["slock"]) ? "L" : "l";
    QString lockMsg = (slock=="L") ? tr("Partial Lock") : tr("No Lock");
    QString sigMsg  = allGood ? tr("Lock") : lockMsg;

    QString sigDesc = tr("Signal %1%").arg(sig,2);
    if (snr > 0.0f)
        sigDesc += " | " + tr("S/N %1dB").arg(log10f(snr), 3, 'f', 1);
    if (ber != 0xffffffff)
        sigDesc += " | " + tr("BE %1", "Bit Errors").arg(ber, 2);
    if ((pos >= 0) && (pos < 100))
        sigDesc += " | " + tr("Rotor %1%").arg(pos,2);

    if (tuned == 1)
        tuneCode = 't';
    else if (tuned == 2)
        tuneCode = 'F';
    else if (tuned == 3)
        tuneCode = 'T';
    else
        tuneCode = '_';

    sigDesc = sigDesc + QString(" | (%1%2%3%4%5%6%7%8%9) %10")
              .arg(tuneCode).arg(slock).arg(pat).arg(pmt).arg(mgt).arg(vct)
              .arg(nit).arg(sdt).arg(crypt).arg(sigMsg);

    if (!err.isEmpty())
        sigDesc = err;
    else if (!msg.isEmpty())
        sigDesc = msg;

    osd = GetOSDLock(ctx);
    if (osd)
    {
        infoMap["description"] = sigDesc;
        osd->SetText("program_info", infoMap, kOSDTimeout_Med);
    }
    ReturnOSDLock(ctx, osd);

    ctx->lastSignalMsg.clear();
    ctx->lastSignalMsgTime.start();

    // Turn off lock timer if we have an "All Good" or good PMT
    if (allGood || (pmt == "M"))
    {
        lockTimerOn = false;
        lastLockSeenTime = QDateTime::currentDateTime();
    }
}

void TV::UpdateOSDTimeoutMessage(PlayerContext *ctx)
{
    bool timed_out = false;

    if (ctx->recorder)
    {
        QString input = ctx->recorder->GetInput();
        uint timeout  = ctx->recorder->GetSignalLockTimeout(input);
        timed_out = lockTimerOn && ((uint)lockTimer.elapsed() > timeout);
    }

    OSD *osd = GetOSDLock(ctx);

    if (!osd)
    {
        if (timed_out)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC +
                "You have no OSD, but tuning has already taken too long.");
        }
        ReturnOSDLock(ctx, osd);
        return;
    }

    bool showing = osd->DialogVisible(OSD_DLG_INFO);
    if (!timed_out)
    {
        if (showing)
            osd->DialogQuit();
        ReturnOSDLock(ctx, osd);
        return;
    }

    if (showing)
    {
        ReturnOSDLock(ctx, osd);
        return;
    }

    // create dialog...
    static QString chan_up   = GET_KEY("TV Playback", ACTION_CHANNELUP);
    static QString chan_down = GET_KEY("TV Playback", ACTION_CHANNELDOWN);
    static QString next_src  = GET_KEY("TV Playback", "NEXTSOURCE");
    static QString tog_cards = GET_KEY("TV Playback", "NEXTINPUT");

    QString message = tr(
        "You should have received a channel lock by now. "
        "You can continue to wait for a signal, or you "
        "can change the channel with %1 or %2, change "
        "video source (%3), inputs (%4), etc.")
        .arg(chan_up).arg(chan_down).arg(next_src).arg(tog_cards);

    osd->DialogShow(OSD_DLG_INFO, message);
    QString action = "DIALOG_INFO_CHANNELLOCK_0";
    osd->DialogAddButton(tr("OK"), action);
    osd->DialogBack("", action, true);

    ReturnOSDLock(ctx, osd);
}

void TV::UpdateLCD(void)
{
    // Make sure the LCD information gets updated shortly
    QMutexLocker locker(&timerIdLock);
    if (lcdTimerId)
        KillTimer(lcdTimerId);
    lcdTimerId = StartTimer(1, __LINE__);
}

void TV::ShowLCDChannelInfo(const PlayerContext *ctx)
{
    LCD *lcd = LCD::Get();
    ctx->LockPlayingInfo(__FILE__, __LINE__);
    if (!lcd || !ctx->playingInfo)
    {
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        return;
    }

    QString title    = ctx->playingInfo->GetTitle();
    QString subtitle = ctx->playingInfo->GetSubtitle();
    QString callsign = ctx->playingInfo->GetChannelSchedulingID();

    ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    if ((callsign != lcdCallsign) || (title != lcdTitle) ||
        (subtitle != lcdSubtitle))
    {
        lcd->switchToChannel(callsign, title, subtitle);
        lcdCallsign = callsign;
        lcdTitle = title;
        lcdSubtitle = subtitle;
    }
}

static void format_time(int seconds, QString &tMin, QString &tHrsMin)
{
    int minutes     = seconds / 60;
    int hours       = minutes / 60;
    int min         = minutes % 60;

    tMin = TV::tr("%n minute(s)", "", minutes);
    tHrsMin.sprintf("%d:%02d", hours, min);
}


void TV::ShowLCDDVDInfo(const PlayerContext *ctx)
{
    LCD *lcd = LCD::Get();

    if (!lcd || !ctx->buffer || !ctx->buffer->IsDVD())
    {
        return;
    }

    DVDRingBuffer *dvd = ctx->buffer->DVD();
    QString dvdName, dvdSerial;
    QString mainStatus, subStatus;

    if (!dvd->GetNameAndSerialNum(dvdName, dvdSerial))
    {
        dvdName = tr("DVD");
    }

    if (dvd->IsInMenu())
    {
        mainStatus = tr("Menu");
    }
    else if (dvd->IsInStillFrame())
    {
        mainStatus = tr("Still Frame");
    }
    else
    {
        QString timeMins, timeHrsMin;
        int playingTitle, playingPart, totalParts;

        dvd->GetPartAndTitle(playingPart, playingTitle);
        totalParts = dvd->NumPartsInTitle();
        format_time(dvd->GetTotalTimeOfTitle(), timeMins, timeHrsMin);

        mainStatus = tr("Title: %1 (%2)").arg(playingTitle).arg(timeHrsMin);
        subStatus = tr("Chapter: %1/%2").arg(playingPart).arg(totalParts);
    }
    if ((dvdName != lcdCallsign) || (mainStatus != lcdTitle) ||
                                    (subStatus != lcdSubtitle))
    {
        lcd->switchToChannel(dvdName, mainStatus, subStatus);
        lcdCallsign = dvdName;
        lcdTitle    = mainStatus;
        lcdSubtitle = subStatus;
    }
}


bool TV::IsTunable(const PlayerContext *ctx, uint chanid, bool use_cache)
{
    return !IsTunableOn(ctx,chanid,use_cache,true).empty();
}

static QString toCommaList(const QSet<uint> &list)
{
    QString ret = "";
    for (QSet<uint>::const_iterator it = list.begin(); it != list.end(); ++it)
        ret += QString("%1,").arg(*it);

    if (ret.length())
        return ret.left(ret.length()-1);

    return "";
}

QSet<uint> TV::IsTunableOn(
    const PlayerContext *ctx, uint chanid, bool use_cache, bool early_exit)
{
    QSet<uint> tunable_cards;

    if (!chanid)
    {
        LOG(VB_CHANNEL, LOG_INFO, LOC +
            QString("IsTunableOn(%1) no").arg(chanid));

        return tunable_cards;
    }

    uint mplexid = ChannelUtil::GetMplexID(chanid);
    mplexid = (32767 == mplexid) ? 0 : mplexid;

    vector<uint> excluded_cards;
    if (ctx->recorder && ctx->pseudoLiveTVState == kPseudoNormalLiveTV)
        excluded_cards.push_back(ctx->GetCardID());

    uint sourceid = ChannelUtil::GetSourceIDForChannel(chanid);
    vector<uint> connected   = RemoteRequestFreeRecorderList();
    vector<uint> interesting = CardUtil::GetCardIDs(sourceid);

    // filter disconnected cards
    vector<uint> cardids = excluded_cards;
    for (uint i = 0; i < connected.size(); i++)
    {
        for (uint j = 0; j < interesting.size(); j++)
        {
            if (connected[i] == interesting[j])
            {
                cardids.push_back(interesting[j]);
                break;
            }
        }
    }

#if 0
    {
        QString msg = QString("cardids[%1]: ").arg(sourceid);
        for (uint i = 0; i < cardids.size(); i++)
            msg += QString("%1, ").arg(cardids[i]);
        LOG(VB_CHANNEL, LOG_INFO,  msg);
    }
#endif

    for (uint i = 0; i < cardids.size(); i++)
    {
        vector<InputInfo> inputs;

        bool used_cache = false;
        if (use_cache)
        {
            QMutexLocker locker(&is_tunable_cache_lock);
            if (is_tunable_cache_inputs.contains(cardids[i]))
            {
                inputs = is_tunable_cache_inputs[cardids[i]];
                used_cache = true;
            }
        }

        if (!used_cache)
        {
            inputs = RemoteRequestFreeInputList(cardids[i], excluded_cards);
            QMutexLocker locker(&is_tunable_cache_lock);
            is_tunable_cache_inputs[cardids[i]] = inputs;
        }

#if 0
    {
        QString msg = QString("inputs[%1]: ").arg(cardids[i]);
        for (uint j = 0; j < inputs.size(); j++)
            msg += QString("%1, ").arg(inputs[j].inputid);
        LOG(VB_CHANNEL, LOG_INFO, msg);
    }
#endif

        for (uint j = 0; j < inputs.size(); j++)
        {
            if (inputs[j].sourceid != sourceid)
                continue;

            if (inputs[j].mplexid &&
                inputs[j].mplexid != mplexid)
                continue;

            tunable_cards.insert(cardids[i]);

            break;
        }

        if (early_exit && !tunable_cards.empty())
            break;
    }

    if (tunable_cards.empty())
    {
        LOG(VB_CHANNEL, LOG_INFO, LOC + QString("IsTunableOn(%1) no")
            .arg(chanid));
    }
    else
    {
        LOG(VB_CHANNEL, LOG_INFO, LOC + QString("IsTunableOn(%1) yes { %2 }")
            .arg(chanid).arg(toCommaList(tunable_cards)));
    }

    return tunable_cards;
}

void TV::ClearTunableCache(void)
{
    QMutexLocker locker(&is_tunable_cache_lock);
    LOG(VB_CHANNEL, LOG_INFO, LOC + "ClearTunableCache()");
    is_tunable_cache_inputs.clear();
}

bool TV::StartEmbedding(const QRect &embedRect)
{
    PlayerContext *ctx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    if (!ctx)
        return false;

    WId wid = GetMythMainWindow()->GetPaintWindow()->winId();

    if (!ctx->IsNullVideoDesired())
        ctx->StartEmbedding(wid, embedRect);
    else
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            QString("StartEmbedding called with null video context #%1")
                .arg(find_player_index(ctx)));
        ctx->ResizePIPWindow(embedRect);
    }

    // Hide any PIP windows...
    PlayerContext *mctx = GetPlayer(ctx, 0);
    for (uint i = 1; (mctx == ctx) && (i < player.size()); i++)
    {
        GetPlayer(ctx,i)->LockDeletePlayer(__FILE__, __LINE__);
        if (GetPlayer(ctx,i)->player)
            GetPlayer(ctx,i)->player->SetPIPVisible(false);
        GetPlayer(ctx,i)->UnlockDeletePlayer(__FILE__, __LINE__);
    }

    // Start checking for end of file for embedded window..
    QMutexLocker locker(&timerIdLock);
    if (embedCheckTimerId)
        KillTimer(embedCheckTimerId);
    embedCheckTimerId = StartTimer(kEmbedCheckFrequency, __LINE__);

    bool embedding = ctx->IsEmbedding();
    ReturnPlayerLock(ctx);
    return embedding;
}

void TV::StopEmbedding(void)
{
    PlayerContext *ctx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    if (!ctx)
        return;

    if (ctx->IsEmbedding())
        ctx->StopEmbedding();

    // Undo any PIP hiding
    PlayerContext *mctx = GetPlayer(ctx, 0);
    for (uint i = 1; (mctx == ctx) && (i < player.size()); i++)
    {
        GetPlayer(ctx,i)->LockDeletePlayer(__FILE__, __LINE__);
        if (GetPlayer(ctx,i)->player)
            GetPlayer(ctx,i)->player->SetPIPVisible(true);
        GetPlayer(ctx,i)->UnlockDeletePlayer(__FILE__, __LINE__);
    }

    // Stop checking for end of file for embedded window..
    QMutexLocker locker(&timerIdLock);
    if (embedCheckTimerId)
        KillTimer(embedCheckTimerId);
    embedCheckTimerId = 0;

    ReturnPlayerLock(ctx);
}

void TV::DrawUnusedRects(void)
{
    if (disableDrawUnusedRects)
        return;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "DrawUnusedRects() -- begin");

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        if (ctx->player)
            ctx->player->ExposeEvent();
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    }
    ReturnPlayerLock(mctx);

    LOG(VB_PLAYBACK, LOG_INFO, LOC + "DrawUnusedRects() -- end");
}

vector<bool> TV::DoSetPauseState(PlayerContext *lctx, const vector<bool> &pause)
{
    vector<bool> was_paused;
    vector<float> times;
    for (uint i = 0; lctx && i < player.size() && i < pause.size(); i++)
    {
        PlayerContext *actx = GetPlayer(lctx, i);
        if (actx)
            was_paused.push_back(ContextIsPaused(actx, __FILE__, __LINE__));
        float time = 0.0f;
        if (pause[i] ^ was_paused.back())
            time = DoTogglePauseStart(GetPlayer(lctx,i));
        times.push_back(time);
    }

    for (uint i = 0; lctx && i < player.size() && i < pause.size(); i++)
    {
        if (pause[i] ^ was_paused[i])
            DoTogglePauseFinish(GetPlayer(lctx,i), times[i], false);
    }

    return was_paused;
}

void TV::DoEditSchedule(int editType)
{
    if ((editType == kScheduleProgramGuide  && !RunProgramGuidePtr) ||
        (editType == kScheduleProgramFinder && !RunProgramFinderPtr) ||
        (editType == kScheduledRecording    && !RunScheduleEditorPtr) ||
        (editType == kViewSchedule          && !RunViewScheduledPtr))
    {
        return;
    }

    PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);

    actx->LockPlayingInfo(__FILE__, __LINE__);
    if (!actx->playingInfo)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            "doEditSchedule(): no active ctx playingInfo.");
        actx->UnlockPlayingInfo(__FILE__, __LINE__);
        ReturnPlayerLock(actx);
        return;
    }

    // Collect channel info
    const ProgramInfo pginfo(*actx->playingInfo);
    uint    chanid  = pginfo.GetChanID();
    QString channum = pginfo.GetChanNum();
    actx->UnlockPlayingInfo(__FILE__, __LINE__);

    ClearOSD(actx);

    // Pause playback as needed...
    bool pause_active = true;
    bool isNearEnd = false;
    bool isLiveTV = StateIsLiveTV(GetState(actx));
    bool allowEmbedding = false;
    bool paused = false;

    {
        actx->LockDeletePlayer(__FILE__, __LINE__);
        pause_active = !actx->player || !actx->player->GetVideoOutput();
        if (actx->player)
        {
            paused = actx->player->IsPaused();
            if (actx->player->GetVideoOutput())
                allowEmbedding =
                    actx->player->GetVideoOutput()->AllowPreviewEPG();
            if (!pause_active)
                isNearEnd = actx->player->IsNearEnd();
        }
        actx->UnlockDeletePlayer(__FILE__, __LINE__);
    }

    pause_active |= kScheduledRecording == editType;
    pause_active |= kViewSchedule == editType;
    pause_active |= kScheduleProgramFinder == editType;
    pause_active |= !isLiveTV && (!db_continue_embedded || isNearEnd);
    pause_active |= paused;
    vector<bool> do_pause;
    do_pause.insert(do_pause.begin(), true, player.size());
    do_pause[find_player_index(actx)] = pause_active;
    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("Pausing player: %1").arg(pause_active));

    saved_pause = DoSetPauseState(actx, do_pause);

    // Resize window to the MythTV GUI size
    PlayerContext *mctx = GetPlayer(actx,0);
    mctx->LockDeletePlayer(__FILE__, __LINE__);
    if (mctx->player && mctx->player->GetVideoOutput())
        mctx->player->GetVideoOutput()->ResizeForGui();
    mctx->UnlockDeletePlayer(__FILE__, __LINE__);
    ReturnPlayerLock(actx);
    MythMainWindow *mwnd = GetMythMainWindow();
    if (!db_use_gui_size_for_tv || !db_use_fixed_size)
    {
        mwnd->setFixedSize(saved_gui_bounds.size());
        mwnd->setGeometry(saved_gui_bounds.left(), saved_gui_bounds.top(),
                          saved_gui_bounds.width(), saved_gui_bounds.height());
    }

    // Actually show the pop-up UI
    switch (editType)
    {
        case kScheduleProgramGuide:
        {
            isEmbedded = (isLiveTV && !pause_active && allowEmbedding);
            RunProgramGuidePtr(chanid, channum, this,
                               isEmbedded, true, channelGroupId);
            ignoreKeyPresses = true;
            break;
        }
        case kScheduleProgramFinder:
        {
            isEmbedded = (isLiveTV && !pause_active && allowEmbedding);
            RunProgramFinderPtr(this, isEmbedded, true);
            ignoreKeyPresses = true;
            break;
        }
        case kScheduledRecording:
        {
            RunScheduleEditorPtr(&pginfo, (void *)this);
            ignoreKeyPresses = true;
            break;
        }
        case kViewSchedule:
        {
            RunViewScheduledPtr((void *)this, !pause_active);
            ignoreKeyPresses = true;
            break;
        }
        case kPlaybackBox:
        {
            RunPlaybackBoxPtr((void *)this, !pause_active);
            ignoreKeyPresses = true;
            break;
        }
    }

    // If the video is paused, don't paint its unused rects & chromakey
    disableDrawUnusedRects = pause_active;

    // We are embedding in a mythui window so assuming no one
    // else has disabled painting show the MythUI window again.
    if (GetMythMainWindow() && weDisabledGUI)
    {
        GetMythMainWindow()->PopDrawDisabled();
        weDisabledGUI = false;
    }
}

void TV::EditSchedule(const PlayerContext *ctx, int editType)
{
    // post the request so the guide will be created in the UI thread
    QString message = QString("START_EPG %1").arg(editType);
    MythEvent* me = new MythEvent(message);
    qApp->postEvent(this, me);
}

void TV::ChangeVolume(PlayerContext *ctx, bool up, int newvolume)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player ||
        (ctx->player && !ctx->player->PlayerControlsVolume()))
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }

    bool setabsolute = (newvolume >= 0 && newvolume <= 100);

    if (ctx->player->IsMuted() && (up || setabsolute))
        ToggleMute(ctx);

    uint curvol = setabsolute ?
                      ctx->player->SetVolume(newvolume) :
                      ctx->player->AdjustVolume((up) ? +2 : -2);

    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (!browsehelper->IsBrowsing())
    {
        UpdateOSDStatus(ctx, tr("Adjust Volume"), tr("Volume"),
                        QString::number(curvol),
                        kOSDFunctionalType_PictureAdjust, "%", curvol * 10,
                        kOSDTimeout_Med);
        SetUpdateOSDPosition(false);

        if (LCD *lcd = LCD::Get())
        {
            QString appName = tr("Video");

            if (StateIsLiveTV(GetState(ctx)))
                appName = tr("TV");

            if (ctx->buffer && ctx->buffer->IsDVD())
                appName = tr("DVD");

            lcd->switchToVolume(appName);
            lcd->setVolumeLevel((float)curvol / 100);

            QMutexLocker locker(&timerIdLock);
            if (lcdVolumeTimerId)
                KillTimer(lcdVolumeTimerId);

            lcdVolumeTimerId = StartTimer(2000, __LINE__);
        }
    }
}

void TV::ToggleTimeStretch(PlayerContext *ctx)
{
    if (ctx->ts_normal == 1.0f)
    {
        ctx->ts_normal = ctx->ts_alt;
    }
    else
    {
        ctx->ts_alt = ctx->ts_normal;
        ctx->ts_normal = 1.0f;
    }
    ChangeTimeStretch(ctx, 0, false);
}

void TV::ChangeTimeStretch(PlayerContext *ctx, int dir, bool allowEdit)
{
    const float kTimeStretchMin = 0.5;
    const float kTimeStretchMax = 2.0;
    float new_ts_normal = ctx->ts_normal + 0.05*dir;
    stretchAdjustment = allowEdit;

    if (new_ts_normal > kTimeStretchMax &&
        ctx->ts_normal < kTimeStretchMax)
    {
        new_ts_normal = kTimeStretchMax;
    }
    else if (new_ts_normal < kTimeStretchMin &&
             ctx->ts_normal > kTimeStretchMin)
    {
        new_ts_normal = kTimeStretchMin;
    }

    if (new_ts_normal > kTimeStretchMax ||
        new_ts_normal < kTimeStretchMin)
    {
        return;
    }

    ctx->ts_normal = new_ts_normal;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player && !ctx->player->IsPaused())
            ctx->player->Play(ctx->ts_normal, true);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (!browsehelper->IsBrowsing())
    {
        if (!allowEdit)
        {
            UpdateOSDSeekMessage(ctx, ctx->GetPlayMessage(), kOSDTimeout_Med);
        }
        else
        {
            UpdateOSDStatus(ctx, tr("Adjust Time Stretch"), tr("Time Stretch"),
                            QString::number(ctx->ts_normal),
                            kOSDFunctionalType_TimeStretchAdjust, "X",
                            (int)(ctx->ts_normal*(1000/kTimeStretchMax)),
                            kOSDTimeout_Med);
            SetUpdateOSDPosition(false);
        }
    }

    SetSpeedChangeTimer(0, __LINE__);
}

void TV::EnableUpmix(PlayerContext *ctx, bool enable, bool toggle)
{
    if (!ctx->player || !ctx->player->HasAudioOut())
        return;
    QString text;

    bool enabled = false;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (toggle)
        enabled = ctx->player->GetAudio()->EnableUpmix(false, true);
    else
        enabled = ctx->player->GetAudio()->EnableUpmix(enable);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (!browsehelper->IsBrowsing())
        SetOSDMessage(ctx, enabled ? tr("Upmixer On") : tr("Upmixer Off"));
}

// dir in 10ms jumps
void TV::ChangeAudioSync(PlayerContext *ctx, int dir, int newsync)
{
    long long newval;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }

    audiosyncAdjustment = true;
    newval = ctx->player->AdjustAudioTimecodeOffset(dir * 10, newsync);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (!browsehelper->IsBrowsing())
    {
        int val = (int)newval;
        UpdateOSDStatus(ctx, tr("Adjust Audio Sync"), tr("Audio Sync"),
                        QString::number(val),
                        kOSDFunctionalType_AudioSyncAdjust,
                        "ms", (val/2) + 500, kOSDTimeout_Med);
        SetUpdateOSDPosition(false);
    }
}

void TV::ToggleMute(PlayerContext *ctx, const bool muteIndividualChannels)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player || !ctx->player->HasAudioOut() ||
        !ctx->player->PlayerControlsVolume())
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }

    MuteState mute_status;

    if (!muteIndividualChannels)
    {
        ctx->player->SetMuted(!ctx->player->IsMuted());
        mute_status = (ctx->player->IsMuted()) ? kMuteAll : kMuteOff;
    }
    else
    {
        mute_status = ctx->player->IncrMuteState();
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    QString text;

    switch (mute_status)
    {
        case kMuteOff:   text = tr("Mute Off"); break;
        case kMuteAll:   text = tr("Mute On"); break;
        case kMuteLeft:  text = tr("Left Channel Muted"); break;
        case kMuteRight: text = tr("Right Channel Muted"); break;
    }

    SetOSDMessage(ctx, text);
}

void TV::ToggleSleepTimer(const PlayerContext *ctx)
{
    QString text;

    // increment sleep index, cycle through
    if (++sleep_index == sleep_times.size())
        sleep_index = 0;

    // set sleep timer to next sleep_index timeout
    if (sleepTimerId)
    {
        KillTimer(sleepTimerId);
        sleepTimerId = 0;
        sleepTimerTimeout = 0;
    }

    if (sleep_times[sleep_index].seconds != 0)
    {
        sleepTimerTimeout = sleep_times[sleep_index].seconds * 1000;
        sleepTimerId = StartTimer(sleepTimerTimeout, __LINE__);
    }

    text = tr("Sleep ") + " " + sleep_times[sleep_index].dispString;

    if (!browsehelper->IsBrowsing())
        SetOSDMessage(ctx, text);
}

void TV::ShowOSDSleep(void)
{
    KillTimer(sleepTimerId);
    sleepTimerId = 0;

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    OSD *osd = GetOSDLock(mctx);
    if (osd)
    {
        QString message = QObject::tr(
            "MythTV was set to sleep after %1 minutes and "
            "will exit in %d seconds.\n"
            "Do you wish to continue watching?")
            .arg(sleepTimerTimeout * (1.0f/60000.0f));

        osd->DialogShow(OSD_DLG_SLEEP, message, kSleepTimerDialogTimeout);
        osd->DialogAddButton(tr("Yes"), "DIALOG_SLEEP_YES_0");
        osd->DialogAddButton(tr("No"),  "DIALOG_SLEEP_NO_0");
    }
    ReturnOSDLock(mctx, osd);
    ReturnPlayerLock(mctx);

    sleepDialogTimerId = StartTimer(kSleepTimerDialogTimeout, __LINE__);
}

void TV::HandleOSDSleep(PlayerContext *ctx, QString action)
{
    if (!DialogIsVisible(ctx, OSD_DLG_SLEEP))
        return;

    if (action == "YES")
    {
        if (sleepDialogTimerId)
        {
            KillTimer(sleepDialogTimerId);
            sleepDialogTimerId = 0;
        }
        sleepTimerId = StartTimer(sleepTimerTimeout * 1000, __LINE__);
    }
    else
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + "No longer watching TV, exiting");
        SetExitPlayer(true, true);
    }
}

void TV::SleepDialogTimeout(void)
{
    KillTimer(sleepDialogTimerId);
    sleepDialogTimerId = 0;

    LOG(VB_GENERAL, LOG_INFO, LOC + "Sleep timeout reached, exiting player.");

    SetExitPlayer(true, true);
}

/*!
 *  \brief After idleTimer has expired, display a dialogue warning the user
 *         that we will exit LiveTV unless they take action.
 *         We change idleTimer, to 45 seconds and when it expires for a second
 *         time we quit the player.
 *         If the user so decides, they may hit ok and we reset the timer
 *         back to the default expiry period.
 */
void TV::ShowOSDIdle(void)
{
    KillTimer(idleTimerId);
    idleTimerId = 0;

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    OSD *osd = GetOSDLock(mctx);
    if (osd)
    {
        QString message = QObject::tr(
            "MythTV has been idle for %1 minutes and "
            "will exit in %d seconds. Are you still watching?")
            .arg(db_idle_timeout * (1.0f/60000.0f));

        osd->DialogShow(OSD_DLG_IDLE, message, kIdleTimerDialogTimeout);
        osd->DialogAddButton(tr("Yes"), "DIALOG_IDLE_YES_0");
        osd->DialogAddButton(tr("No"),  "DIALOG_IDLE_NO_0");
    }
    ReturnOSDLock(mctx, osd);
    ReturnPlayerLock(mctx);

    idleDialogTimerId = StartTimer(kIdleTimerDialogTimeout, __LINE__);
}

void TV::HandleOSDIdle(PlayerContext *ctx, QString action)
{
    if (!DialogIsVisible(ctx, OSD_DLG_IDLE))
        return;

    if (action == "YES")
    {
        if (idleDialogTimerId)
        {
            KillTimer(idleDialogTimerId);
            idleDialogTimerId = 0;
        }
        if (idleTimerId)
            KillTimer(idleTimerId);
        idleTimerId = StartTimer(db_idle_timeout, __LINE__);
    }
    else
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + "No longer watching LiveTV, exiting");
        SetExitPlayer(true, true);
    }
}

void TV::IdleDialogTimeout(void)
{
    KillTimer(idleDialogTimerId);
    idleDialogTimerId = 0;

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    if (StateIsLiveTV(mctx->GetState()))
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + "Idle timeout reached, leaving LiveTV");
        SetExitPlayer(true, true);
    }
    ReturnPlayerLock(mctx);
}

void TV::ToggleAspectOverride(PlayerContext *ctx, AspectOverrideMode aspectMode)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }
    ctx->player->ToggleAspectOverride(aspectMode);
    QString text = toString(ctx->player->GetAspectOverride());
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    SetOSDMessage(ctx, text);
}

void TV::ToggleAdjustFill(PlayerContext *ctx, AdjustFillMode adjustfillMode)
{
    if (ctx != GetPlayer(ctx,0) || ctx->IsPBP())
        return;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (!ctx->player)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }
    ctx->player->ToggleAdjustFill(adjustfillMode);
    QString text = toString(ctx->player->GetAdjustFill());
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    SetOSDMessage(ctx, text);
}

void TV::PauseAudioUntilBuffered(PlayerContext *ctx)
{
    if (!ctx)
        return;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->GetAudio()->PauseAudioUntilBuffered();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

static struct ActionDefStruct<TV> tmeActions[] = {
    { ACTION_SETVOLUME,     &TV::doMythEventSetVolume },
    { ACTION_SETAUDIOSYNC,  &TV::doMythEventSetAudioSync },
    { ACTION_SETBRIGHTNESS, &TV::doMythEventSetBrightness },
    { ACTION_SETCONTRAST,   &TV::doMythEventSetContrast },
    { ACTION_SETCOLOUR,     &TV::doMythEventSetColor },
    { ACTION_SETHUE,        &TV::doMythEventSetHue },
    { ACTION_JUMPCHAPTER,   &TV::doMythEventJumpChapter },
    { ACTION_SWITCHTITLE,   &TV::doMythEventSwitchTitle },
    { ACTION_SWITCHANGLE,   &TV::doMythEventSwitchAngle },
    { ACTION_SEEKABSOLUTE,  &TV::doMythEventSeekAbsolute },
    { ACTION_SCREENSHOT,    &TV::doMythEventScreenshot },
    { ACTION_GETSTATUS,     &TV::doMythEventGetStatus },
    { "^DONE_RECORDING",    &TV::doMythEventDoneRecording },
    { "^ASK_RECORDING",     &TV::doMythEventAskRecording },
    { "^QUIT_LIVETV",       &TV::doMythEventQuitLiveTV },
    { "^LIVETV_WATCH",      &TV::doMythEventLiveTVWatch },
    { "^LIVETV_CHAIN",      &TV::doMythEventLiveTVChain },
    { "^EXIT_TO_MENU",      &TV::doMythEventExitToMenu },
    { "^NETWORK_CONTROL",   &TV::doMythEventNetworkControl },
    { "^(EPG|PROGFINDER|VIEWSCHEDULED|PLAYBACKBOX|SCHEDULEEDITOR)_EXITING",
                            &TV::doMythEventExiting },
    { "^COMMFLAG_START",    &TV::doMythEventCommflagStart },
    { "^COMMFLAG_UPDATE",   &TV::doMythEventCommflagUpdate }
};
static int tmeActionCount = NELEMS(tmeActions);

bool TV::doMythEventSetVolume(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    ChangeVolume(ctx, false, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventSetAudioSync(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    ChangeAudioSync(ctx, 0, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventSetBrightness(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    DoChangePictureAttribute(ctx, kAdjustingPicture_Playback,
                             kPictureAttribute_Brightness, false, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventSetContrast(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    DoChangePictureAttribute(ctx, kAdjustingPicture_Playback,
                             kPictureAttribute_Contrast, false, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventSetColor(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    DoChangePictureAttribute(ctx, kAdjustingPicture_Playback,
                             kPictureAttribute_Colour, false, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventSetHue(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    DoChangePictureAttribute(ctx, kAdjustingPicture_Playback,
                             kPictureAttribute_Hue, false, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventJumpChapter(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    DoJumpChapter(ctx, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventSwitchTitle(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    DoSwitchTitle(ctx, value - 1);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventSwitchAngle(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    DoSwitchAngle(ctx, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventSeekAbsolute(const QString &action)
{
    if (m_actionMythEventArgs.size() != 1)
        return false;

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    int value = m_actionMythEventArgs[0].toInt();
    DoSeekAbsolute(ctx, value);
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventScreenshot(const QString &action)
{
    int width = 0;
    int height = 0;
    QString filename;

    if (m_actionMythEventArgs.size() >= 2)
    {
        width  = m_actionMythEventArgs[0].toInt();
        height = m_actionMythEventArgs[1].toInt();

        if (m_actionMythEventArgs.size() == 3)
            filename = m_actionMythEventArgs[2];
    }

    PlayerContext *ctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    MythPlayer *play = ctx->player;

    if (!play || !play->GetScreenShot(width, height, filename))
    {
        GetMythMainWindow()->ScreenShot(width, height, filename);
    }
    ReturnPlayerLock(ctx);
    return true;
}

bool TV::doMythEventGetStatus(const QString &action)
{
    GetStatus();
    return true;
}

bool TV::doMythEventDoneRecording(const QString &action)
{
    uint cardnum = 0;
    int seconds = 0;
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    if (tokens.size() >= 4)
    {
        cardnum = tokens[1].toUInt();
        seconds = tokens[2].toInt();
        //frames = tokens[3].toLongLong();
    }

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        if (ctx->GetState() == kState_WatchingRecording)
        {
            if (ctx->recorder && (cardnum == ctx->GetCardID()))
            {
                ctx->LockDeletePlayer(__FILE__, __LINE__);
                if (ctx->player)
                {
                    ctx->player->SetWatchingRecording(false);
                    ctx->player->SetLength(seconds);
                }
                ctx->UnlockDeletePlayer(__FILE__, __LINE__);

                ctx->ChangeState(kState_WatchingPreRecorded);
                ScheduleStateChange(ctx);
            }
        }
        else if (StateIsLiveTV(ctx->GetState()))
        {
            if (ctx->recorder && cardnum == ctx->GetCardID() &&
                ctx->tvchain && ctx->tvchain->HasNext())
            {
                ctx->LockDeletePlayer(__FILE__, __LINE__);
                if (ctx->player)
                {
                    ctx->player->SetWatchingRecording(false);
                    ctx->player->SetLength(seconds);
                }
                ctx->UnlockDeletePlayer(__FILE__, __LINE__);
            }
        }
    }
    ReturnPlayerLock(mctx);
    return true;
}

bool TV::doMythEventAskRecording(const QString &action)
{
    uint cardnum = 0;
    int timeuntil = 0;
    int hasrec = 0;
    int haslater = 0;

    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    if (tokens.size() >= 5)
    {
        cardnum   = tokens[1].toUInt();
        timeuntil = tokens[2].toInt();
        hasrec    = tokens[3].toInt();
        haslater  = tokens[4].toInt();
    }

    LOG(VB_GENERAL, LOG_INFO,
        LOC + action + QString(" hasrec: %1 haslater: %2")
            .arg(hasrec).arg(haslater));

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    if (mctx->recorder && cardnum == mctx->GetCardID())
    {
        AskAllowRecording(mctx, m_actionMythEventArgs,
                          timeuntil, hasrec, haslater);
    }

    for (uint i = 1; i < player.size(); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        if (ctx->recorder && ctx->GetCardID() == cardnum)
        {
            LOG(VB_GENERAL, LOG_INFO, LOC + "Disabling PxP for recording");
            QString type = ctx->IsPIP() ?  tr("PiP", "Picture-in-Picture") :
                                           tr("PbP", "Picture-by-Picture");
            StopStuff(mctx, ctx, true, true, true);
            SetOSDMessage(mctx, tr("Disabling %1 for recording").arg(type));
        }
    }
    ReturnPlayerLock(mctx);
    return true;
}

bool TV::doMythEventQuitLiveTV(const QString &action)
{
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    uint cardnum = (tokens.size() >= 2) ? tokens[1].toUInt() : 0;

    PlayerContext *mctx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    int match = -1;
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        match = (ctx->GetCardID() == cardnum) ? i : match;
    }

    if (match >= 0 && GetPlayer(mctx, match)->recorder)
    {
        if (match == 0)
        {
            for (uint i = 1; mctx && (i < player.size()); i++)
            {
                PlayerContext *ctx = GetPlayer(mctx, i);
                if (ctx->recorder && (ctx->GetCardID() == cardnum))
                {
                    LOG(VB_GENERAL, LOG_INFO, LOC +
                        "Disabling PiP for QUIT_LIVETV");
                    StopStuff(mctx, ctx, true, true, true);
                }
            }
            SetLastProgram(NULL);
            jumpToProgram = true;
            SetExitPlayer(true, false);
        }
        else
        {
            PlayerContext *ctx = GetPlayer(mctx, match);
            StopStuff(mctx, ctx, true, true, true);
        }
    }
    ReturnPlayerLock(mctx);
    return true;
}

bool TV::doMythEventLiveTVWatch(const QString &action)
{
    uint cardnum = 0;
    int watch = 0;
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    if (tokens.size() >= 3)
    {
        cardnum = tokens[1].toUInt();
        watch   = tokens[2].toInt();
    }

    PlayerContext *mctx = GetPlayerWriteLock(0, __FILE__, __LINE__);
    int match = -1;
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        match = (ctx->GetCardID() == cardnum) ? i : match;
    }

    if (match >= 0)
    {
        if (watch)
        {
            ProgramInfo pi(m_actionMythEventArgs);
            if (pi.HasPathname() || pi.GetChanID())
            {
                PlayerContext *ctx = GetPlayer(mctx, match);
                ctx->SetPseudoLiveTV(&pi, kPseudoChangeChannel);

                QMutexLocker locker(&timerIdLock);
                if (!pseudoChangeChanTimerId)
                    pseudoChangeChanTimerId = StartTimer(0, __LINE__);
            }
        }
        else
        {
            PlayerContext *ctx = GetPlayer(mctx, match);
            ctx->SetPseudoLiveTV(NULL, kPseudoNormalLiveTV);
        }
    }
    ReturnPlayerLock(mctx);
    return true;
}

bool TV::doMythEventLiveTVChain(const QString &action)
{
    QString id = QString::null;
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    if ((tokens.size() >= 2) && tokens[1] == "UPDATE")
        id = tokens[2];

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        if (ctx->tvchain && ctx->tvchain->GetID() == id)
        {
            QMutexLocker locker(&timerIdLock);
            tvchainUpdateTimerId[StartTimer(1, __LINE__)] = ctx;
            break;
        }
    }
    ReturnPlayerLock(mctx);
    return true;
}

bool TV::doMythEventExitToMenu(const QString &action)
{
    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        PrepareToExitPlayer(ctx, __LINE__, db_playback_exit_prompt == 1 ||
                                           db_playback_exit_prompt == 2);
    }

    SetExitPlayer(true, true);
    if (mctx && mctx->player)
        mctx->player->DisableEdit(-1);
    ReturnPlayerLock(mctx);
    return true;
}

bool TV::doMythEventSignal(const QString &action)
{
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    uint cardnum = (tokens.size() >= 2) ? tokens[1].toUInt() : 0;

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    OSD *osd = GetOSDLock(mctx);
    if (osd && !osd->IsWindowVisible(OSD_WIN_INTERACT))
    {
        for (uint i = 0; mctx && (i < player.size()); i++)
        {
            PlayerContext *ctx = GetPlayer(mctx, i);
            bool tc = ctx->recorder && (ctx->GetCardID() == cardnum);
            if (tc && !m_actionMythEventArgs.empty())
            {
                UpdateOSDSignal(ctx, m_actionMythEventArgs);
                UpdateOSDTimeoutMessage(ctx);
            }
        }
    }
    ReturnOSDLock(mctx, osd);
    ReturnPlayerLock(mctx);
    return true;
}

bool TV::doMythEventNetworkControl(const QString &action)
{
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    if ((tokens.size() >= 2) &&
        (tokens[1] != "ANSWER") && (tokens[1] != "RESPONSE"))
    {
        QMutexLocker locker(&timerIdLock);
        QString command(action);
        command.detach();
        networkControlCommands.enqueue(command);
        if (!networkControlTimerId)
            networkControlTimerId = StartTimer(1, __LINE__);
    }
    return true;
}

bool TV::doMythEventStartEPG(const QString &action)
{
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    int editType = tokens[1].toInt();
    DoEditSchedule(editType);
    return true;
}

bool TV::doMythEventExiting(const QString &action)
{
    // Resize the window back to the MythTV Player size
    PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);
    PlayerContext *mctx;
    MythMainWindow *mwnd = GetMythMainWindow();

    StopEmbedding();
    MythPainter *painter = GetMythPainter();
    if (painter)
        painter->FreeResources();

    mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    mctx->LockDeletePlayer(__FILE__, __LINE__);
    if (mctx->player && mctx->player->GetVideoOutput())
        mctx->player->GetVideoOutput()->ResizeForVideo();
    mctx->UnlockDeletePlayer(__FILE__, __LINE__);
    ReturnPlayerLock(mctx);

    if (!db_use_gui_size_for_tv || !db_use_fixed_size)
    {
        mwnd->setMinimumSize(QSize(16, 16));
        mwnd->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        mwnd->setGeometry(player_bounds.left(), player_bounds.top(),
                          player_bounds.width(), player_bounds.height());
    }

    DoSetPauseState(actx, saved_pause); // Restore pause states
    disableDrawUnusedRects = false;

    qApp->processEvents();

    if (!weDisabledGUI)
    {
        weDisabledGUI = true;
        GetMythMainWindow()->PushDrawDisabled();
        DrawUnusedRects();
    }

    isEmbedded = false;
    ignoreKeyPresses = false;

    QString which(action);
    which.chop(8);
    if (which == "PLAYBACKBOX")
    {
        ProgramInfo pginfo(m_actionMythEventArgs);
        if (pginfo.HasPathname() || pginfo.GetChanID())
            PrepToSwitchToRecordedProgram(actx, pginfo);
    }

    ReturnPlayerLock(actx);
    return true;
}

bool TV::doMythEventCommflagStart(const QString &action)
{
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    if (tokens.size() < 2)
        return false;

    uint evchanid = 0;
    QDateTime evrecstartts;
    ProgramInfo::ExtractKey(tokens[1], evchanid, evrecstartts);

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    for (uint i = 0; mctx && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        ctx->LockPlayingInfo(__FILE__, __LINE__);
        bool doit = ((ctx->playingInfo) &&
                   (ctx->playingInfo->GetChanID()             == evchanid) &&
                   (ctx->playingInfo->GetRecordingStartTime() == evrecstartts));
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);

        if (doit)
        {
            QString msg = "COMMFLAG_REQUEST ";
            msg += ProgramInfo::MakeUniqueKey(evchanid, evrecstartts);
            gCoreContext->SendMessage(msg);
        }
    }
    ReturnPlayerLock(mctx);
    return true;
}

bool TV::doMythEventCommflagUpdate(const QString &action)
{
    QStringList tokens = action.split(" ", QString::SkipEmptyParts);
    if (tokens.size() < 3)
        return false;

    uint evchanid = 0;
    QDateTime evrecstartts;
    ProgramInfo::ExtractKey(tokens[1], evchanid, evrecstartts);

    PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
    for (uint i = 0; mctx && evchanid && (i < player.size()); i++)
    {
        PlayerContext *ctx = GetPlayer(mctx, i);
        ctx->LockPlayingInfo(__FILE__, __LINE__);
        bool doit = ((ctx->playingInfo) &&
                   (ctx->playingInfo->GetChanID()             == evchanid) &&
                   (ctx->playingInfo->GetRecordingStartTime() == evrecstartts));
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);

        if (doit)
        {
            frm_dir_map_t newMap;
            QStringList mark;
            QStringList marks = tokens[2].split(",", QString::SkipEmptyParts);
            for (uint i = 0; i < (uint)marks.size(); i++)
            {
                mark = marks[i].split(":", QString::SkipEmptyParts);
                if (marks.size() >= 2)
                {
                    newMap[mark[0].toLongLong()] = (MarkTypes) mark[1].toInt();
                }
            }
            ctx->LockDeletePlayer(__FILE__, __LINE__);
            if (ctx->player)
                ctx->player->SetCommBreakMap(newMap);
            ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        }
    }
    ReturnPlayerLock(mctx);
    return true;
}


/// This handles all custom events
void TV::customEvent(QEvent *e)
{
    if (e->type() == MythEvent::kUpdateTvProgressEventType && myWindow)
    {
        myWindow->UpdateProgress();
        return;
    }

    if (e->type() == MythEvent::MythUserMessage)
    {
        MythEvent *me = reinterpret_cast<MythEvent*>(e);
        QString message = me->Message();

        if (message.isEmpty())
            return;

        uint timeout = 0;
        if (me->ExtraDataCount() == 1)
        {
            uint t = me->ExtraData(0).toUInt();
            if (t > 0 && t < 1000)
                timeout = t * 1000;
        }

        if (timeout > 0)
            message += " (%d)";

        PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
        OSD *osd = GetOSDLock(mctx);
        if (osd)
            osd->DialogShow(OSD_DLG_CONFIRM, message, timeout);
        ReturnOSDLock(mctx, osd);
        ReturnPlayerLock(mctx);

        return;
    }

    if (e->type() == MythEvent::kUpdateBrowseInfoEventType)
    {
        UpdateBrowseInfoEvent *b =
            reinterpret_cast<UpdateBrowseInfoEvent*>(e);
        PlayerContext *mctx = GetPlayerReadLock(0, __FILE__, __LINE__);
        OSD *osd = GetOSDLock(mctx);
        if (osd)
        {
            osd->SetText("browse_info", b->im, kOSDTimeout_None);
            osd->SetExpiry("browse_info", kOSDTimeout_None);
        }
        ReturnOSDLock(mctx, osd);
        ReturnPlayerLock(mctx);
        return;
    }

    if (e->type() == DialogCompletionEvent::kEventType)
    {
        DialogCompletionEvent *dce =
            reinterpret_cast<DialogCompletionEvent*>(e);
        LOG(VB_KEYPRESS, LOG_INFO, "OSDDialog Start");
        OSDDialogEvent(dce->GetResult(), dce->GetResultText(),
                       dce->GetData().toString());
        LOG(VB_KEYPRESS, LOG_INFO, "OSDDialog Finish");
        return;
    }

    if (e->type() == OSDHideEvent::kEventType)
    {
        OSDHideEvent *ce = reinterpret_cast<OSDHideEvent*>(e);
        HandleOSDClosed(ce->GetFunctionalType());
        return;
    }

    if (e->type() != MythEvent::MythEventMessage)
        return;

    MythEvent *me = reinterpret_cast<MythEvent*>(e);
    QString message = me->Message();
    m_actionMythEventArgs = me->ExtraDataList();

    if (!m_mythEventActions)
        m_mythEventActions = new MythActions<TV>(this, tmeActions,
                                                 tmeActionCount);
    bool touched;
    m_mythEventActions->handleAction(message, touched);
}

void TV::ToggleRecord(PlayerContext *ctx)
{
    BrowseInfo bi = browsehelper->GetBrowsedInfo();
    if (bi.m_chanid)
    {
        InfoMap infoMap;
        QDateTime startts = QDateTime::fromString(
            bi.m_starttime, Qt::ISODate);

        RecordingInfo::LoadStatus status;
        RecordingInfo recinfo(bi.m_chanid, startts, false, 0, &status);
        if (RecordingInfo::kFoundProgram == status)
            recinfo.ToggleRecord();
        recinfo.ToMap(infoMap);
        infoMap["iconpath"] = ChannelUtil::GetIcon(recinfo.GetChanID());
        if ((recinfo.IsVideoFile() || recinfo.IsVideoDVD() ||
            recinfo.IsVideoBD()) && recinfo.GetPathname() != recinfo.GetBasename())
        {
            infoMap["coverartpath"] = VideoMetaDataUtil::GetArtPath(
                recinfo.GetPathname(), "Coverart");
            infoMap["fanartpath"] = VideoMetaDataUtil::GetArtPath(
                recinfo.GetPathname(), "Fanart");
            infoMap["bannerpath"] = VideoMetaDataUtil::GetArtPath(
                recinfo.GetPathname(), "Banners");
            infoMap["screenshotpath"] = VideoMetaDataUtil::GetArtPath(
                recinfo.GetPathname(), "Screenshots");
        }

        OSD *osd = GetOSDLock(ctx);
        if (osd)
        {
            osd->SetText("browse_info", infoMap, kOSDTimeout_Med);
            QHash<QString,QString> map;
            map.insert("message_text", tr("Record"));
            osd->SetText("osd_message", map, kOSDTimeout_Med);
        }
        ReturnOSDLock(ctx, osd);
        return;
    }

    ctx->LockPlayingInfo(__FILE__, __LINE__);
    if (!ctx->playingInfo)
    {
        LOG(VB_GENERAL, LOG_INFO, LOC + "Unknown recording during live tv.");
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        return;
    }

    QString cmdmsg("");
    if (ctx->playingInfo->QueryAutoExpire() == kLiveTVAutoExpire)
    {
        RecordingInfo recInfo(*ctx->playingInfo);
        recInfo.SaveAutoExpire((AutoExpireType)db_autoexpire_default);
        recInfo.ApplyRecordRecGroupChange("Default");
        *ctx->playingInfo = recInfo;

        cmdmsg = tr("Record");
        ctx->SetPseudoLiveTV(ctx->playingInfo, kPseudoRecording);
        ctx->recorder->SetLiveRecording(true);
        LOG(VB_RECORD, LOG_INFO, LOC + "Toggling Record on");
    }
    else
    {
        RecordingInfo recInfo(*ctx->playingInfo);
        recInfo.SaveAutoExpire(kLiveTVAutoExpire);
        recInfo.ApplyRecordRecGroupChange("LiveTV");
        *ctx->playingInfo = recInfo;

        cmdmsg = tr("Cancel Record");
        ctx->SetPseudoLiveTV(ctx->playingInfo, kPseudoNormalLiveTV);
        ctx->recorder->SetLiveRecording(false);
        LOG(VB_RECORD, LOG_INFO, LOC + "Toggling Record off");
    }

    QString msg = cmdmsg + " \"" + ctx->playingInfo->GetTitle() + "\"";

    ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    SetOSDMessage(ctx, msg);
}

void TV::HandleOSDClosed(int osdType)
{
    switch (osdType)
    {
        case kOSDFunctionalType_PictureAdjust:
            adjustingPicture = kAdjustingPicture_None;
            adjustingPictureAttribute = kPictureAttribute_None;
            break;
        case kOSDFunctionalType_SmartForward:
            doSmartForward = false;
            break;
        case kOSDFunctionalType_TimeStretchAdjust:
            stretchAdjustment = false;
            break;
        case kOSDFunctionalType_AudioSyncAdjust:
            audiosyncAdjustment = false;
            break;
        case kOSDFunctionalType_Default:
            break;
    }
}

PictureAttribute TV::NextPictureAdjustType(
    PictureAdjustType type, MythPlayer *mp, PictureAttribute attr)
{
    if (!mp)
        return kPictureAttribute_None;

    uint sup = kPictureAttributeSupported_None;
    if ((kAdjustingPicture_Playback == type) && mp && mp->GetVideoOutput())
    {
        sup = mp->GetVideoOutput()->GetSupportedPictureAttributes();
        if (mp->HasAudioOut() && mp->PlayerControlsVolume())
            sup |= kPictureAttributeSupported_Volume;
    }
    else if (kAdjustingPicture_Channel == type)
    {
        sup = (kPictureAttributeSupported_Brightness |
               kPictureAttributeSupported_Contrast |
               kPictureAttributeSupported_Colour |
               kPictureAttributeSupported_Hue);
    }
    else if (kAdjustingPicture_Recording == type)
    {
        sup = (kPictureAttributeSupported_Brightness |
               kPictureAttributeSupported_Contrast |
               kPictureAttributeSupported_Colour |
               kPictureAttributeSupported_Hue);
    }

    return ::next((PictureAttributeSupported)sup, (PictureAttribute) attr);
}

void TV::DoToggleStudioLevels(const PlayerContext *ctx)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    ctx->player->ToggleStudioLevels();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::DoToggleNightMode(const PlayerContext *ctx)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    ctx->player->ToggleNightMode();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::DoTogglePictureAttribute(const PlayerContext *ctx,
                                  PictureAdjustType type)
{
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    PictureAttribute attr = NextPictureAdjustType(type, ctx->player,
                                                  adjustingPictureAttribute);
    if (kPictureAttribute_None == attr)
    {
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        return;
    }

    adjustingPicture          = type;
    adjustingPictureAttribute = attr;

    QString title = toTitleString(type);

    int value = 99;
    if (kAdjustingPicture_Playback == type)
    {
        if (!ctx->player)
        {
            ctx->UnlockDeletePlayer(__FILE__, __LINE__);
            return;
        }
        if (kPictureAttribute_Volume != adjustingPictureAttribute)
        {
            value = ctx->player->GetVideoOutput()->GetPictureAttribute(attr);
        }
        else if (ctx->player->HasAudioOut())
        {
            value = ctx->player->GetVolume();
            title = tr("Adjust Volume");
        }
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (ctx->recorder && (kAdjustingPicture_Playback != type))
    {
        value = ctx->recorder->GetPictureAttribute(attr);
    }

    QString text = toString(attr) + " " + toTypeString(type);

    UpdateOSDStatus(ctx, title, text, QString::number(value),
                    kOSDFunctionalType_PictureAdjust, "%",
                    value * 10, kOSDTimeout_Med);
    SetUpdateOSDPosition(false);
}

void TV::DoChangePictureAttribute(
    PlayerContext *ctx,
    PictureAdjustType type, PictureAttribute attr,
    bool up, int newvalue)
{
    int value = 99;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (kAdjustingPicture_Playback == type)
    {
        if (kPictureAttribute_Volume == attr)
        {
            ctx->UnlockDeletePlayer(__FILE__, __LINE__);
            ChangeVolume(ctx, up, newvalue);
            return;
        }
        if (!ctx->player)
        {
            ctx->UnlockDeletePlayer(__FILE__, __LINE__);
            return;
        }

        if (ctx->player->GetVideoOutput())
        {
            VideoOutput *vo = ctx->player->GetVideoOutput();
            if ((newvalue >= 0) && (newvalue <= 100))
                value = vo->SetPictureAttribute(attr, newvalue);
            else
                value = vo->ChangePictureAttribute(attr, up);
        }
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (ctx->recorder && (kAdjustingPicture_Playback != type))
    {
        value = ctx->recorder->ChangePictureAttribute(type, attr, up);
    }

    QString text = toString(attr) + " " + toTypeString(type);

    UpdateOSDStatus(ctx, toTitleString(type), text, QString::number(value),
                    kOSDFunctionalType_PictureAdjust, "%",
                    value * 10, kOSDTimeout_Med);
    SetUpdateOSDPosition(false);
}

void TV::SetActive(PlayerContext *lctx, int index, bool osd_msg)
{
    if (!lctx)
        return;

    int new_index = (index < 0) ? (playerActive+1) % player.size() : index;
    new_index = ((uint)new_index >= player.size()) ? 0 : new_index;

    QString loc = LOC + QString("SetActive(%1,%2) %3 -> %4")
        .arg(index).arg((osd_msg) ? "with OSD" : "w/o OSD")
        .arg(playerActive).arg(new_index);

    LOG(VB_PLAYBACK, LOG_INFO, loc + " -- begin");

    for (uint i = 0; i < player.size(); i++)
        ClearOSD(GetPlayer(lctx,i));

    playerActive = new_index;

    for (int i = 0; i < (int)player.size(); i++)
    {
        PlayerContext *ctx = GetPlayer(lctx, i);
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        if (ctx->player)
            ctx->player->SetPIPActive(i == playerActive);
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    }

    if (osd_msg && !GetPlayer(lctx, -1)->IsPIP() && player.size() > 1)
    {
        PlayerContext *actx = GetPlayer(lctx, -1);
        SetOSDMessage(actx, tr("Active Changed"));
    }

    LOG(VB_PLAYBACK, LOG_INFO, loc + " -- end");
}

void TV::ShowOSDCutpoint(PlayerContext *ctx, const QString &type)
{
    OSD *osd = GetOSDLock(ctx);
    if (!osd)
    {
        ReturnOSDLock(ctx, osd);
        return;
    }


    if (("EDIT_CUT_POINTS" == type) || ("EDIT_CUT_REGION" == type))
    {
        uint64_t frame   = ctx->player->GetFramesPlayed();
        uint64_t previous_cut = ctx->player->GetNearestMark(frame, false);
        uint64_t next_cut = ctx->player->GetNearestMark(frame, true);
        uint64_t total_frames = ctx->player->GetTotalFrameCount();

        osd->DialogShow(OSD_DLG_CUTPOINT,
                        QObject::tr("Edit Cut Points"));
        if (ctx->player->IsInDelete(frame))
        {
            if (ctx->player->IsTemporaryMark(frame))
            {
                if (previous_cut > 0)
                    osd->DialogAddButton(QObject::tr("Move Previous Cut End "
                                                     "Here"),
                                         QString("DIALOG_CUTPOINT_MOVEPREV_%1")
                                                 .arg(frame));
                else
                    osd->DialogAddButton(QObject::tr("Cut to Beginning"),
                                   QString("DIALOG_CUTPOINT_CUTTOBEGINNING_%1")
                                           .arg(frame));

                if (next_cut == total_frames)
                    osd->DialogAddButton(QObject::tr("Cut to End"),
                                         QString("DIALOG_CUTPOINT_CUTTOEND_%1")
                                                 .arg(frame));
                else
                    osd->DialogAddButton(QObject::tr("Move Next Cut Start "
                                                     "Here"),
                                         QString("DIALOG_CUTPOINT_MOVENEXT_%1")
                                                 .arg(frame));
            }
            else
            {
                osd->DialogAddButton(QObject::tr("Move Start of Cut Here"),
                                     QString("DIALOG_CUTPOINT_MOVEPREV_%1")
                                             .arg(frame));
                osd->DialogAddButton(QObject::tr("Move End of Cut Here"),
                                     QString("DIALOG_CUTPOINT_MOVENEXT_%1")
                                             .arg(frame));
            }
            osd->DialogAddButton(QObject::tr("Delete This Cut"),
                                 QString("DIALOG_CUTPOINT_DELETE_%1")
                                         .arg(frame));
        }
        else
        {
            if (previous_cut > 0)
                osd->DialogAddButton(QObject::tr("Move Previous Cut End Here"),
                                     QString("DIALOG_CUTPOINT_MOVEPREV_%1")
                                             .arg(frame));
            else
                osd->DialogAddButton(QObject::tr("Cut to Beginning"),
                                  QString("DIALOG_CUTPOINT_CUTTOBEGINNING_%1")
                                          .arg(frame));
            if (next_cut == total_frames)
                osd->DialogAddButton(QObject::tr("Cut to End"),
                                     QString("DIALOG_CUTPOINT_CUTTOEND_%1")
                                             .arg(frame));
            else
                osd->DialogAddButton(QObject::tr("Move Next Cut Start Here"),
                                     QString("DIALOG_CUTPOINT_MOVENEXT_%1")
                                             .arg(frame));
            osd->DialogAddButton(QObject::tr("Add New Cut"),
                                 QString("DIALOG_CUTPOINT_NEWCUT_%1")
                                         .arg(frame));
            osd->DialogAddButton(QObject::tr("Join Surrounding Cuts"),
                                 QString("DIALOG_CUTPOINT_DELETE_%1")
                                         .arg(frame));
        }
        if (ctx->player->DeleteMapHasUndo())
            osd->DialogAddButton(QObject::tr("Undo") + " - " +
                                 ctx->player->DeleteMapGetUndoMessage(),
                                 QString("DIALOG_CUTPOINT_UNDO_0"));
        if (ctx->player->DeleteMapHasRedo())
            osd->DialogAddButton(QObject::tr("Redo") + " - " +
                                 ctx->player->DeleteMapGetRedoMessage(),
                                 QString("DIALOG_CUTPOINT_REDO_0"));
        if ("EDIT_CUT_POINTS" == type)
            osd->DialogAddButton(QObject::tr("Cut List Options"),
                                 "DIALOG_CUTPOINT_CUTLISTOPTIONS_0", true);
    }
    else if ("CUT_LIST_OPTIONS" == type)
    {
        osd->DialogShow(OSD_DLG_CUTPOINT,
                        QObject::tr("Cut List Options"));
        osd->DialogAddButton(QObject::tr("Clear Cuts"),
                             "DIALOG_CUTPOINT_CLEARMAP_0");
        osd->DialogAddButton(QObject::tr("Reverse Cuts"),
                             "DIALOG_CUTPOINT_INVERTMAP_0");
        osd->DialogAddButton(QObject::tr("Load Detected Commercials"),
                             "DIALOG_CUTPOINT_LOADCOMMSKIP_0");
        osd->DialogAddButton(QObject::tr("Undo Changes"),
                             "DIALOG_CUTPOINT_REVERT_0");
        osd->DialogAddButton(QObject::tr("Exit Without Saving"),
                             "DIALOG_CUTPOINT_REVERTEXIT_0");
        osd->DialogAddButton(QObject::tr("Save Cuts"),
                             "DIALOG_CUTPOINT_SAVEMAP_0");
        osd->DialogAddButton(QObject::tr("Save Cuts and Exit"),
                             "DIALOG_CUTPOINT_SAVEEXIT_0");
    }
    else if ("EXIT_EDIT_MODE" == type)
    {
        osd->DialogShow(OSD_DLG_CUTPOINT,
                        QObject::tr("Exit Recording Editor"));
        osd->DialogAddButton(QObject::tr("Save Cuts and Exit"),
                             "DIALOG_CUTPOINT_SAVEEXIT_0");
        osd->DialogAddButton(QObject::tr("Exit Without Saving"),
                             "DIALOG_CUTPOINT_REVERTEXIT_0");
        osd->DialogAddButton(QObject::tr("Save Cuts"),
                             "DIALOG_CUTPOINT_SAVEMAP_0");
        osd->DialogAddButton(QObject::tr("Undo Changes"),
                             "DIALOG_CUTPOINT_REVERT_0");
    }
    osd->DialogBack("", "DIALOG_CUTPOINT_DONOTHING_0", true);
    QHash<QString,QString> map;
    map.insert("title", tr("Edit"));
    osd->SetText("osd_program_editor", map, kOSDTimeout_None);
    ReturnOSDLock(ctx, osd);
}

bool TV::HandleOSDCutpoint(PlayerContext *ctx, QString action, long long frame)
{
    bool res = true;
    if (!DialogIsVisible(ctx, OSD_DLG_CUTPOINT))
        return res;

    OSD *osd = GetOSDLock(ctx);
    if (action == "CUTLISTOPTIONS" && osd)
    {
        ShowOSDCutpoint(ctx, "CUT_LIST_OPTIONS");
        res = false;
    }
    else if (action == "DONOTHING" && osd)
    {
    }
    else if (osd)
    {
        QStringList actions(action);
        if (!ctx->player->HandleProgramEditorActions(actions, frame))
            LOG(VB_GENERAL, LOG_ERR, LOC + "Unrecognised cutpoint action");
        else
            editmode = ctx->player->GetEditMode();
    }
    ReturnOSDLock(ctx, osd);
    return res;
}

/** \fn TV::StartProgramEditMode(PlayerContext *ctx)
 *  \brief Starts Program Cut Map Editing mode
 */
void TV::StartProgramEditMode(PlayerContext *ctx)
{
    ctx->LockPlayingInfo(__FILE__, __LINE__);
    bool isEditing = ctx->playingInfo->QueryIsEditing();
    ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    if (isEditing)
    {
        ShowOSDAlreadyEditing(ctx);
        return;
    }

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        editmode = ctx->player->EnableEdit();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::ShowOSDAlreadyEditing(PlayerContext *ctx)
{
    OSD *osd = GetOSDLock(ctx);
    if (osd)
    {
        osd->DialogQuit();
        bool was_paused = ContextIsPaused(ctx, __FILE__, __LINE__);
        if (!was_paused)
            DoTogglePause(ctx, true);

        QString message = tr("This program is currently being edited");
        osd->DialogShow(OSD_DLG_EDITING, message);
        QString def = QString("DIALOG_EDITING_CONTINUE_%1").arg(was_paused);
        osd->DialogAddButton(tr("Continue Editing"), def, false, true);
        osd->DialogAddButton(tr("Do not edit"),
                             QString("DIALOG_EDITING_STOP_%1").arg(was_paused));
        osd->DialogBack("", def, true);
    }
    ReturnOSDLock(ctx, osd);
}

void TV::HandleOSDAlreadyEditing(PlayerContext *ctx, QString action,
                                 bool was_paused)
{
    if (!DialogIsVisible(ctx, OSD_DLG_EDITING))
        return;

    bool paused = ContextIsPaused(ctx, __FILE__, __LINE__);

    if (action == "STOP")
    {
        ctx->LockPlayingInfo(__FILE__, __LINE__);
        if (ctx->playingInfo)
            ctx->playingInfo->SaveEditing(false);
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        if (!was_paused && paused)
            DoTogglePause(ctx, true);
    }
    else // action == "CONTINUE"
    {
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        if (ctx->player)
        {
            ctx->playingInfo->SaveEditing(false);
            editmode = ctx->player->EnableEdit();
            if (!editmode && !was_paused && paused)
                DoTogglePause(ctx, false);
        }
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
    }

}

static void insert_map(InfoMap &infoMap, const InfoMap &newMap)
{
    InfoMap::const_iterator it = newMap.begin();
    for (; it != newMap.end(); ++it)
        infoMap.insert(it.key(), *it);
}

/** \fn TV::StartChannelEditMode(PlayerContext*)
 *  \brief Starts channel editing mode.
 */
void TV::StartChannelEditMode(PlayerContext *ctx)
{
    OSD *osd = GetOSDLock(ctx);
    if (!ctx->recorder || !osd)
    {
        ReturnOSDLock(ctx, osd);
        return;
    }
    ReturnOSDLock(ctx, osd);

    QMutexLocker locker(&chanEditMapLock);
    ddMapLoader->wait();

    // Get the info available from the backend
    chanEditMap.clear();
    ctx->recorder->GetChannelInfo(chanEditMap);

    // Assuming the data is valid, try to load DataDirect listings.
    uint sourceid = chanEditMap["sourceid"].toUInt();

    // Update with XDS and DataDirect Info
    ChannelEditAutoFill(ctx, chanEditMap);

    // Set proper initial values for channel editor, and make it visible..
    osd = GetOSDLock(ctx);
    if (osd)
    {
        osd->DialogQuit();
        osd->DialogShow(OSD_DLG_EDITOR);
        osd->SetText(OSD_DLG_EDITOR, chanEditMap, kOSDTimeout_None);
    }
    ReturnOSDLock(ctx, osd);

    if (sourceid && (sourceid != ddMapSourceId))
    {
        ddMapLoader->SetSourceID(sourceid);
        MThreadPool::globalInstance()->start(ddMapLoader, "DDMapLoader");
    }
}

/** \fn TV::ChannelEditKey(const PlayerContext*, const QKeyEvent *e)
 *  \brief Processes channel editing key.
 */
bool TV::HandleOSDChannelEdit(PlayerContext *ctx, QString action)
{
    QMutexLocker locker(&chanEditMapLock);
    bool hide = false;

    if (!DialogIsVisible(ctx, OSD_DLG_EDITOR))
        return hide;

    OSD *osd = GetOSDLock(ctx);
    if (osd && action == "PROBE")
    {
        InfoMap infoMap;
        osd->DialogGetText(infoMap);
        ChannelEditAutoFill(ctx, infoMap);
        insert_map(chanEditMap, infoMap);
        osd->SetText(OSD_DLG_EDITOR, chanEditMap, kOSDTimeout_None);
    }
    else if (osd && action == "OK")
    {
        InfoMap infoMap;
        osd->DialogGetText(infoMap);
        insert_map(chanEditMap, infoMap);
        ctx->recorder->SetChannelInfo(chanEditMap);
        hide = true;
    }
    else if (osd && action == "QUIT")
    {
        hide = true;
    }
    ReturnOSDLock(ctx, osd);
    return hide;
}

/** \fn TV::ChannelEditAutoFill(const PlayerContext*,InfoMap&) const
 *  \brief Automatically fills in as much information as possible.
 */
void TV::ChannelEditAutoFill(const PlayerContext *ctx, InfoMap &infoMap) const
{
    QMap<QString,bool> dummy;
    ChannelEditAutoFill(ctx, infoMap, dummy);
}

/** \fn TV::ChannelEditAutoFill(const PlayerContext*,InfoMap&,const QMap<QString,bool>&) const
 *  \brief Automatically fills in as much information as possible.
 */
void TV::ChannelEditAutoFill(const PlayerContext *ctx, InfoMap &infoMap,
                             const QMap<QString,bool> &changed) const
{
    const QString keys[4] = { "XMLTV", "callsign", "channame", "channum", };

    // fill in uninitialized and unchanged fields from XDS
    ChannelEditXDSFill(ctx, infoMap);

    // if no data direct info we're done..
    if (!ddMapSourceId)
        return;

    if (changed.size())
    {
        ChannelEditDDFill(infoMap, changed, false);
    }
    else
    {
        QMutexLocker locker(&chanEditMapLock);
        QMap<QString,bool> chg;
        // check if anything changed
        for (uint i = 0; i < 4; i++)
            chg[keys[i]] = infoMap[keys[i]] != chanEditMap[keys[i]];

        // clean up case and extra spaces
        infoMap["callsign"] = infoMap["callsign"].toUpper().trimmed();
        infoMap["channum"]  = infoMap["channum"].trimmed();
        infoMap["channame"] = infoMap["channame"].trimmed();
        infoMap["XMLTV"]    = infoMap["XMLTV"].trimmed();

        // make sure changes weren't just chaff
        for (uint i = 0; i < 4; i++)
            chg[keys[i]] &= infoMap[keys[i]] != chanEditMap[keys[i]];

        ChannelEditDDFill(infoMap, chg, true);
    }
}

void TV::ChannelEditXDSFill(const PlayerContext *ctx, InfoMap &infoMap) const
{
    QMap<QString,bool> modifiable;
    if (!(modifiable["callsign"] = infoMap["callsign"].isEmpty()))
    {
        QString unsetsign = QObject::tr("UNKNOWN%1", "Synthesized callsign");
        uint    unsetcmpl = unsetsign.length() - 2;
        unsetsign = unsetsign.left(unsetcmpl);
        if (infoMap["callsign"].left(unsetcmpl) == unsetsign) // was unsetcmpl????
            modifiable["callsign"] = true;
    }
    modifiable["channame"] = infoMap["channame"].isEmpty();

    const QString xds_keys[2] = { "callsign", "channame", };
    for (uint i = 0; i < 2; i++)
    {
        if (!modifiable[xds_keys[i]])
            continue;

        ctx->LockDeletePlayer(__FILE__, __LINE__);
        QString tmp = ctx->player->GetXDS(xds_keys[i]).toUpper();
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);

        if (tmp.isEmpty())
            continue;

        if ((xds_keys[i] == "callsign") &&
            ((tmp.length() > 5) || (tmp.indexOf(" ") >= 0)))
        {
            continue;
        }

        infoMap[xds_keys[i]] = tmp;
    }
}

void TV::ChannelEditDDFill(InfoMap &infoMap,
                           const QMap<QString,bool> &changed,
                           bool check_unchanged) const
{
    if (!ddMapSourceId)
        return;

    QMutexLocker locker(&chanEditMapLock);
    const QString keys[4] = { "XMLTV", "callsign", "channame", "channum", };

    // First check changed keys for availability in our listings source.
    // Then, if check_unchanged is set, check unchanged fields.
    QString key = "", dd_xmltv = "";
    uint endj = (check_unchanged) ? 2 : 1;
    for (uint j = 0; (j < endj) && dd_xmltv.isEmpty(); j++)
    {
        for (uint i = 0; (i < 4) && dd_xmltv.isEmpty(); i++)
        {
            key = keys[i];
            if (((j == 1) ^ changed[key]) && !infoMap[key].isEmpty())
                dd_xmltv = GetDataDirect(key, infoMap[key], "XMLTV");
        }
    }

    // If we found the channel in the listings, fill in all the data we have
    if (!dd_xmltv.isEmpty())
    {
        infoMap[keys[0]] = dd_xmltv;
        for (uint i = 1; i < 4; i++)
        {
            QString tmp = GetDataDirect(key, infoMap[key], keys[i]);
            if (!tmp.isEmpty())
                infoMap[keys[i]] = tmp;
        }
        return;
    }

    // If we failed to find an exact match, try partial matches.
    // But only fill the current field since this data is dodgy.
    key = "callsign";
    if (!infoMap[key].isEmpty())
    {
        dd_xmltv = GetDataDirect(key, infoMap[key], "XMLTV", true);
        LOG(VB_GENERAL, LOG_INFO, QString("xmltv: %1 for key %2")
                .arg(dd_xmltv).arg(key));
        if (!dd_xmltv.isEmpty())
            infoMap[key] = GetDataDirect("XMLTV", dd_xmltv, key);
    }

    key = "channame";
    if (!infoMap[key].isEmpty())
    {
        dd_xmltv = GetDataDirect(key, infoMap[key], "XMLTV", true);
        LOG(VB_GENERAL, LOG_INFO, QString("xmltv: %1 for key %2")
                .arg(dd_xmltv).arg(key));
        if (!dd_xmltv.isEmpty())
            infoMap[key] = GetDataDirect("XMLTV", dd_xmltv, key);
    }
}

QString TV::GetDataDirect(QString key, QString value, QString field,
                          bool allow_partial_match) const
{
    QMutexLocker locker(&chanEditMapLock);

    uint sourceid = chanEditMap["sourceid"].toUInt();
    if (!sourceid)
        return QString::null;

    if (sourceid != ddMapSourceId)
        return QString::null;

    DDKeyMap::const_iterator it_key = ddMap.find(key);
    if (it_key == ddMap.end())
        return QString::null;

    DDValueMap::const_iterator it_val = (*it_key).find(value);
    if (it_val != (*it_key).end())
    {
        InfoMap::const_iterator it_field = (*it_val).find(field);
        if (it_field != (*it_val).end())
        {
            QString ret = *it_field;
            ret.detach();
            return ret;
        }
    }

    if (!allow_partial_match || value.isEmpty())
        return QString::null;

    // Check for partial matches.. prefer early match, then short string
    DDValueMap::const_iterator best_match = (*it_key).end();
    int best_match_idx = INT_MAX, best_match_len = INT_MAX;
    for (it_val = (*it_key).begin(); it_val != (*it_key).end(); ++it_val)
    {
        int match_idx = it_val.key().indexOf(value);
        if (match_idx < 0)
            continue;

        int match_len = it_val.key().length();
        if ((match_idx < best_match_idx) && (match_len < best_match_len))
        {
            best_match     = it_val;
            best_match_idx = match_idx;
            best_match_len = match_len;
        }
    }

    if (best_match != (*it_key).end())
    {
        InfoMap::const_iterator it_field = (*best_match).find(field);
        if (it_field != (*it_val).end())
        {
            QString ret = *it_field;
            ret.detach();
            return ret;
        }
    }

    return QString::null;
}

void TV::RunLoadDDMap(uint sourceid)
{
    QMutexLocker locker(&chanEditMapLock);

    const PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);

    // Load DataDirect info
    LoadDDMap(sourceid);

    // Update with XDS and DataDirect Info
    ChannelEditAutoFill(actx, chanEditMap);

    OSD *osd = GetOSDLock(actx);
    if (osd)
    {
        if (osd->DialogVisible(OSD_DLG_EDITOR))
            osd->SetText(OSD_DLG_EDITOR, chanEditMap, kOSDTimeout_None);
        else
            LOG(VB_GENERAL, LOG_ERR, LOC + "No channel editor visible. Failed "
                                         "to update data direct channel info.");
    }
    ReturnOSDLock(actx, osd);
    ReturnPlayerLock(actx);
}

bool TV::LoadDDMap(uint sourceid)
{
    QMutexLocker locker(&chanEditMapLock);
    const QString keys[4] = { "XMLTV", "callsign", "channame", "channum", };

    ddMap.clear();
    ddMapSourceId = 0;

    QString grabber, userid, passwd, lineupid;
    bool ok = SourceUtil::GetListingsLoginData(sourceid, grabber, userid,
                                               passwd, lineupid);
    if (!ok || (grabber != "datadirect"))
    {
        LOG(VB_PLAYBACK, LOG_ERR, LOC +
            QString("LoadDDMap() g(%1)").arg(grabber));
        return false;
    }

    DataDirectProcessor ddp(DD_ZAP2IT, userid, passwd);
    ddp.GrabFullLineup(lineupid, true, false, 36*60*60);
    const DDLineupChannels channels = ddp.GetDDLineup(lineupid);

    InfoMap tmp;
    DDLineupChannels::const_iterator it;
    for (it = channels.begin(); it != channels.end(); ++it)
    {
        DDStation station = ddp.GetDDStation((*it).stationid);
        tmp["XMLTV"]    = (*it).stationid;
        tmp["callsign"] = station.callsign;
        tmp["channame"] = station.stationname;
        tmp["channum"]  = (*it).channel;
        if (!(*it).channelMinor.isEmpty())
        {
            tmp["channum"] += SourceUtil::GetChannelSeparator(sourceid);
            tmp["channum"] += (*it).channelMinor;
        }

#if 0
        LOG(VB_CHANNEL, LOG_INFO,
            QString("Adding channel: %1 -- %2 -- %3 -- %4")
                .arg(tmp["channum"],4).arg(tmp["callsign"],7)
                .arg(tmp["XMLTV"]).arg(tmp["channame"]));
#endif

        for (uint j = 0; j < 4; j++)
            for (uint i = 0; i < 4; i++)
                ddMap[keys[j]][tmp[keys[j]]][keys[i]] = tmp[keys[i]];
    }

    if (!ddMap.empty())
        ddMapSourceId = sourceid;

    return !ddMap.empty();
}

static struct ActionDefStruct<TV> toActions[] = {
    { "^DIALOG_",                    &TV::doOSDDialog },
    { ACTION_PAUSE,                  &TV::doOSDPause },
    { ACTION_STOP,                   &TV::doOSDStop },
    { ACTION_JUMPFFWD,               &TV::doOSDFfwd },
    { ACTION_JUMPRWND,               &TV::doOSDRwnd },
    { "^DEINTERLACER",               &TV::doOSDDeinterlace },
    { ACTION_TOGGLEOSDDEBUG,         &TV::doOSDToggleDebug },
    { "TOGGLEMANUALZOOM",            &TV::doOSDToggleManualZoom },
    { "TOGGLESTRETCH",               &TV::doOSDToggleStretch },
    { ACTION_ENABLEUPMIX,            &TV::doOSDEnableUpmix },
    { ACTION_DISABLEUPMIX,           &TV::doOSDDisableUpmix },
    { "^ADJUSTSTRETCH",              &TV::doOSDAdjustStretch },
    { "^SELECTSCAN_",                &TV::doOSDSelectScan },
    { "^" ACTION_TOGGELAUDIOSYNC,    &TV::doOSDToggleAudioSync },
    { ACTION_TOGGLEVISUALISATION,    &TV::doOSDToggleVis },
    { ACTION_ENABLEVISUALISATION,    &TV::doOSDEnableVis },
    { ACTION_DISABLEVISUALISATION,   &TV::doOSDDisableVis },
    { "^" + ACTION_TOGGLESLEEP,      &TV::doOSDToggleSleep },
    { "^TOGGLEPICCONTROLS",          &TV::doOSDTogglePicControls },
    { "^" ACTION_TOGGLESTUDIOLEVELS, &TV::doOSDToggleStudioLevels },
    { ACTION_TOGGLENIGHTMODE,        &TV::doOSDToggleNightMode },
    { "^TOGGLEASPECT",               &TV::doOSDToggleAspect },
    { "^TOGGLEFILL",                 &TV::doOSDToggleFill },
    { "AUTODETECT_FILL",             &TV::doOSDAutodetectFill },
    { ACTION_GUIDE,                  &TV::doOSDGuide },
    { "^CHANGROUP_",                 &TV::doOSDChangroup },
    { ACTION_FINDER,                 &TV::doOSDFinder },
    { "SCHEDULE",                    &TV::doOSDSchedule },
    { ACTION_VIEWSCHEDULED,          &TV::doOSDViewScheduled },
    { "^VISUALISER",                 &TV::doOSDVisualiser },
    { "^3D",                         &TV::doOSD3d }
};
static int toActionCount = NELEMS(toActions);

static struct ActionDefStruct<TV> todActions[] = {
    { "MENU",         &TV::doOSDDialogMenu },
    { ACTION_JUMPREC, &TV::doOSDDialogRecord },
    { "VIDEOEXIT",    &TV::doOSDDialogExit },
    { "SLEEP",        &TV::doOSDDialogSleep },
    { "IDLE",         &TV::doOSDDialogIdle },
    { "INFO",         &TV::doOSDDialogInfo },
    { "EDITING",      &TV::doOSDDialogEditing },
    { "ASKALLOW",     &TV::doOSDDialogAskAllow },
    { "EDITOR",       &TV::doOSDDialogEditor },
    { "CUTPOINT",     &TV::doOSDDialogCutpoint },
    { "DELETE",       &TV::doOSDDialogDelete },
    { ACTION_PLAY,    &TV::doOSDDialogPlay },
    { "CONFIRM",      &TV::doOSDDialogConfirm }
};
static int todActionCount = NELEMS(todActions);

static struct ActionDefStruct<TV> toltActions[] = {
    { "TOGGLEBROWSE",    &TV::doOSDLiveTVToggleBrowse },
    { "PREVCHAN",        &TV::doOSDLiveTVPrevChan },
    { "^SWITCHTOINPUT_", &TV::doOSDLiveTVSwitchInput },
    { "EDIT",            &TV::doOSDLiveTVEdit }
};
static int toltActionCount = NELEMS(toltActions);

static struct ActionDefStruct<TV> topActions[] = {
    { ACTION_JUMPTODVDROOTMENU,    &TV::doOSDPlayingDVDMenu },
    { ACTION_JUMPTODVDCHAPTERMENU, &TV::doOSDPlayingDVDMenu },
    { ACTION_JUMPTOPOPUPMENU,      &TV::doOSDPlayingDVDMenu },
    { ACTION_JUMPTODVDTITLEMENU,   &TV::doOSDPlayingDVDMenu },
    { "^" ACTION_JUMPCHAPTER,      &TV::doOSDPlayingJumpChapter },
    { "^" ACTION_SWITCHTITLE,      &TV::doOSDPlayingSwitchTitle },
    { "^" ACTION_SWITCHANGLE,      &TV::doOSDPlayingSwitchAngle },
    { "EDIT",                      &TV::doOSDPlayingEdit },
    { "TOGGLEAUTOEXPIRE",          &TV::doOSDPlayingToggleAutoexpire },
    { "^TOGGLECOMMSKIP",           &TV::doOSDPlayingToggleCommSkip },
    { "QUEUETRANSCODE",            &TV::doOSDPlayingTranscode },
    { "QUEUETRANSCODE_AUTO",       &TV::doOSDPlayingTranscodeAuto },
    { "QUEUETRANSCODE_HIGH",       &TV::doOSDPlayingTranscodeHigh },
    { "QUEUETRANSCODE_MEDIUM",     &TV::doOSDPlayingTranscodeMed },
    { "QUEUETRANSCODE_LOW",        &TV::doOSDPlayingTranscodeLow }
};
static int topActionCount = NELEMS(topActions);

bool TV::doOSDDialog(const QString &action)
{
    if (!m_osdDialogActions)
        m_osdDialogActions = new MythActions<TV>(this, todActions,
                                                 todActionCount);
    QString newAction(action);
    newAction.remove("DIALOG_");

    m_actionOSDArgs = newAction.split("_");
    if (m_actionOSDArgs.size() != 3)
        return true;

    bool touched;
    bool handled = m_osdDialogActions->handleAction(m_actionOSDArgs[0],
                                                    touched);
    if (!handled)
        LOG(VB_GENERAL, LOG_ERR, "Unrecognised dialog event.");

    return true;
}

bool TV::doOSDPause(const QString &action)
{
    DoTogglePause(m_actionOSDContext, true);
    return true;
}

bool TV::doOSDStop(const QString &action)
{
    PrepareToExitPlayer(m_actionOSDContext, __LINE__, false);
    SetExitPlayer(true, true);
    return true;
}

bool TV::doOSDFfwd(const QString &action)
{
    DoJumpFFWD(m_actionOSDContext);
    return true;
}

bool TV::doOSDRwnd(const QString &action)
{
    DoJumpRWND(m_actionOSDContext);
    return true;
}

bool TV::doOSDDeinterlace(const QString &action)
{
    HandleDeinterlacer(m_actionOSDContext, action);
    return true;
}

bool TV::doOSDToggleDebug(const QString &action)
{
    ToggleOSDDebug(m_actionOSDContext);
    return true;
}

bool TV::doOSDToggleManualZoom(const QString &action)
{
    SetManualZoom(m_actionOSDContext, true, tr("Zoom Mode ON"));
    return true;
}

bool TV::doOSDToggleStretch(const QString &action)
{
    ToggleTimeStretch(m_actionOSDContext);
    return true;
}

bool TV::doOSDEnableUpmix(const QString &action)
{
    EnableUpmix(m_actionOSDContext, true);
    return true;
}

bool TV::doOSDDisableUpmix(const QString &action)
{
    EnableUpmix(m_actionOSDContext, false);
    return true;
}

bool TV::doOSDAdjustStretch(const QString &action)
{
    QString newAction(action);
    newAction.remove("ADJUSTSTRETCH");
    bool floatRead;
    float stretch = newAction.toFloat(&floatRead);
    if (floatRead && stretch <= 2.0 && stretch >= 0.48)
    {
        m_actionOSDContext->ts_normal = stretch; // alter speed before display
    }

    StopFFRew(m_actionOSDContext);

    if (ContextIsPaused(m_actionOSDContext, __FILE__, __LINE__))
        DoTogglePause(m_actionOSDContext, true);

    ChangeTimeStretch(m_actionOSDContext, 0, !floatRead);   // just display
    return true;
}

bool TV::doOSDSelectScan(const QString &action)
{
    QString msg = QString("");
    m_actionOSDContext->LockDeletePlayer(__FILE__, __LINE__);
    MythPlayer *play = m_actionOSDContext->player;
    play->SetScanType((FrameScanType) action.right(1).toInt());
    m_actionOSDContext->UnlockDeletePlayer(__FILE__, __LINE__);
    msg = toString(play->GetScanType());

    if (!msg.isEmpty())
        SetOSDMessage(m_actionOSDContext, msg);
    return true;
}

bool TV::doOSDToggleAudioSync(const QString &action)
{
    ChangeAudioSync(m_actionOSDContext, 0);
    return true;
}

bool TV::doOSDToggleVis(const QString &action)
{
    EnableVisualisation(m_actionOSDContext, false, true);
    return true;
}

bool TV::doOSDEnableVis(const QString &action)
{
    EnableVisualisation(m_actionOSDContext, true);
    return true;
}

bool TV::doOSDDisableVis(const QString &action)
{
    EnableVisualisation(m_actionOSDContext, false);
    return true;
}

bool TV::doOSDToggleSleep(const QString &action)
{
    ToggleSleepTimer(m_actionOSDContext, action.left(13));
    return true;
}

bool TV::doOSDTogglePicControls(const QString &action)
{
    adjustingPictureAttribute = (PictureAttribute)(action.right(1).toInt() - 1);
    DoTogglePictureAttribute(m_actionOSDContext, kAdjustingPicture_Playback);
    return true;
}

bool TV::doOSDToggleStudioLevels(const QString &action)
{
    DoToggleStudioLevels(m_actionOSDContext);
    return true;
}

bool TV::doOSDToggleNightMode(const QString &action)
{
    DoToggleNightMode(m_actionOSDContext);
    return true;
}

bool TV::doOSDToggleAspect(const QString &action)
{
    ToggleAspectOverride(m_actionOSDContext,
                         (AspectOverrideMode)action.right(1).toInt());
    return true;
}

bool TV::doOSDToggleFill(const QString &action)
{
    ToggleAdjustFill(m_actionOSDContext,
                     (AdjustFillMode)action.right(1).toInt());
    return true;
}

bool TV::doOSDAutodetectFill(const QString &action)
{
    MythPlayer *play = m_actionOSDContext->player;
    play->detect_letter_box->SetDetectLetterbox(
                               !play->detect_letter_box->GetDetectLetterbox());
    return true;
}

bool TV::doOSDGuide(const QString &action)
{
    EditSchedule(m_actionOSDContext, kScheduleProgramGuide);
    return true;
}

bool TV::doOSDChangroup(const QString &action)
{
    if (!db_use_channel_groups)
        return false;

    QString newAction(action);
    newAction.remove("CHANGROUP_");

    if (newAction == "ALL_CHANNELS")
    {
        UpdateChannelList(-1);
        return true;
    }

    UpdateChannelList(newAction.toInt());

    // make sure the current channel is from the selected group
    // or tune to the first in the group
    QString cur_channum, new_channum;
    if (m_actionOSDContext->tvchain)
    {
        QMutexLocker locker(&channelGroupLock);
        const DBChanList &list = channelGroupChannelList;
        cur_channum = m_actionOSDContext->tvchain->GetChannelName(-1);
        new_channum = cur_channum;

        DBChanList::const_iterator it = list.begin();
        for (; it != list.end(); ++it)
        {
            if ((*it).channum == cur_channum)
            {
                break;
            }
        }

        if (it == list.end())
        {
            // current channel not found so switch to the
            // first channel in the group
            it = list.begin();
            if (it != list.end())
                new_channum = (*it).channum;
        }

        LOG(VB_CHANNEL, LOG_INFO, LOC + QString("Channel Group: '%1'->'%2'")
            .arg(cur_channum).arg(new_channum));
    }

    if (m_actionOSDContext->tvchain)
    {
        // Only change channel if new channel != current channel
        if (cur_channum != new_channum && !new_channum.isEmpty())
        {
            QMutexLocker locker(&timerIdLock);
            queuedInput   = new_channum;
            queuedInput.detach();
            queuedChanNum = new_channum;
            queuedChanNum.detach();
            queuedChanID  = 0;
            if (!queueInputTimerId)
                queueInputTimerId = StartTimer(10, __LINE__);
        }

        // Turn off OSD Channel Num so the channel
        // changes right away
        HideOSDWindow(m_actionOSDContext, "osd_input");
    }
    return true;
}

bool TV::doOSDFinder(const QString &action)
{
    EditSchedule(m_actionOSDContext, kScheduleProgramFinder);
    return true;
}

bool TV::doOSDSchedule(const QString &action)
{
    EditSchedule(m_actionOSDContext, kScheduledRecording);
    return true;
}

bool TV::doOSDViewScheduled(const QString &action)
{
    EditSchedule(m_actionOSDContext, kViewSchedule);
    return true;
}

bool TV::doOSDVisualiser(const QString &action)
{
    EnableVisualisation(m_actionOSDContext, true, false, action);
    return true;
}

bool TV::doOSD3d(const QString &action)
{
    Handle3D(m_actionOSDContext, action);
    return true;
}


bool TV::doOSDDialogMenu(const QString &action)
{
    ShowOSDMenu(m_actionOSDContext, m_actionOSDArgs[1], m_actionOSDText);
    m_actionOSDHide = false;
    return true;
}

bool TV::doOSDDialogRecord(const QString &action)
{
    FillOSDMenuJumpRec(m_actionOSDContext, m_actionOSDArgs[1],
                       m_actionOSDArgs[2].toInt(), m_actionOSDText);
    m_actionOSDHide = false;
    return true;
}

bool TV::doOSDDialogExit(const QString &action)
{
    m_actionOSDHide = HandleOSDVideoExit(m_actionOSDContext,
                                         m_actionOSDArgs[1]);
    return true;
}

bool TV::doOSDDialogSleep(const QString &action)
{
    HandleOSDSleep(m_actionOSDContext, m_actionOSDArgs[1]);
    return true;
}

bool TV::doOSDDialogIdle(const QString &action)
{
    HandleOSDIdle(m_actionOSDContext, m_actionOSDArgs[1]);
    return true;
}

bool TV::doOSDDialogInfo(const QString &action)
{
    HandleOSDInfo(m_actionOSDContext, m_actionOSDArgs[1]);
    return true;
}

bool TV::doOSDDialogEditing(const QString &action)
{
    HandleOSDAlreadyEditing(m_actionOSDContext, m_actionOSDArgs[1],
                            m_actionOSDArgs[2].toInt());
    return true;
}

bool TV::doOSDDialogAskAllow(const QString &action)
{
    HandleOSDAskAllow(m_actionOSDContext, m_actionOSDArgs[1]);
    return true;
}

bool TV::doOSDDialogEditor(const QString &action)
{
    m_actionOSDHide = HandleOSDChannelEdit(m_actionOSDContext,
                                           m_actionOSDArgs[1]);
    return true;
}

bool TV::doOSDDialogCutpoint(const QString &action)
{
    m_actionOSDHide = HandleOSDCutpoint(m_actionOSDContext, m_actionOSDArgs[1],
                                        m_actionOSDArgs[2].toLongLong());
    return true;
}

bool TV::doOSDDialogDelete(const QString &action)
{
    return true;
}

bool TV::doOSDDialogPlay(const QString &action)
{
    DoPlay(m_actionOSDContext);
    return true;
}

bool TV::doOSDDialogConfirm(const QString &action)
{
    return true;
}


bool TV::doOSDLiveTVToggleBrowse(const QString &action)
{
    browsehelper->BrowseStart(m_actionOSDContext);
    return true;
}

bool TV::doOSDLiveTVPrevChan(const QString &action)
{
    PopPreviousChannel(m_actionOSDContext, true);
    return true;
}

bool TV::doOSDLiveTVSwitchInput(const QString &action)
{
    QString newAction(action);
    newAction.remove("SWITCHTOINPUT_");
    switchToInputId = newAction.toUInt();

    QMutexLocker locker(&timerIdLock);
    if (!switchToInputTimerId)
        switchToInputTimerId = StartTimer(1, __LINE__);

    return true;
}

bool TV::doOSDLiveTVEdit(const QString &action)
{
    StartChannelEditMode(m_actionOSDContext);
    m_actionOSDHide = false;
    return true;
}


bool TV::doOSDPlayingDVDMenu(const QString &action)
{
    QString menu = "root";
    if (action == ACTION_JUMPTODVDCHAPTERMENU)
        menu = "chapter";
    else if (action == ACTION_JUMPTODVDTITLEMENU)
        menu = "title";
    else if (action == ACTION_JUMPTOPOPUPMENU)
        menu = "popup";
    m_actionOSDContext->LockDeletePlayer(__FILE__, __LINE__);
    if (m_actionOSDContext->player)
        m_actionOSDContext->player->GoToMenu(menu);
    m_actionOSDContext->UnlockDeletePlayer(__FILE__, __LINE__);
    return true;
}

bool TV::doOSDPlayingJumpChapter(const QString &action)
{
    int chapter = action.right(3).toInt();
    DoJumpChapter(m_actionOSDContext, chapter);
    return true;
}

bool TV::doOSDPlayingSwitchTitle(const QString &action)
{
    int title = action.right(3).toInt();
    DoSwitchTitle(m_actionOSDContext, title);
    return true;
}

bool TV::doOSDPlayingSwitchAngle(const QString &action)
{
    int angle = action.right(3).toInt();
    DoSwitchAngle(m_actionOSDContext, angle);
    return true;
}

bool TV::doOSDPlayingEdit(const QString &action)
{
    StartProgramEditMode(m_actionOSDContext);
    m_actionOSDHide = false;
    return true;
}

bool TV::doOSDPlayingToggleAutoexpire(const QString &action)
{
    ToggleAutoExpire(m_actionOSDContext);
    return true;
}

bool TV::doOSDPlayingToggleCommSkip(const QString &action)
{
    SetAutoCommercialSkip(m_actionOSDContext,
                          (CommSkipMode)(action.right(1).toInt()));
    return true;
}

bool TV::doOSDPlayingTranscode(const QString &action)
{
    DoQueueTranscode(m_actionOSDContext, "Default");
    return true;
}

bool TV::doOSDPlayingTranscodeAuto(const QString &action)
{
    DoQueueTranscode(m_actionOSDContext, "Autodetect");
    return true;
}

bool TV::doOSDPlayingTranscodeHigh(const QString &action)
{
    DoQueueTranscode(m_actionOSDContext, "High Quality");
    return true;
}

bool TV::doOSDPlayingTranscodeMed(const QString &action)
{
    DoQueueTranscode(m_actionOSDContext, "Medium Quality");
    return true;
}

bool TV::doOSDPlayingTranscodeLow(const QString &action)
{
    DoQueueTranscode(m_actionOSDContext, "Low Quality");
    return true;
}


void TV::OSDDialogEvent(int result, QString text, QString action)
{
    PlayerContext *actx = GetPlayerReadLock(-1, __FILE__, __LINE__);

    LOG(VB_GENERAL, LOG_DEBUG, LOC +
        QString("OSDDialogEvent: result %1 text %2 action %3")
            .arg(result).arg(text).arg(action));

    bool touched;

    if (result < 0)     // exit dialog
    {
        if (!m_osdExitActions)
            m_osdExitActions = new MythActions<TV>(this, toActions, 1);
        m_osdExitActions->handleAction(action, touched);
        return;
    }

    m_actionOSDHide = true;
    m_actionOSDContext = actx;
    m_actionOSDText = text;

    if (!m_osdActions)
        m_osdActions = new MythActions<TV>(this, toActions, toActionCount);
    bool handled = m_osdActions->handleAction(action, touched);

    QStringList actions(action);

    if (!handled)
        handled = HandleTrackAction(actx, actions);

    if (!handled)
        handled = HandleJumpToProgramAction(actx, actions);

    if (!handled)
    {
        handled = PxPHandleAction(actx, actions);
        if (handled)
        {
            for (uint i = 0; i < player.size(); i++)
                ClearOSD(GetPlayer(actx,i));
            // "NEXTPIPWINDOW" changes active context..
            actx = GetPlayer(actx, -1);
        }
    }

    if (!handled && StateIsLiveTV(GetState(actx)))
    {
        if (!m_osdLiveTVActions)
            m_osdLiveTVActions = new MythActions<TV>(this, toltActions,
                                                     toltActionCount);
        handled = m_osdLiveTVActions->handleAction(action, touched);
        
        if (!handled)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "Unknown menu action selected: " +
                                     action);
            m_actionOSDHide = false;
        }
    }

    if (!handled && StateIsPlaying(actx->GetState()))
    {
        if (!m_osdPlayingActions)
            m_osdPlayingActions = new MythActions<TV>(this, topActions,
                                                     topActionCount);
        handled = m_osdPlayingActions->handleAction(action, touched);

        if (!handled)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "Unknown menu action selected: " +
                                     action);
            m_actionOSDHide = false;
        }
    }

    if (m_actionOSDHide)
    {
        OSD *osd = GetOSDLock(actx);
        if (osd)
            osd->DialogQuit();
        ReturnOSDLock(actx, osd);
    }

    ReturnPlayerLock(actx);
}

bool TV::DialogIsVisible(PlayerContext *ctx, const QString &dialog)
{
    bool visible = false;
    OSD *osd = GetOSDLock(ctx);
    if (osd)
        visible = osd->DialogVisible(dialog);
    ReturnOSDLock(ctx, osd);
    return visible;
}

void TV::HandleOSDInfo(PlayerContext *ctx, QString action)
{
    if (!DialogIsVisible(ctx, OSD_DLG_INFO))
        return;

    if (action == "CHANNELLOCK")
    {
        lockTimerOn = false;
    }
}

void TV::ShowOSDMenu(const PlayerContext *ctx, const QString category,
                     const QString selected)
{
    QString cat = category.isEmpty() ? "MAIN" : category;

    OSD *osd = GetOSDLock(ctx);
    if (osd)
    {
        osd->DialogShow(OSD_DLG_MENU, tr("Playback Menu"));
        QString currenttext = QString();
        QString back = QString();

        FillOSDMenuAudio    (ctx, osd, cat, selected, currenttext, back);
        FillOSDMenuVideo    (ctx, osd, cat, selected, currenttext, back);
        FillOSDMenuSubtitles(ctx, osd, cat, selected, currenttext, back);
        FillOSDMenuPlayback (ctx, osd, cat, selected, currenttext, back);
        FillOSDMenuNavigate (ctx, osd, cat, selected, currenttext, back);
        FillOSDMenuSchedule (ctx, osd, cat, selected, currenttext, back);
        FillOSDMenuSource   (ctx, osd, cat, selected, currenttext, back);
        FillOSDMenuJobs     (ctx, osd, cat, selected, currenttext, back);

        if (!currenttext.isEmpty())
            osd->DialogSetText(currenttext);
        if (!back.isEmpty() && !category.isEmpty())
            osd->DialogBack(cat, QString("DIALOG_MENU_%1_0").arg(back));
    }
    ReturnOSDLock(ctx, osd);
}

void TV::FillOSDMenuAudio(const PlayerContext *ctx, OSD *osd,
                          QString category, const QString selected,
                          QString &currenttext, QString &backaction)
{
    QStringList tracks;
    uint curtrack = ~0;
    bool avsync = true;
    bool visual = false;
    QString active = QString("");
    bool upmixing = false;
    bool canupmix = false;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        visual = ctx->player->CanVisualise();
        active = ctx->player->GetVisualiserName();
        tracks = ctx->player->GetTracks(kTrackTypeAudio);
        if (!tracks.empty())
            curtrack = (uint) ctx->player->GetTrack(kTrackTypeAudio);
        avsync = (ctx->player->GetTrackCount(kTrackTypeVideo) > 0) &&
                  !tracks.empty();
        upmixing = ctx->player->GetAudio()->IsUpmixing();
        canupmix = ctx->player->GetAudio()->CanUpmix();
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (tracks.empty()) // No audio
        return;

    if (category == "MAIN")
    {
        osd->DialogAddButton(tr("Audio"), "DIALOG_MENU_AUDIO_0",
                             true, selected == "AUDIO");
    }
    else if (category == "AUDIO")
    {
        // TODO Add mute and volume
        backaction = "MAIN";
        currenttext = tr("Audio");
        if (tracks.size() > 1)
        {
            osd->DialogAddButton(tr("Select Audio Track"),
                                 "DIALOG_MENU_AUDIOTRACKS_0", true,
                                 selected == "AUDIOTRACKS");
        }
        if (avsync)
            osd->DialogAddButton(tr("Adjust Audio Sync"), ACTION_TOGGELAUDIOSYNC);
        if (visual)
        {
            osd->DialogAddButton(tr("Visualisation"),
                                 "DIALOG_MENU_VISUALISATIONS_0", true,
                                 selected == "VISUALISATIONS");
        }

        if (canupmix)
        {
            if (upmixing)
            {
                osd->DialogAddButton(tr("Disable Audio Upmixer"),
                                     ACTION_DISABLEUPMIX);
            }
            else
            {
                osd->DialogAddButton(tr("Enable Audio Upmixer"),
                                     ACTION_ENABLEUPMIX);
            }
        }

    }
    else if (category == "AUDIOTRACKS")
    {
        backaction  = "AUDIO";
        currenttext = tr("Select Audio Track");
        for (uint i = 0; i < (uint)tracks.size(); i++)
        {
            osd->DialogAddButton(tracks[i],
                                 "SELECTAUDIO_" + QString::number(i),
                                 false, i == curtrack);
        }
    }
    else if (category == "VISUALISATIONS")
    {
        backaction = "AUDIO";
        currenttext = tr("Visualisation");
        osd->DialogAddButton(tr("None"),
                             ACTION_DISABLEVISUALISATION, false,
                             active.isEmpty());
        QStringList visualisers;
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        if (ctx->player)
            visualisers = ctx->player->GetVisualiserList();
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        for (int i = 0; i < visualisers.size(); i++)
        {
            osd->DialogAddButton(visualisers[i],
                "VISUALISER_" + visualisers[i], false,
                active == visualisers[i]);
        }
    }
}

void TV::FillOSDMenuVideo(const PlayerContext *ctx, OSD *osd,
                          QString category, const QString selected,
                          QString &currenttext, QString &backaction)
{
    QStringList tracks;
    //uint curtrack                     = ~0;
    uint sup                          = kPictureAttributeSupported_None;
    bool studio_levels                = false;
    bool autodetect                   = false;
    AdjustFillMode adjustfill         = kAdjustFill_Off;
    AspectOverrideMode aspectoverride = kAspect_Off;
    FrameScanType scan_type           = kScan_Ignore;
    bool scan_type_locked             = false;
    bool stereoallowed                = false;
    StereoscopicMode stereomode       = kStereoscopicModeNone;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        tracks           = ctx->player->GetTracks(kTrackTypeVideo);
        aspectoverride   = ctx->player->GetAspectOverride();
        adjustfill       = ctx->player->GetAdjustFill();
        scan_type        = ctx->player->GetScanType();
        scan_type_locked = ctx->player->IsScanTypeLocked();
        //if (!tracks.empty())
        //    curtrack = (uint) ctx->player->GetTrack(kTrackTypeVideo);
        VideoOutput *vo = ctx->player->GetVideoOutput();
        if (vo)
        {
            sup = vo->GetSupportedPictureAttributes();
            studio_levels = vo->GetPictureAttribute(kPictureAttribute_StudioLevels) > 0;
            autodetect = !vo->hasHWAcceleration();
            stereoallowed = vo->StereoscopicModesAllowed();
            stereomode = vo->GetStereoscopicMode();
        }
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (tracks.empty()) // No video
        return;

    if (category == "MAIN")
    {
        osd->DialogAddButton(tr("Video"), "DIALOG_MENU_VIDEO_0",
                             true, selected == "VIDEO");
    }
    else if (category == "VIDEO")
    {
        // TODO Add deinterlacer, filters, video track
        backaction = "MAIN";
        currenttext = tr("Video");
        if (ctx == GetPlayer(ctx, 0))
        {
            osd->DialogAddButton(tr("Change Aspect Ratio"),
                                 "DIALOG_MENU_VIDEOASPECT_0", true,
                                 selected == "VIDEOASPECT");
            osd->DialogAddButton(tr("Adjust Fill"),
                                 "DIALOG_MENU_ADJUSTFILL_0", true,
                                 selected == "ADJUSTFILL");
            osd->DialogAddButton(tr("Manual Zoom Mode"), "TOGGLEMANUALZOOM");
            if (sup != kPictureAttributeSupported_None)
            {
                osd->DialogAddButton(tr("Adjust Picture"),
                                     "DIALOG_MENU_ADJUSTPICTURE_0", true,
                                     selected == "ADJUSTPICTURE");
            }
        }
        if (stereoallowed)
        {
            osd->DialogAddButton(tr("3D"), "DIALOG_MENU_3D_0",
                                 true, selected == "3D");
        }
        osd->DialogAddButton(tr("Advanced"), "DIALOG_MENU_ADVANCEDVIDEO_0",
                             true, selected == "ADVANCEDVIDEO");
    }
    else if (category == "VIDEOASPECT")
    {
        backaction = "VIDEO";
        currenttext = tr("Change Aspect Ratio");

        for (int j = kAspect_Off; j < kAspect_END; j++)
        {
            // swap 14:9 and 16:9
            int i = ((kAspect_14_9 == j) ? kAspect_16_9 :
                     ((kAspect_16_9 == j) ? kAspect_14_9 : j));
            osd->DialogAddButton(toString((AspectOverrideMode) i),
                                 QString("TOGGLEASPECT%1").arg(i), false,
                                 aspectoverride == i);
        }
    }
    else if (category == "ADJUSTFILL")
    {
        backaction = "VIDEO";
        currenttext = tr("Adjust Fill");

        if (autodetect)
        {
            osd->DialogAddButton(tr("Auto Detect"), "AUTODETECT_FILL",
                 false, (adjustfill == kAdjustFill_AutoDetect_DefaultHalf) ||
                        (adjustfill == kAdjustFill_AutoDetect_DefaultOff));
        }
        for (int i = kAdjustFill_Off; i < kAdjustFill_END; i++)
        {
            osd->DialogAddButton(toString((AdjustFillMode) i),
                                 QString("TOGGLEFILL%1").arg(i), false,
                                 adjustfill == i);
        }
    }
    else if (category == "ADJUSTPICTURE")
    {
        backaction = "VIDEO";
        currenttext = tr("Adjust Picture");
        for (int i = kPictureAttribute_MIN; i < kPictureAttribute_MAX; i++)
        {
            if (toMask((PictureAttribute)i) & sup)
            {
                if ((PictureAttribute)i == kPictureAttribute_StudioLevels)
                {
                    QString msg = studio_levels ? tr("Disable studio levels") :
                                                  tr("Enable studio levels");
                    osd->DialogAddButton(msg, ACTION_TOGGLESTUDIOLEVELS);
                }
                else
                {
                    osd->DialogAddButton(toString((PictureAttribute) i),
                                   QString("TOGGLEPICCONTROLS%1").arg(i));
                }
            }
        }
        osd->DialogAddButton(
            gCoreContext->GetNumSetting("NightModeEnabled", 0) ?
            tr("Disable Night Mode") : tr("Enable Night Mode"),
            ACTION_TOGGLENIGHTMODE);
    }
    else if (category == "3D")
    {
        backaction = "VIDEO";
        currenttext = tr("3D");
        osd->DialogAddButton(tr("None"),
                             ACTION_3DNONE, false,
                             stereomode == kStereoscopicModeNone);
        osd->DialogAddButton(tr("Side by Side"),
                             ACTION_3DSIDEBYSIDE, false,
                             stereomode == kStereoscopicModeSideBySide);
        osd->DialogAddButton(tr("Discard Side by Side"),
                             ACTION_3DSIDEBYSIDEDISCARD, false,
                             stereomode == kStereoscopicModeSideBySideDiscard);
        osd->DialogAddButton(tr("Top and Bottom"),
                             ACTION_3DTOPANDBOTTOM, false,
                             stereomode == kStereoscopicModeTopAndBottom);
        osd->DialogAddButton(tr("Discard Top and Bottom"),
                             ACTION_3DTOPANDBOTTOMDISCARD, false,
                             stereomode == kStereoscopicModeTopAndBottomDiscard);
    }
    else if (category == "ADVANCEDVIDEO")
    {
        osd->DialogAddButton(tr("Video Scan"),
                             "DIALOG_MENU_VIDEOSCAN_0", true,
                             selected == "VIDEOSCAN");
        if (kScan_Progressive != scan_type)
        {
            osd->DialogAddButton(tr("Deinterlacer"),
                                 "DIALOG_MENU_DEINTERLACER_0", true,
                                 selected == "DEINTERLACER");
        }
        backaction = "VIDEO";
        currenttext = tr("Advanced");
    }
    else if (category == "DEINTERLACER")
    {
        backaction = "ADVANCEDVIDEO";
        currenttext = tr("Deinterlacer");

        QStringList deinterlacers;
        QString     currentdeinterlacer;
        bool        doublerate = false;
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        if (ctx->player && ctx->player->GetVideoOutput())
        {
            ctx->player->GetVideoOutput()->GetDeinterlacers(deinterlacers);
            currentdeinterlacer = ctx->player->GetVideoOutput()->GetDeinterlacer();
            doublerate = ctx->player->CanSupportDoubleRate();
        }
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);

        foreach (QString deint, deinterlacers)
        {
            if ((deint.contains("doublerate") ||
                deint.contains("doubleprocess") ||
                deint.contains("bobdeint")) && !doublerate)
            {
                continue;
            }
            QString trans = VideoDisplayProfile::GetDeinterlacerName(deint);
            osd->DialogAddButton(trans, "DEINTERLACER_" + deint, false,
                                 deint == currentdeinterlacer);
        }
    }
    else if (category == "VIDEOSCAN")
    {
        backaction = "ADVANCEDVIDEO";
        currenttext = tr("Video Scan");

        QString cur_mode = "";
        if (!scan_type_locked)
        {
            if (kScan_Interlaced == scan_type)
                cur_mode = tr("(I)", "Interlaced (Normal)");
            else if (kScan_Intr2ndField == scan_type)
                cur_mode = tr("(i)", "Interlaced (Reversed)");
            else if (kScan_Progressive == scan_type)
                cur_mode = tr("(P)", "Progressive");
            cur_mode = " " + cur_mode;
            scan_type = kScan_Detect;
        }

        osd->DialogAddButton(tr("Detect") + cur_mode, "SELECTSCAN_0", false,
                             scan_type == kScan_Detect);
        osd->DialogAddButton(tr("Progressive"), "SELECTSCAN_3", false,
                             scan_type == kScan_Progressive);
        osd->DialogAddButton(tr("Interlaced (Normal)"), "SELECTSCAN_1", false,
                             scan_type == kScan_Interlaced);
        osd->DialogAddButton(tr("Interlaced (Reversed)"), "SELECTSCAN_2", false,
                             scan_type == kScan_Intr2ndField);
    }
}

void TV::FillOSDMenuSubtitles(const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction)
{
    uint capmode  = 0;
    QStringList av_tracks;
    QStringList cc708_tracks;
    QStringList cc608_tracks;
    QStringList ttx_tracks;
    QStringList ttm_tracks;
    QStringList text_tracks;
    uint av_curtrack    = ~0;
    uint cc708_curtrack = ~0;
    uint cc608_curtrack = ~0;
    uint ttx_curtrack   = ~0;
    uint text_curtrack  = ~0;
    bool havetext = false;
    bool forcedon = true;
    bool enabled  = false;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        capmode      = ctx->player->GetCaptionMode();
        enabled      = ctx->player->GetCaptionsEnabled();
        havetext     = ctx->player->HasTextSubtitles();
        forcedon     = ctx->player->GetAllowForcedSubtitles();
        av_tracks    = ctx->player->GetTracks(kTrackTypeSubtitle);
        cc708_tracks = ctx->player->GetTracks(kTrackTypeCC708);
        cc608_tracks = ctx->player->GetTracks(kTrackTypeCC608);
        ttx_tracks   = ctx->player->GetTracks(kTrackTypeTeletextCaptions);
        ttm_tracks   = ctx->player->GetTracks(kTrackTypeTeletextMenu);
        text_tracks  = ctx->player->GetTracks(kTrackTypeRawText);
        if (!av_tracks.empty())
            av_curtrack = (uint) ctx->player->GetTrack(kTrackTypeSubtitle);
        if (!cc708_tracks.empty())
            cc708_curtrack = (uint) ctx->player->GetTrack(kTrackTypeCC708);
        if (!cc608_tracks.empty())
            cc608_curtrack = (uint) ctx->player->GetTrack(kTrackTypeCC608);
        if (!ttx_tracks.empty())
            ttx_curtrack = (uint) ctx->player->GetTrack(kTrackTypeTeletextCaptions);
        if (!text_tracks.empty())
            text_curtrack = (uint) ctx->player->GetTrack(kTrackTypeRawText);
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    bool have_subs = !av_tracks.empty() || havetext || !cc708_tracks.empty() ||
                     !cc608_tracks.empty() || !ttx_tracks.empty() ||
                     !text_tracks.empty();

    if (category == "MAIN")
    {
        if (have_subs || !ttm_tracks.empty())
        {
            osd->DialogAddButton(tr("Subtitles"),
                                 "DIALOG_MENU_SUBTITLES_0",
                                 true, selected == "SUBTITLES");
        }
    }
    else if (category == "SUBTITLES")
    {
        backaction = "MAIN";
        currenttext = tr("Subtitles");

        if (have_subs && enabled)
            osd->DialogAddButton(tr("Disable Subtitles"), ACTION_DISABLESUBS);
        else if (have_subs && !enabled)
            osd->DialogAddButton(tr("Enable Subtitles"), ACTION_ENABLESUBS);
        if (!av_tracks.empty())
        {
            if (forcedon)
            {
                osd->DialogAddButton(tr("Disable Forced Subtitles"),
                                     ACTION_DISABLEFORCEDSUBS);
            }
            else
            {
                osd->DialogAddButton(tr("Enable Forced Subtitles"),
                                     ACTION_ENABLEFORCEDSUBS);
            }
            osd->DialogAddButton(tr("Select Subtitle"),
                                 "DIALOG_MENU_AVSUBTITLES_0",
                                 true, selected == "AVSUBTITLES");
        }
        if (havetext || !text_tracks.empty())
        {
            osd->DialogAddButton(tr("Text Subtitles"),
                                 "DIALOG_MENU_TEXTSUBTITLES_0",
                                 true, selected == "TEXTSUBTITLES");
        }
        if (!cc708_tracks.empty())
        {
            osd->DialogAddButton(tr("Select ATSC CC"),
                                 "DIALOG_MENU_708SUBTITLES_0",
                                 true, selected == "708SUBTITLES");
        }
        if (!cc608_tracks.empty())
        {
            osd->DialogAddButton(tr("Select VBI CC"),
                                 "DIALOG_MENU_608SUBTITLES_0",
                                 true, selected == "608SUBTITLES");
        }
        if (!ttx_tracks.empty())
        {
            osd->DialogAddButton(tr("Select Teletext CC"),
                                 "DIALOG_MENU_TTXSUBTITLES_0",
                                 true, selected == "TTXSUBTITLES");
        }
        if (!ttm_tracks.empty())
            osd->DialogAddButton(tr("Toggle Teletext Menu"), "TOGGLETTM");
    }
    else if (category == "AVSUBTITLES")
    {
        backaction = "SUBTITLES";
        currenttext = tr("Select Subtitle");
        for (uint i = 0; i < (uint)av_tracks.size(); i++)
        {
            osd->DialogAddButton(av_tracks[i],
                                 "SELECTSUBTITLE_" + QString::number(i),
                                 false, i == av_curtrack);
        }
    }
    else if (category == "TEXTSUBTITLES")
    {
        backaction = "SUBTITLES";
        currenttext = tr("Text Subtitles");
        if (havetext)
        {
            if (capmode == kDisplayTextSubtitle)
            {
                osd->DialogAddButton(tr("Disable External Subtitles"),
                                     ACTION_DISABLEEXTTEXT);
            }
            else
            {
                osd->DialogAddButton(tr("Enable External Subtitles"),
                                     ACTION_ENABLEEXTTEXT);
            }
        }
        if (!text_tracks.empty())
        {
            for (uint i = 0; i < (uint)text_tracks.size(); i++)
            {
                osd->DialogAddButton(text_tracks[i],
                                     "SELECTRAWTEXT_" + QString::number(i),
                                     false, i == text_curtrack);
            }
        }
    }
    else if (category == "708SUBTITLES")
    {
        backaction = "SUBTITLES";
        currenttext = tr("Select ATSC CC");
        for (uint i = 0; i < (uint)cc708_tracks.size(); i++)
        {
            osd->DialogAddButton(cc708_tracks[i],
                                 "SELECTCC708_" + QString::number(i),
                                 false, i == cc708_curtrack);
        }
    }
    else if (category == "608SUBTITLES")
    {
        backaction = "SUBTITLES";
        currenttext = tr("Select VBI CC");
        for (uint i = 0; i < (uint)cc608_tracks.size(); i++)
        {
            osd->DialogAddButton(cc608_tracks[i],
                                 "SELECTCC608_" + QString::number(i),
                                 false, i == cc608_curtrack);
        }
    }
    else if (category == "TTXSUBTITLES")
    {
        backaction = "SUBTITLES";
        currenttext = tr("Select Teletext CC");
        for (uint i = 0; i < (uint)ttx_tracks.size(); i++)
        {
            osd->DialogAddButton(ttx_tracks[i],
                                 "SELECTTTC_" + QString::number(i),
                                 false, i == ttx_curtrack);
        }
    }
}

void TV::FillOSDMenuNavigate(const PlayerContext *ctx, OSD *osd,
                             QString category, const QString selected,
                             QString &currenttext, QString &backaction)
{
    int num_chapters  = GetNumChapters(ctx);
    int num_titles    = GetNumTitles(ctx);
    int num_angles    = GetNumAngles(ctx);
    TVState state     = ctx->GetState();
    bool isdvd        = state == kState_WatchingDVD;
    bool isbd         = ctx->buffer && ctx->buffer->IsBD() &&
                        ctx->buffer->BD()->IsHDMVNavigation();
    bool islivetv     = StateIsLiveTV(state);
    bool isrecording  = state == kState_WatchingPreRecorded ||
                        state == kState_WatchingRecording;
    bool previouschan = false;
    if (islivetv)
    {
        QString prev_channum = ctx->GetPreviousChannel();
        QString cur_channum  = QString();
        if (ctx->tvchain)
            cur_channum = ctx->tvchain->GetChannelName(-1);
        if (!prev_channum.isEmpty() && prev_channum != cur_channum)
            previouschan = true;
    }

    bool jump = !num_chapters && !isdvd && !isbd &&
                ctx->buffer->IsSeekingAllowed();
    bool show = isdvd || num_chapters || num_titles || previouschan ||
                isrecording || num_angles || jump;

    if (category == "MAIN")
    {
        if (show)
        {
            osd->DialogAddButton(tr("Navigate"), "DIALOG_MENU_NAVIGATE_0",
                                 true, selected == "NAVIGATE");
        }
    }
    else if (category == "NAVIGATE")
    {
        backaction = "MAIN";
        currenttext = tr("Navigate");
        if (jump)
        {
            osd->DialogAddButton(tr("Jump Ahead"), ACTION_JUMPFFWD, false, false);
            osd->DialogAddButton(tr("Jump Back"), ACTION_JUMPRWND, false, false);
        }
        if (isrecording)
        {
            osd->DialogAddButton(tr("Commercial Auto-Skip"),
                                 "DIALOG_MENU_COMMSKIP_0",
                                 true, selected == "COMMSKIP");
        }
        if (isbd)
        {
            osd->DialogAddButton(tr("Top menu"), ACTION_JUMPTODVDROOTMENU);
            osd->DialogAddButton(tr("Popup menu"), ACTION_JUMPTOPOPUPMENU);
        }
        if (isdvd)
        {
            osd->DialogAddButton(tr("DVD Root Menu"),    ACTION_JUMPTODVDROOTMENU);
            osd->DialogAddButton(tr("DVD Title Menu"),   ACTION_JUMPTODVDTITLEMENU);
            osd->DialogAddButton(tr("DVD Chapter Menu"), ACTION_JUMPTODVDCHAPTERMENU);
        }
        if (previouschan)
        {
            osd->DialogAddButton(tr("Previous Channel"), "PREVCHAN");
        }
        if (num_chapters)
        {
            osd->DialogAddButton(tr("Chapter"), "DIALOG_MENU_AVCHAPTER_0",
                                 true, selected == "AVCHAPTER");
        }
        if (num_angles > 1)
        {
            osd->DialogAddButton(tr("Angle"), "DIALOG_MENU_AVANGLE_0",
                                 true, selected == "AVANGLE");
        }
        if (num_titles)
        {
            osd->DialogAddButton(tr("Title"), "DIALOG_MENU_AVTITLE_0",
                                 true, selected == "AVTITLE");
        }
    }
    else if (category == "AVCHAPTER")
    {
        backaction = "NAVIGATE";
        currenttext = tr("Chapter");
        int current_chapter =  GetCurrentChapter(ctx);
        QList<long long> times;
        GetChapterTimes(ctx, times);
        if (num_chapters == times.size())
        {
            int size = QString::number(num_chapters).size();
            for (int i = 0; i < num_chapters; i++)
            {
                int hours   = times[i] / 60 / 60;
                int minutes = (times[i] / 60) - (hours * 60);
                int secs    = times[i] % 60;
                QString chapter1 = QString("%1").arg(i+1, size, 10, QChar(48));
                QString chapter2 = QString("%1").arg(i+1, 3   , 10, QChar(48));
                QString desc = chapter1 + QString(" (%1:%2:%3)")
                    .arg(hours, 2, 10, QChar(48)).arg(minutes, 2, 10, QChar(48))
                    .arg(secs, 2, 10, QChar(48));
                osd->DialogAddButton(desc, ACTION_JUMPCHAPTER + chapter2,
                                     false, current_chapter == (i + 1));
            }
        }
    }
    else if (category == "AVTITLE")
    {
        backaction = "NAVIGATE";
        currenttext = tr("Title");
        int current_title = GetCurrentTitle(ctx);

        for (int i = 0; i < num_titles; i++)
        {
            if (GetTitleDuration(ctx, i) < 120) // Ignore < 2 minutes long
                continue;

            QString titleIdx = QString("%1").arg(i, 3, 10, QChar(48));
            QString desc = GetTitleName(ctx, i);
            osd->DialogAddButton(desc, ACTION_SWITCHTITLE + titleIdx,
                                 false, current_title == i);
        }
    }
    else if (category == "AVANGLE")
    {
        backaction = "NAVIGATE";
        currenttext = tr("Angle");
        int current_angle = GetCurrentAngle(ctx);

        for (int i = 0; i < num_angles; i++)
        {
            QString angleIdx = QString("%1").arg(i, 3, 10, QChar(48));
            QString desc = GetAngleName(ctx, i);
            osd->DialogAddButton(desc, ACTION_SWITCHANGLE + angleIdx,
                                 false, current_angle == i);
        }
    }
    else if (category == "COMMSKIP")
    {
        backaction = "NAVIGATE";
        currenttext = tr("Commercial Auto-Skip");
        uint cas_ord[] = { 0, 2, 1 };
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        CommSkipMode cur = kCommSkipOff;
        if (ctx->player)
            cur = ctx->player->GetAutoCommercialSkip();
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);

        for (uint i = 0; i < sizeof(cas_ord)/sizeof(uint); i++)
        {
            const CommSkipMode mode = (CommSkipMode) cas_ord[i];
            osd->DialogAddButton(toString((CommSkipMode) cas_ord[i]),
                                 QString("TOGGLECOMMSKIP%1").arg(cas_ord[i]),
                                 false, mode == cur);
        }
    }
}

void TV::FillOSDMenuSource(const PlayerContext *ctx, OSD *osd,
                           QString category, const QString selected,
                           QString &currenttext, QString &backaction)
{
    QMap<uint,InputInfo> sources;
    vector<uint> cardids;
    uint cardid = 0;
    vector<uint> excluded_cardids;
    uint sourceid = 0;

    if ((category == "SOURCE" || category == "INPUTSWITCHING"||
         category == "SOURCESWITCHING") && ctx->recorder)
    {
        cardids = RemoteRequestFreeRecorderList();
        cardid  = ctx->GetCardID();
        cardids.push_back(cardid);
        // The cardids are already in the preferred order.  Don't
        // alter it if switching sources.
        if (category != "SOURCESWITCHING")
            stable_sort(cardids.begin(), cardids.end());
        excluded_cardids.push_back(cardid);
        InfoMap info;
        ctx->recorder->GetChannelInfo(info);
        sourceid = info["sourceid"].toUInt();

        if (category != "INPUTSWITCHING")
        {
            // Get sources available on other cards
            vector<uint>::const_iterator it = cardids.begin();
            for (; it != cardids.end(); ++it)
            {
                vector<InputInfo> inputs =
                        RemoteRequestFreeInputList(*it, excluded_cardids);
                if (inputs.empty())
                    continue;

                for (uint i = 0; i < inputs.size(); i++)
                    if (!sources.contains(inputs[i].sourceid))
                       sources[inputs[i].sourceid] = inputs[i];
            }
            // Get other sources available on this card
            vector<uint> currentinputs = CardUtil::GetInputIDs(cardid);
            if (!currentinputs.empty())
            {
                for (uint i = 0; i < currentinputs.size(); i++)
                {
                    InputInfo info;
                    info.inputid = currentinputs[i];
                    if (CardUtil::GetInputInfo(info))
                        if (!sources.contains(info.sourceid) &&
                            info.livetvorder)
                            sources[info.sourceid] = info;
                }
            }
            // delete current source from list
            sources.remove(sourceid);
        }
    }

    if (category == "MAIN")
    {
        osd->DialogAddButton(tr("Source"), "DIALOG_MENU_SOURCE_0",
                             true, selected == "SOURCE");
    }
    else if (category == "SOURCE")
    {
        backaction = "MAIN";
        currenttext = tr("Source");
        osd->DialogAddButton(tr("Jump to Program"),
                             "DIALOG_MENU_" + ACTION_JUMPREC + "_0",
                             true, selected == ACTION_JUMPREC);

        vector<uint>::const_iterator it = cardids.begin();
        for (; it != cardids.end(); ++it)
        {
            vector<InputInfo> inputs = RemoteRequestFreeInputList(
                *it, excluded_cardids);
            uint testsize = 0;
            for (uint i = 0; i < inputs.size(); i++)
            {
                if ((inputs[i].cardid   == cardid) &&
                    (inputs[i].sourceid == sourceid))
                {
                    testsize = 1;
                    break;
                }
            }
            if (inputs.size() <= testsize)
                continue;
            osd->DialogAddButton(tr("Switch Input"),
                                 "DIALOG_MENU_INPUTSWITCHING_0",
                                 true, selected == "INPUTSWITCHING");
            break;
        }
        if (!sources.empty())
        {
            osd->DialogAddButton(tr("Switch Source"),
                                 "DIALOG_MENU_SOURCESWITCHING_0",
                                 true, selected == "SOURCESWITCHING");
        }
    }
    else if (category == ACTION_JUMPREC)
    {
        backaction = "SOURCE";
        currenttext = tr("Jump to Program");
        osd->DialogAddButton(tr("Recorded Program"),
                             "DIALOG_" + ACTION_JUMPREC + "_X_0",
                             true, selected == ACTION_JUMPREC + "2");
        if (lastProgram != NULL)
        {
            if (lastProgram->GetSubtitle().isEmpty())
                osd->DialogAddButton(lastProgram->GetTitle(), ACTION_JUMPPREV);
            else
                osd->DialogAddButton(QString("%1: %2")
                        .arg(lastProgram->GetTitle())
                        .arg(lastProgram->GetSubtitle()), ACTION_JUMPPREV);
        }
    }
    else if (category == "INPUTSWITCHING")
    {
        backaction = "SOURCE";
        currenttext = tr("Switch Input");
        vector<uint>::const_iterator it = cardids.begin();
        for (; it != cardids.end(); ++it)
        {
            vector<InputInfo> inputs = RemoteRequestFreeInputList(
                *it, excluded_cardids);

            for (uint i = 0; i < inputs.size(); i++)
            {
                // don't add current input to list
                if ((inputs[i].cardid   == cardid) &&
                    (inputs[i].sourceid == sourceid))
                {
                    continue;
                }

                QString name = CardUtil::GetDisplayName(inputs[i].inputid);
                if (name.isEmpty())
                {
                    name = tr("C", "Card") + ":" + QString::number(*it) + " " +
                        tr("I", "Input") + ":" + inputs[i].name;
                }

                osd->DialogAddButton(name,
                    QString("SWITCHTOINPUT_%1").arg(inputs[i].inputid));
            }
        }
    }
    else if (category == "SOURCESWITCHING")
    {
        backaction = "SOURCE";
        currenttext = tr("Switch Source");
        QMap<uint,InputInfo>::const_iterator sit = sources.begin();
        for (; sit != sources.end(); ++sit)
        {
            osd->DialogAddButton(SourceUtil::GetSourceName((*sit).sourceid),
                                 QString("SWITCHTOINPUT_%1").arg((*sit).inputid));
        }
    }
}

void TV::FillOSDMenuJobs(const PlayerContext *ctx, OSD *osd,
                         QString category, const QString selected,
                         QString &currenttext, QString &backaction)
{
    TVState state    = ctx->GetState();
    bool islivetv    = StateIsLiveTV(state);
    bool isrecorded  = state == kState_WatchingPreRecorded;
    bool isrecording = state == kState_WatchingRecording;

    if (category == "MAIN")
    {
        if (islivetv || isrecording || isrecorded)
        {
            osd->DialogAddButton(tr("Jobs"), "DIALOG_MENU_JOBS_0",
                                 true, selected == "JOBS");
        }
    }
    else if (category == "JOBS")
    {
        backaction = "MAIN";
        currenttext = tr("Jobs");

        ctx->LockPlayingInfo(__FILE__, __LINE__);
        bool is_on = ctx->playingInfo->QueryAutoExpire() != kDisableAutoExpire;
        bool transcoding = JobQueue::IsJobQueuedOrRunning(
                                JOB_TRANSCODE,
                                ctx->playingInfo->GetChanID(),
                                ctx->playingInfo->GetScheduledStartTime());
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);

        if (islivetv)
        {
            osd->DialogAddButton(tr("Edit Channel"),   "EDIT");
        }

        if (isrecorded || isrecording)
        {
            osd->DialogAddButton(tr("Edit Recording"), "EDIT");
            osd->DialogAddButton(is_on ? tr("Turn Auto-Expire OFF") :
                                 tr("Turn Auto-Expire ON"), "TOGGLEAUTOEXPIRE");
        }

        if (isrecorded)
        {
            if (transcoding)
            {
                osd->DialogAddButton(tr("Stop Transcoding"), "QUEUETRANSCODE");
            }
            else
            {
                osd->DialogAddButton(tr("Begin Transcoding"),
                                     "DIALOG_MENU_TRANSCODE_0",
                                     true, selected == "TRANSCODE");
            }
        }
    }
    else if (category == "TRANSCODE")
    {
        backaction = "JOBS";
        currenttext = tr("Begin Transcoding");
        osd->DialogAddButton(tr("Default"),       "QUEUETRANSCODE");
        osd->DialogAddButton(tr("Autodetect"),    "QUEUETRANSCODE_AUTO");
        osd->DialogAddButton(tr("High Quality"),  "QUEUETRANSCODE_HIGH");
        osd->DialogAddButton(tr("Medium Quality"),"QUEUETRANSCODE_MEDIUM");
        osd->DialogAddButton(tr("Low Quality"),   "QUEUETRANSCODE_LOW");
    }
}

void TV::FillOSDMenuPlayback(const PlayerContext *ctx, OSD *osd,
                             QString category, const QString selected,
                             QString &currenttext, QString &backaction)
{
    bool allowPIP = IsPIPSupported(ctx);
    bool allowPBP = IsPBPSupported(ctx);
    bool ispaused = false;
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ispaused = ctx->player->IsPaused();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (category == "MAIN")
    {
        osd->DialogAddButton(tr("Playback"), "DIALOG_MENU_PLAYBACK_0",
                             true, selected == "PLAYBACK");
    }
    else if (category == "PLAYBACK")
    {
        backaction = "MAIN";
        currenttext = tr("Playback");

        osd->DialogAddButton(ispaused ? tr("Play") : tr("Pause"),
                             ACTION_PAUSE, false, false);
        osd->DialogAddButton(tr("Adjust Time Stretch"),
                             "DIALOG_MENU_TIMESTRETCH_0", true,
                              selected == "TIMESTRETCH");
        if (allowPIP || allowPBP)
        {
            osd->DialogAddButton(tr("Picture-in-Picture"),
                                 "DIALOG_MENU_PIP_1", true,
                                 selected == "PIP");
        }
        osd->DialogAddButton(tr("Sleep"),
                             "DIALOG_MENU_SLEEP_0", true,
                              selected == "SLEEP");
        if (db_use_channel_groups)
        {
            osd->DialogAddButton(tr("Channel Groups"),
                                 "DIALOG_MENU_CHANNELGROUP_0",
                                 true, selected == "CHANNELGROUP");
        }
        if (!db_browse_always)
            osd->DialogAddButton(tr("Toggle Browse Mode"), "TOGGLEBROWSE");
        osd->DialogAddButton(tr("Playback data"),
                             ACTION_TOGGLEOSDDEBUG, false, false);
    }
    else if (category == "TIMESTRETCH")
    {
        backaction = "PLAYBACK";
        currenttext = tr("Adjust Time Stretch");
        int speedX100 = (int)(round(ctx->ts_normal * 100));
        osd->DialogAddButton(tr("Toggle"), "TOGGLESTRETCH");
        osd->DialogAddButton(tr("Adjust"), "ADJUSTSTRETCH");
        osd->DialogAddButton(tr("0.5X"), "ADJUSTSTRETCH0.5", false, speedX100 == 50);
        osd->DialogAddButton(tr("0.9X"), "ADJUSTSTRETCH0.9", false, speedX100 == 90);
        osd->DialogAddButton(tr("1.0X"), "ADJUSTSTRETCH1.0", false, speedX100 == 100);
        osd->DialogAddButton(tr("1.1X"), "ADJUSTSTRETCH1.1", false, speedX100 == 110);
        osd->DialogAddButton(tr("1.2X"), "ADJUSTSTRETCH1.2", false, speedX100 == 120);
        osd->DialogAddButton(tr("1.3X"), "ADJUSTSTRETCH1.3", false, speedX100 == 130);
        osd->DialogAddButton(tr("1.4X"), "ADJUSTSTRETCH1.4", false, speedX100 == 140);
        osd->DialogAddButton(tr("1.5X"), "ADJUSTSTRETCH1.5", false, speedX100 == 150);
    }
    else if (category == "SLEEP")
    {
        backaction = "PLAYBACK";
        currenttext = tr("Sleep");
        if (sleepTimerId)
            osd->DialogAddButton(tr("Sleep Off"), ACTION_TOGGLESLEEP + "ON");
        osd->DialogAddButton(tr("%n minute(s)", "", 30),
                                ACTION_TOGGLESLEEP + "30");
        osd->DialogAddButton(tr("%n minute(s)", "", 60),
                                ACTION_TOGGLESLEEP + "60");
        osd->DialogAddButton(tr("%n minute(s)", "", 90),
                                ACTION_TOGGLESLEEP + "90");
        osd->DialogAddButton(tr("%n minute(s)", "", 120),
                                ACTION_TOGGLESLEEP + "120");
    }
    else if (category == "CHANNELGROUP")
    {
        backaction = "PLAYBACK";
        currenttext = tr("Channel Groups");
        osd->DialogAddButton(tr("All Channels"), "CHANGROUP_ALL_CHANNELS");
        ChannelGroupList::const_iterator it;
        for (it = db_channel_groups.begin();
             it != db_channel_groups.end(); ++it)
        {
            osd->DialogAddButton(it->name,
                                 QString("CHANGROUP_%1").arg(it->grpid),
                                 false, (int)(it->grpid) == channelGroupId);
        }
    }
    else if (category == "PIP" && (allowPIP || allowPBP))
    {
        backaction = "PLAYBACK";
        currenttext = tr("Picture-in-Picture");
        bool hasPBP = (player.size()>1) && GetPlayer(ctx,1)->IsPBP();
        bool hasPIP = (player.size()>1) && GetPlayer(ctx,1)->IsPIP();

        if (RemoteGetFreeRecorderCount())
        {
            if (player.size() <= kMaxPIPCount && !hasPBP && allowPIP)
                osd->DialogAddButton(tr("Open Live TV PIP"), "CREATEPIPVIEW");
            if (player.size() < kMaxPBPCount && !hasPIP && allowPBP)
                osd->DialogAddButton(tr("Open Live TV PBP"), "CREATEPBPVIEW");
        }

        if (player.size() <= kMaxPIPCount && !hasPBP && allowPIP)
            osd->DialogAddButton(tr("Open Recording PIP"), "JUMPRECPIP");
        if (player.size() < kMaxPBPCount && !hasPIP && allowPBP)
            osd->DialogAddButton(tr("Open Recording PBP"), "JUMPRECPBP");

        if (player.size() > 1)
        {
            osd->DialogAddButton(tr("Change Active Window"), "NEXTPIPWINDOW");

            QString pipType  = (ctx->IsPBP()) ? "PBP" : "PIP";
            QString toggleMode = QString("TOGGLE%1MODE").arg(pipType);

            bool isPBP = ctx->IsPBP();
            const PlayerContext *mctx = GetPlayer(ctx, 0);
            QString pipClose = (isPBP) ? tr("Close PBP") : tr("Close PIP");
            if (mctx == ctx)
            {
                if (player.size() > 2)
                    pipClose = (isPBP) ? tr("Close PBPs") : tr("Close PIPs");
                osd->DialogAddButton(pipClose, toggleMode);

                if (player.size() == 2)
                    osd->DialogAddButton(tr("Swap Windows"), "SWAPPIP");
            }
            else
            {
                osd->DialogAddButton(pipClose, toggleMode);
                osd->DialogAddButton(tr("Swap Windows"), "SWAPPIP");
            }

            uint max_cnt = min(kMaxPBPCount, kMaxPIPCount+1);
            if (player.size() <= max_cnt &&
                !(hasPIP && !allowPBP) &&
                !(hasPBP && !allowPIP))
            {
                QString switchTo = (isPBP) ? tr("Switch to PIP") : tr("Switch to PBP");
                osd->DialogAddButton(switchTo, "TOGGLEPIPSTATE");
            }
        }
    }
}

void TV::FillOSDMenuSchedule(const PlayerContext *ctx, OSD *osd,
                             QString category, const QString selected,
                             QString &currenttext, QString &backaction)
{
    if (category == "MAIN")
    {
        osd->DialogAddButton(tr("Schedule"),
                             "DIALOG_MENU_SCHEDULE_0",
                             true, selected == "SCHEDULE");
    }
    else if (category == "SCHEDULE")
    {
        backaction = "MAIN";
        currenttext = tr("Schedule");
        osd->DialogAddButton(tr("Program Guide"),           ACTION_GUIDE);
        osd->DialogAddButton(tr("Program Finder"),          ACTION_FINDER);
        osd->DialogAddButton(tr("Upcoming Recordings"),     ACTION_VIEWSCHEDULED);
        osd->DialogAddButton(tr("Edit Recording Schedule"), "SCHEDULE");
    }
}

void TV::FillOSDMenuJumpRec(PlayerContext* ctx, const QString category,
                            int level, const QString selected)
{
    // bool in_recgroup = !category.isEmpty() && level > 0;
    if (level < 0 || level > 1)
    {
        level = 0;
        // in_recgroup = false;
    }

    OSD *osd = GetOSDLock(ctx);
    if (osd)
    {
        QString title = tr("Recorded Program");
        osd->DialogShow("osd_jumprec", title);

        QMutexLocker locker(&progListsLock);
        progLists.clear();
        vector<ProgramInfo*> *infoList = RemoteGetRecordedList(0);
        bool LiveTVInAllPrograms = gCoreContext->GetNumSetting("LiveTVInAllPrograms",0);
        if (infoList)
        {
            QList<QString> titles_seen;

            ctx->LockPlayingInfo(__FILE__, __LINE__);
            QString currecgroup = ctx->playingInfo->GetRecordingGroup();
            ctx->UnlockPlayingInfo(__FILE__, __LINE__);

            vector<ProgramInfo *>::const_iterator it = infoList->begin();
            for ( ; it != infoList->end(); ++it)
            {
                if ((*it)->GetRecordingGroup() != "LiveTV" || LiveTVInAllPrograms ||
                     (*it)->GetRecordingGroup() == currecgroup)
                {
                    progLists[(*it)->GetRecordingGroup()].push_front(
                        new ProgramInfo(*(*it)));
                }
            }

            ProgramInfo *lastprog = GetLastProgram();
            QMap<QString,ProgramList>::const_iterator Iprog;
            for (Iprog = progLists.begin(); Iprog != progLists.end(); ++Iprog)
            {
                const ProgramList &plist = *Iprog;
                uint progIndex = (uint) plist.size();
                QString group = Iprog.key();

                if (plist[0]->GetRecordingGroup() != currecgroup)
                    SetLastProgram(plist[0]);

                if (progIndex == 1 && level == 0)
                {
                    osd->DialogAddButton(Iprog.key(),
                                         QString("JUMPPROG %1 0").arg(group));
                }
                else if (progIndex > 1 && level == 0)
                {
                    QString act = QString("DIALOG_%1_%2_1")
                                    .arg(ACTION_JUMPREC).arg(group);
                    osd->DialogAddButton(group, act,
                                         true, selected == group);
                }
                else if (level == 1 && Iprog.key() == category)
                {
                    ProgramList::const_iterator it = plist.begin();
                    for (; it != plist.end(); ++it)
                    {
                        const ProgramInfo *p = *it;

                        if (titles_seen.contains(p->GetTitle()))
                            continue;

                        titles_seen.push_back(p->GetTitle());

                        ProgramList::const_iterator it2 = plist.begin();
                        int j = -1;
                        for (; it2 != plist.end(); ++it2)
                        {
                            const ProgramInfo *q = *it2;
                            j++;

                            if (q->GetTitle() != p->GetTitle())
                                continue;

                            osd->DialogAddButton(q->GetSubtitle().isEmpty() ?
                                    q->GetTitle() : q->GetSubtitle(),
                                    QString("JUMPPROG %1 %2")
                                        .arg(Iprog.key()).arg(j));
                        }
                    }
                }
            }
            SetLastProgram(lastprog);
            if (lastprog)
                delete lastprog;

            while (!infoList->empty())
            {
                delete infoList->back();
                infoList->pop_back();
            }
            delete infoList;
        }

        if (!category.isEmpty())
        {
            if (level == 1)
                osd->DialogBack(category, "DIALOG_" + ACTION_JUMPREC + "_X_0");
            else if (level == 0)
                osd->DialogBack(ACTION_JUMPREC,
                                "DIALOG_MENU_" + ACTION_JUMPREC +"_0");
        }
    }
    ReturnOSDLock(ctx, osd);
}

void TV::HandleDeinterlacer(PlayerContext *ctx, const QString &action)
{
    if (!action.startsWith("DEINTERLACER"))
        return;

    QString deint = action.mid(13);
    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->ForceDeinterlacer(deint);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::ToggleAutoExpire(PlayerContext *ctx)
{
    QString desc = QString::null;

    ctx->LockPlayingInfo(__FILE__, __LINE__);

    if (ctx->playingInfo->QueryAutoExpire() != kDisableAutoExpire)
    {
        ctx->playingInfo->SaveAutoExpire(kDisableAutoExpire);
        desc = tr("Auto-Expire OFF");
    }
    else
    {
        ctx->playingInfo->SaveAutoExpire(kNormalAutoExpire);
        desc = tr("Auto-Expire ON");
    }

    ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    if (!desc.isEmpty())
        UpdateOSDSeekMessage(ctx, desc, kOSDTimeout_Med);
}

void TV::SetAutoCommercialSkip(const PlayerContext *ctx,
                               CommSkipMode skipMode)
{
    QString desc = QString::null;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
    {
        ctx->player->SetAutoCommercialSkip(skipMode);
        desc = toString(ctx->player->GetAutoCommercialSkip());
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (!desc.isEmpty())
        UpdateOSDSeekMessage(ctx, desc, kOSDTimeout_Med);
}

void TV::SetManualZoom(const PlayerContext *ctx, bool zoomON, QString desc)
{
    if (ctx->GetPIPState() != kPIPOff)
        return;

    zoomMode = zoomON;
    if (zoomON)
        ClearOSD(ctx);

    if (!desc.isEmpty())
        UpdateOSDSeekMessage(ctx, desc, kOSDTimeout_Med);
}

static struct ActionDefStruct<TV> tjpActions[] = {
    { ACTION_JUMPPREV, &TV::doJumpProgPrevChan },
    { "PREVCHAN",      &TV::doJumpProgPrevChan },
    { "^JUMPPROG",     &TV::doJumpProgProgram },
    { ACTION_JUMPREC,  &TV::doJumpProgRecord },
    { "JUMPRECPIP",    &TV::doJumpProgRecord },
    { "JUMPRECPBP",    &TV::doJumpProgRecord },
};
static int tjpActionCount = NELEMS(tjpActions);

bool TV::doJumpProgPrevChan(const QString &action)
{
    const PlayerContext *mctx = GetPlayer(m_actionContext, 0);
    TVState s = m_actionContext->GetState();
    if (action != "PREVCHAN" || !StateIsLiveTV(s))
    {
        if (mctx == m_actionContext)
        {
            PrepareToExitPlayer(m_actionContext, __LINE__);
            jumpToProgram = true;
            SetExitPlayer(true, true);
        }
        return true;
    }
    return false;
}

bool TV::doJumpProgProgram(const QString &action)
{
    bool ok;
    QString progKey   = action.section(" ",1,-2);
    uint    progIndex = action.section(" ",-1,-1).toUInt(&ok);
    ProgramInfo *p = NULL;

    if (ok)
    {
        QMutexLocker locker(&progListsLock);
        QMap<QString,ProgramList>::const_iterator it = progLists.find(progKey);
        if (it != progLists.end())
        {
            const ProgramInfo *tmp = (*it)[progIndex];
            if (tmp)
                p = new ProgramInfo(*tmp);
        }
    }

    if (!p)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC +
            QString("Failed to locate jump to program '%1' @ %2")
                .arg(progKey).arg(action.section(" ",-1,-1)));
        return true;
    }

    PIPState state = kPIPOff;
    {
        QMutexLocker locker(&timerIdLock);
        state = jumpToProgramPIPState;
    }

    if (state == kPIPOff)
    {
        const PlayerContext *mctx = GetPlayer(m_actionContext, 0);
        if (mctx == m_actionContext)
        {
            PrepToSwitchToRecordedProgram(m_actionContext, *p);
        }
    }
    else
    {
        QString type = (jumpToProgramPIPState == kPIPonTV) ? "PIP" : "PBP";
        LOG(VB_GENERAL, LOG_INFO, LOC +
            QString("Creating %1 with program: %2")
                .arg(type).arg(p->toString(ProgramInfo::kTitleSubtitle)));

        if (jumpToProgramPIPState == kPIPonTV)
            CreatePIP(m_actionContext, p);
        else if (jumpToProgramPIPState == kPBPLeft)
            CreatePBP(m_actionContext, p);
    }

    delete p;

    return true;
}

bool TV::doJumpProgRecord(const QString &action)
{
    bool wants_pip =  (action == "JUMPRECPIP");
    bool wants_pbp =  (action == "JUMPRECPBP");

    {
        QMutexLocker locker(&timerIdLock);
        jumpToProgramPIPState = wants_pip ? kPIPonTV :
            (wants_pbp ? kPBPLeft : kPIPOff);
    }

    TVState s = m_actionContext->GetState();
    if ((wants_pbp || wants_pip || db_jump_prefer_osd) &&
        (StateIsPlaying(s) || StateIsLiveTV(s)))
    {
        QMutexLocker locker(&timerIdLock);
        if (jumpMenuTimerId)
            KillTimer(jumpMenuTimerId);
        jumpMenuTimerId = StartTimer(1, __LINE__);
    }
    else if (RunPlaybackBoxPtr)
        EditSchedule(m_actionContext, kPlaybackBox);
    else
        LOG(VB_GENERAL, LOG_ERR, "Failed to open jump to program GUI");

    return true;
}

bool TV::HandleJumpToProgramAction(PlayerContext *ctx,
                                   const QStringList &actions)
{
    m_actionContext = ctx;

    if (!m_jumpProgramActions)
        m_jumpProgramActions = new MythActions<TV>(this, tjpActions,
                                                   tjpActionCount);
    bool handled = m_jumpProgramActions->handleActions(actions);

    return handled;
}

#define MINUTE 60*1000

void TV::ToggleSleepTimer(const PlayerContext *ctx, const QString &time)
{
    int mins = 0;

    if (time == ACTION_TOGGLESLEEP + "ON")
    {
        if (sleepTimerId)
        {
            KillTimer(sleepTimerId);
            sleepTimerId = 0;
        }
        else
        {
            mins = 60;
            sleepTimerTimeout = mins * MINUTE;
            sleepTimerId = StartTimer(sleepTimerTimeout, __LINE__);
        }
    }
    else
    {
        if (sleepTimerId)
        {
            KillTimer(sleepTimerId);
            sleepTimerId = 0;
        }

        if (time.length() > 11)
        {
            bool intRead = false;
            mins = time.right(time.length() - 11).toInt(&intRead);

            if (intRead)
            {
                // catch 120 -> 240 mins
                if (mins < 30)
                {
                    mins *= 10;
                }
            }
            else
            {
                mins = 0;
                LOG(VB_GENERAL, LOG_ERR, LOC + "Invalid time " + time);
            }
        }
        else
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + "Invalid time string " + time);
        }

        if (mins)
        {
            sleepTimerTimeout = mins * MINUTE;
            sleepTimerId = StartTimer(sleepTimerTimeout, __LINE__);
        }
    }

    QString out;
    if (mins != 0)
        out = tr("Sleep") + " " + QString::number(mins);
    else
        out = tr("Sleep") + " " + sleep_times[0].dispString;
    SetOSDMessage(ctx, out);
}

void TV::ShowNoRecorderDialog(const PlayerContext *ctx, NoRecorderMsg msgType)
{
    QString errorText;

    switch (msgType)
    {
        case kNoRecorders:
            errorText = tr("MythTV is already using all available "
                           "inputs for the channel you selected. "
                           "If you want to watch an in-progress recording, "
                           "select one from the playback menu.  If you "
                           "want to watch Live TV, cancel one of the "
                           "in-progress recordings from the delete "
                           "menu.");
            break;
        case kNoCurrRec:
            errorText = tr("Error: MythTV is using all inputs, "
                           "but there are no active recordings?");
            break;
        case kNoTuners:
            errorText = tr("MythTV has no capture cards defined. "
                           "Please run the mythtv-setup program.");
            break;
    }

    OSD *osd = GetOSDLock(ctx);
    if (osd)
    {
        osd->DialogShow(OSD_DLG_INFO, errorText);
        osd->DialogAddButton(tr("OK"), "DIALOG_INFO_X_X");
    }
    else
    {
        ShowOkPopup(errorText);
    }
    ReturnOSDLock(ctx, osd);
}

/**
 *  \brief Used in ChangeChannel(), ChangeChannel(),
 *         and ToggleInputs() to temporarily stop video output.
 */
void TV::PauseLiveTV(PlayerContext *ctx)
{
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("PauseLiveTV() player ctx %1")
            .arg(find_player_index(ctx)));

    lockTimerOn = false;

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player && ctx->buffer)
    {
        ctx->buffer->IgnoreLiveEOF(true);
        ctx->buffer->StopReads();
        ctx->player->PauseDecoder();
        ctx->buffer->StartReads();
    }
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    // XXX: Get rid of this?
    ctx->recorder->PauseRecorder();

    ctx->lastSignalMsg.clear();
    ctx->lastSignalUIInfo.clear();

    lockTimerOn = false;

    QString input = ctx->recorder->GetInput();
    uint timeout  = ctx->recorder->GetSignalLockTimeout(input);

    if (timeout < 0xffffffff && !ctx->IsPIP())
    {
        lockTimer.start();
        lockTimerOn = true;
    }

    SetSpeedChangeTimer(0, __LINE__);
}

/**
 *  \brief Used in ChangeChannel(), ChangeChannel(),
 *         and ToggleInputs() to restart video output.
 */
void TV::UnpauseLiveTV(PlayerContext *ctx, bool bQuietly /*=false*/)
{
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("UnpauseLiveTV() player ctx %1")
            .arg(find_player_index(ctx)));

    if (ctx->HasPlayer() && ctx->tvchain)
    {
        ctx->ReloadTVChain();
        ctx->tvchain->JumpTo(-1, 1);
        ctx->LockDeletePlayer(__FILE__, __LINE__);
        if (ctx->player)
            ctx->player->Play(ctx->ts_normal, true, false);
        ctx->UnlockDeletePlayer(__FILE__, __LINE__);
        ctx->buffer->IgnoreLiveEOF(false);

        SetSpeedChangeTimer(0, __LINE__);
    }

    ITVRestart(ctx, true);

    if (ctx->HasPlayer() && !bQuietly)
    {
        UpdateOSDProgInfo(ctx, "program_info");
        UpdateLCD();
        ctx->PushPreviousChannel();
    }
}

/**
 * \brief Restart the MHEG/MHP engine.
 */
void TV::ITVRestart(PlayerContext *ctx, bool isLive)
{
    int chanid = -1;
    int sourceid = -1;

    if (ContextIsPaused(ctx, __FILE__, __LINE__))
        return;

    ctx->LockPlayingInfo(__FILE__, __LINE__);
    if (ctx->playingInfo)
    {
        chanid = ctx->playingInfo->GetChanID();
        sourceid = ChannelUtil::GetSourceIDForChannel(chanid);
    }
    ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    if (ctx->player)
        ctx->player->ITVRestart(chanid, sourceid, isLive);
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);
}

void TV::DoJumpFFWD(PlayerContext *ctx)
{
    if (GetState(ctx) == kState_WatchingDVD)
        DVDJumpForward(ctx);
    else if (GetNumChapters(ctx) > 0)
        DoJumpChapter(ctx, 9999);
    else
        DoSeek(ctx, ctx->jumptime * 60, tr("Jump Ahead"));
}

void TV::DoJumpRWND(PlayerContext *ctx)
{
    if (GetState(ctx) == kState_WatchingDVD)
        DVDJumpBack(ctx);
    else if (GetNumChapters(ctx) > 0)
        DoJumpChapter(ctx, -1);
    else
        DoSeek(ctx, -ctx->jumptime * 60, tr("Jump Back"));
}

/*  \fn TV::DVDJumpBack(PlayerContext*)
    \brief jump to the previous dvd title or chapter
*/
void TV::DVDJumpBack(PlayerContext *ctx)
{
    DVDRingBuffer *dvdrb = dynamic_cast<DVDRingBuffer*>(ctx->buffer);
    if (!ctx->HasPlayer() || !dvdrb)
        return;

    if (ctx->buffer->IsInDiscMenuOrStillFrame())
    {
        UpdateOSDSeekMessage(ctx, tr("Skip Back Not Allowed"), kOSDTimeout_Med);
    }
    else if (!dvdrb->StartOfTitle())
    {
        DoJumpChapter(ctx, -1);
    }
    else
    {
        uint titleLength = dvdrb->GetTotalTimeOfTitle();
        uint chapterLength = dvdrb->GetChapterLength();
        if ((titleLength == chapterLength) && chapterLength > 300)
        {
            DoSeek(ctx, -ctx->jumptime * 60, tr("Jump Back"));
        }
        else
        {
            ctx->LockDeletePlayer(__FILE__, __LINE__);
            if (ctx->player)
                ctx->player->GoToDVDProgram(0);
            ctx->UnlockDeletePlayer(__FILE__, __LINE__);

            UpdateOSDSeekMessage(ctx, tr("Previous Title"), kOSDTimeout_Med);
        }
    }
}

/* \fn TV::DVDJumpForward(PlayerContext*)
 * \brief jump to the next dvd title or chapter
 */
void TV::DVDJumpForward(PlayerContext *ctx)
{
    DVDRingBuffer *dvdrb = dynamic_cast<DVDRingBuffer*>(ctx->buffer);
    if (!ctx->HasPlayer() || !dvdrb)
        return;

    bool in_still = dvdrb->IsInStillFrame();
    bool in_menu  = dvdrb->IsInMenu();
    if (in_still && !dvdrb->NumMenuButtons())
    {
        dvdrb->SkipStillFrame();
        UpdateOSDSeekMessage(ctx, tr("Skip Still Frame"), kOSDTimeout_Med);
    }
    else if (!dvdrb->EndOfTitle() && !in_still && !in_menu)
    {
        DoJumpChapter(ctx, 9999);
    }
    else if (!in_still && !in_menu)
    {
        uint titleLength = dvdrb->GetTotalTimeOfTitle();
        uint chapterLength = dvdrb->GetChapterLength();
        uint currentTime = dvdrb->GetCurrentTime();
        if ((titleLength == chapterLength) &&
             (currentTime < (chapterLength - (ctx->jumptime * 60))) &&
             chapterLength > 300)
        {
            DoSeek(ctx, ctx->jumptime * 60, tr("Jump Ahead"));
        }
        else
        {
            ctx->LockDeletePlayer(__FILE__, __LINE__);
            if (ctx->player)
                ctx->player->GoToDVDProgram(1);
            ctx->UnlockDeletePlayer(__FILE__, __LINE__);

            UpdateOSDSeekMessage(ctx, tr("Next Title"), kOSDTimeout_Med);
        }
    }
}

/* \fn TV::IsBookmarkAllowed(const PlayerContext*) const
 * \brief Returns true if bookmarks are allowed for the current player.
 */
bool TV::IsBookmarkAllowed(const PlayerContext *ctx) const
{
    ctx->LockPlayingInfo(__FILE__, __LINE__);

    // Allow bookmark of "Record current LiveTV program"
    if (StateIsLiveTV(GetState(ctx)) && ctx->playingInfo &&
        (ctx->playingInfo->QueryAutoExpire() == kLiveTVAutoExpire))
    {
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        return false;
    }

    if (StateIsLiveTV(GetState(ctx)) && !ctx->playingInfo)
    {
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        return false;
    }

    ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    return ctx->buffer && ctx->buffer->IsBookmarkAllowed();
}

/* \fn TV::IsDeleteAllowed(const PlayerContext*) const
 * \brief Returns true if the delete menu option should be offered.
 */
bool TV::IsDeleteAllowed(const PlayerContext *ctx) const
{
    bool allowed = false;

    if (!StateIsLiveTV(GetState(ctx)))
    {
        ctx->LockPlayingInfo(__FILE__, __LINE__);
        ProgramInfo *curProgram = ctx->playingInfo;
        allowed = curProgram && curProgram->QueryIsDeleteCandidate(true);
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);
    }

    return allowed;
}

void TV::ShowOSDStopWatchingRecording(PlayerContext *ctx)
{
    ClearOSD(ctx);

    if ((ctx != GetPlayer(ctx, 0)))
        return;

    if (!ContextIsPaused(ctx, __FILE__, __LINE__))
        DoTogglePause(ctx, false);

    QString message;
    QString videotype = QString::null;
    QStringList options;

    if (StateIsLiveTV(GetState(ctx)))
        videotype = tr("Live TV");
    else if (ctx->buffer->IsDVD())
        videotype = tr("this DVD");

    ctx->LockPlayingInfo(__FILE__, __LINE__);
    if (videotype.isEmpty() && ctx->playingInfo->IsVideo())
        videotype = tr("this Video");
    ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    if (videotype.isEmpty())
        videotype = tr("this recording");

    OSD *osd = GetOSDLock(ctx);
    if (osd)
    {
        osd->DialogShow(OSD_DLG_VIDEOEXIT,
                        tr("You are exiting %1").arg(videotype));

        if (IsBookmarkAllowed(ctx))
        {
            osd->DialogAddButton(tr("Save this position and go to the menu"),
                                 "DIALOG_VIDEOEXIT_SAVEPOSITIONANDEXIT_0");
            osd->DialogAddButton(tr("Do not save, just exit to the menu"),
                                 ACTION_STOP);
        }
        else
            osd->DialogAddButton(tr("Exit %1").arg(videotype),
                                 ACTION_STOP);

        if (IsDeleteAllowed(ctx))
            osd->DialogAddButton(tr("Delete this recording"),
                                 "DIALOG_VIDEOEXIT_CONFIRMDELETE_0");

        osd->DialogAddButton(tr("Keep watching"),
                             "DIALOG_VIDEOEXIT_KEEPWATCHING_0");
        osd->DialogBack("", "DIALOG_VIDEOEXIT_KEEPWATCHING_0", true);
    }
    ReturnOSDLock(ctx, osd);

    QMutexLocker locker(&timerIdLock);
    if (videoExitDialogTimerId)
        KillTimer(videoExitDialogTimerId);
    videoExitDialogTimerId = StartTimer(kVideoExitDialogTimeout, __LINE__);
}

void TV::ShowOSDPromptDeleteRecording(PlayerContext *ctx, QString title,
                                      bool force)
{
    ctx->LockPlayingInfo(__FILE__, __LINE__);

    if (ctx->ff_rew_state ||
        StateIsLiveTV(ctx->GetState()) ||
        exitPlayerTimerId)
    {
        // this should only occur when the cat walks on the keyboard.
        LOG(VB_GENERAL, LOG_ERR, "It is unsafe to delete at the moment");
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        return;
    }

    if ((ctx != GetPlayer(ctx, 0)))
    {
        // just to avoid confusion...
        LOG(VB_GENERAL, LOG_ERR, "Only the main program may be deleted");
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        return;
    }

    bool paused = ContextIsPaused(ctx, __FILE__, __LINE__);
    if (!ctx->playingInfo->QueryIsDeleteCandidate(true))
    {
        LOG(VB_GENERAL, LOG_ERR,
            "This program cannot be deleted at this time.");
        ProgramInfo pginfo(*ctx->playingInfo);
        ctx->UnlockPlayingInfo(__FILE__, __LINE__);

        OSD *osd = GetOSDLock(ctx);
        if (osd && !osd->DialogVisible())
        {
            QString message = QObject::tr("Cannot delete program ") +
                QString("%1 ")
                .arg(pginfo.GetTitle());

            if (!pginfo.GetSubtitle().isEmpty())
                message += QString("\"%1\" ").arg(pginfo.GetSubtitle());

            if (!pginfo.IsRecording())
            {
                message += QObject::tr("because it is not a recording.");
            }
            else
            {
                message += QObject::tr("because it is in use by");
                QStringList byWho;
                pginfo.QueryIsInUse(byWho);
                for (uint i = 0; i+2 < (uint)byWho.size(); i+=3)
                {
                    if (byWho[i+1] == gCoreContext->GetHostName() &&
                        byWho[i+0].contains(kPlayerInUseID))
                        continue;
                    if (byWho[i+0].contains(kRecorderInUseID))
                        continue;
                    message += " " + byWho[i+2];
                }
            }
            osd->DialogShow(OSD_DLG_DELETE, message);
            QString action = "DIALOG_DELETE_OK_0";
            osd->DialogAddButton(tr("OK"), action);
            osd->DialogBack("", action, true);
        }
        ReturnOSDLock(ctx, osd);
        // If the delete prompt is to be displayed at the end of a
        // recording that ends in a final cut region, it will get into
        // a loop of popping up the OK button while the cut region
        // plays.  Avoid this.
        if (ctx->player->IsNearEnd() && !paused)
            SetExitPlayer(true, true);

        return;
    }
    ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    ClearOSD(ctx);

    if (!paused)
        DoTogglePause(ctx, false);

    InfoMap infoMap;
    ctx->GetPlayingInfoMap(infoMap);
    QString message = QString("%1\n%2\n%3")
        .arg(title).arg(infoMap["title"]).arg(infoMap["timedate"]);

    OSD *osd = GetOSDLock(ctx);
    if (osd && (!osd->DialogVisible() || force))
    {
        osd->DialogShow(OSD_DLG_VIDEOEXIT, message);
        if (title == "End Of Recording")
        {
            osd->DialogAddButton(tr("Delete it, but allow it to re-record"),
                                 "DIALOG_VIDEOEXIT_DELETEANDRERECORD_0");
            osd->DialogAddButton(tr("Delete it"),
                                 "DIALOG_VIDEOEXIT_JUSTDELETE_0");
            osd->DialogAddButton(tr("Save it so I can watch it again"),
                                 ACTION_STOP, false, true);
        }
        else
        {
            osd->DialogAddButton(tr("Yes, and allow re-record"),
                                 "DIALOG_VIDEOEXIT_DELETEANDRERECORD_0");
            osd->DialogAddButton(tr("Yes, delete it"),
                                 "DIALOG_VIDEOEXIT_JUSTDELETE_0");
            osd->DialogAddButton(tr("No, keep it"),
                                 ACTION_STOP, false, true);
            if (!paused)
                osd->DialogBack("", "DIALOG_PLAY_0_0", true);
        }

        QMutexLocker locker(&timerIdLock);
        if (videoExitDialogTimerId)
            KillTimer(videoExitDialogTimerId);
        videoExitDialogTimerId = StartTimer(kVideoExitDialogTimeout, __LINE__);
    }
    ReturnOSDLock(ctx, osd);
}

bool TV::HandleOSDVideoExit(PlayerContext *ctx, QString action)
{
    if (!DialogIsVisible(ctx, OSD_DLG_VIDEOEXIT))
        return false;

    bool hide        = true;
    bool delete_ok   = IsDeleteAllowed(ctx);
    bool bookmark_ok = IsBookmarkAllowed(ctx);

    ctx->LockDeletePlayer(__FILE__, __LINE__);
    bool near_end = ctx->player && ctx->player->IsNearEnd();
    ctx->UnlockDeletePlayer(__FILE__, __LINE__);

    if (action == "DELETEANDRERECORD" && delete_ok)
    {
        allowRerecord = true;
        requestDelete = true;
        SetExitPlayer(true, true);
    }
    else if (action == "JUSTDELETE" && delete_ok)
    {
        requestDelete = true;
        SetExitPlayer(true, true);
    }
    else if (action == "CONFIRMDELETE")
    {
        hide = false;
        ShowOSDPromptDeleteRecording(ctx, tr("Are you sure you want to delete:"),
                                     true);
    }
    else if (action == "SAVEPOSITIONANDEXIT" && bookmark_ok)
    {
        PrepareToExitPlayer(ctx, __LINE__);
        SetExitPlayer(true, true);
    }
    else if (action == "KEEPWATCHING" && !near_end)
    {
        DoTogglePause(ctx, true);
    }

    return hide;
}

void TV::SetLastProgram(const ProgramInfo *rcinfo)
{
    QMutexLocker locker(&lastProgramLock);

    if (lastProgram)
        delete lastProgram;

    if (rcinfo)
        lastProgram = new ProgramInfo(*rcinfo);
    else
        lastProgram = NULL;
}

ProgramInfo *TV::GetLastProgram(void) const
{
    QMutexLocker locker(&lastProgramLock);
    if (lastProgram)
        return new ProgramInfo(*lastProgram);
    return NULL;
}

QString TV::GetRecordingGroup(int player_idx) const
{
    QString ret = QString::null;

    const PlayerContext *ctx = GetPlayerReadLock(player_idx, __FILE__, __LINE__);
    if (ctx)
    {
        if (StateIsPlaying(GetState(ctx)))
        {
            ctx->LockPlayingInfo(__FILE__, __LINE__);
            if (ctx->playingInfo)
                ret = ctx->playingInfo->GetRecordingGroup();
            ctx->UnlockPlayingInfo(__FILE__, __LINE__);
        }
    }
    ReturnPlayerLock(ctx);
    return ret;
}

bool TV::IsSameProgram(int player_idx, const ProgramInfo *rcinfo) const
{
    if (!rcinfo)
        return false;

    bool ret = false;
    const PlayerContext *ctx = GetPlayerReadLock(player_idx, __FILE__, __LINE__);
    if (ctx)
        ret = ctx->IsSameProgram(*rcinfo);
    ReturnPlayerLock(ctx);

    return ret;
}

void TV::RestoreScreenSaver(const PlayerContext *ctx)
{
    if (ctx == GetPlayer(ctx, 0))
        GetMythUI()->RestoreScreensaver();
}

bool TV::ContextIsPaused(PlayerContext *ctx, const char *file, int location)
{
    if (!ctx)
        return false;
    bool paused = false;
    ctx->LockDeletePlayer(file, location);
    if (ctx->player)
        paused = ctx->player->IsPaused();
    ctx->UnlockDeletePlayer(file, location);
    return paused;
}

OSD *TV::GetOSDL(const char *file, int location)
{
    PlayerContext *actx = GetPlayerReadLock(-1, file, location);

    OSD *osd = GetOSDL(actx, file, location);
    if (!osd)
        ReturnPlayerLock(actx);

    return osd;
}

OSD *TV::GetOSDL(const PlayerContext *ctx, const char *file, int location)
{
    if (!ctx)
        return NULL;

    const PlayerContext *mctx = GetPlayer(ctx, 0);

    mctx->LockDeletePlayer(file, location);
    if (mctx->player && ctx->IsPIP())
    {
        mctx->LockOSD();
        OSD *osd = mctx->player->GetOSD();
        if (!osd)
        {
            mctx->UnlockOSD();
            mctx->UnlockDeletePlayer(file, location);
        }
        else
            osd_lctx[osd] = mctx;
        return osd;
    }
    mctx->UnlockDeletePlayer(file, location);

    ctx->LockDeletePlayer(file, location);
    if (ctx->player && !ctx->IsPIP())
    {
        ctx->LockOSD();
        OSD *osd = ctx->player->GetOSD();
        if (!osd)
        {
            ctx->UnlockOSD();
            ctx->UnlockDeletePlayer(file, location);
        }
        else
            osd_lctx[osd] = ctx;
        return osd;
    }
    ctx->UnlockDeletePlayer(file, location);

    return NULL;
}

void TV::ReturnOSDLock(const PlayerContext *ctx, OSD *&osd)
{
    if (!ctx || !osd)
        return;

    osd_lctx[osd]->UnlockOSD();
    osd_lctx[osd]->UnlockDeletePlayer(__FILE__, __LINE__);

    osd = NULL;
}

PlayerContext *TV::GetPlayerWriteLock(int which, const char *file, int location)
{
    playerLock.lockForWrite();

    if ((which >= (int)player.size()))
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            QString("GetPlayerWriteLock(%1,%2,%3) returning NULL size(%4)")
                .arg(which).arg(file).arg(location).arg(player.size()));
        return NULL;
    }

    return (which < 0) ? player[playerActive] : player[which];
}

PlayerContext *TV::GetPlayerReadLock(int which, const char *file, int location)
{
    playerLock.lockForRead();

    if ((which >= (int)player.size()))
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            QString("GetPlayerReadLock(%1,%2,%3) returning NULL size(%4)")
                .arg(which).arg(file).arg(location).arg(player.size()));
        return NULL;
    }

    return (which < 0) ? player[playerActive] : player[which];
}

const PlayerContext *TV::GetPlayerReadLock(
    int which, const char *file, int location) const
{
    playerLock.lockForRead();

    if ((which >= (int)player.size()))
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            QString("GetPlayerReadLock(%1,%2,%3) returning NULL size(%4)")
                .arg(which).arg(file).arg(location).arg(player.size()));
        return NULL;
    }

    return (which < 0) ? player[playerActive] : player[which];
}

PlayerContext *TV::GetPlayerHaveLock(
    PlayerContext *locked_context,
    int which, const char *file, int location)
{
    if (!locked_context || (which >= (int)player.size()))
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            QString("GetPlayerHaveLock(0x%1,%2,%3,%4) returning NULL size(%5)")
                .arg((uint64_t)locked_context, 0, 16)
                .arg(which).arg(file).arg(location).arg(player.size()));
        return NULL;
    }

    return (which < 0) ? player[playerActive] : player[which];
}

const PlayerContext *TV::GetPlayerHaveLock(
    const PlayerContext *locked_context,
    int which, const char *file, int location) const
{
    if (!locked_context || (which >= (int)player.size()))
    {
        LOG(VB_GENERAL, LOG_WARNING, LOC +
            QString("GetPlayerHaveLock(0x%1,%2,%3,%4) returning NULL size(%5)")
                .arg((uint64_t)locked_context, 0, 16)
                .arg(which).arg(file).arg(location).arg(player.size()));
        return NULL;
    }

    return (which < 0) ? player[playerActive] : player[which];
}

void TV::ReturnPlayerLock(PlayerContext *&ctx)
{
    playerLock.unlock();
    ctx = NULL;
}

void TV::ReturnPlayerLock(const PlayerContext *&ctx) const
{
    playerLock.unlock();
    ctx = NULL;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */

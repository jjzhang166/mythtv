// -*- Mode: c++ -*-

#ifndef TVPLAY_H
#define TVPLAY_H

// C
#include <stdint.h>

// C++
#include <vector>
using namespace std;

// Qt
#include <QReadWriteLock>
#include <QWaitCondition>
#include <QStringList>
#include <QDateTime>
#include <QKeyEvent>
#include <QObject>
#include <QRegExp>
#include <QString>
#include <QEvent>
#include <QMutex>
#include <QHash>
#include <QTime>
#include <QMap>
#include <QSet>
#include <QString>

// MythTV
#include "mythdeque.h"
#include "tv.h"
#include "mythdate.h"
#include "programinfo.h"
#include "channelutil.h"
#include "videoouttypes.h"
#include "volumebase.h"
#include "inputinfo.h"
#include "channelgroup.h"
#include "mythtimer.h"
#include "osd.h"
#include "mythactions.h"

class QDateTime;
class OSD;
class RemoteEncoder;
class MythPlayer;
class DetectLetterbox;
class RingBuffer;
class ProgramInfo;
class MythDialog;
class PlayerContext;
class TvPlayWindow;
class TV;
class TVBrowseHelper;
class DDLoader;
struct osdInfo;

typedef QMap<QString,InfoMap>    DDValueMap;
typedef QMap<QString,DDValueMap> DDKeyMap;
typedef void (*EMBEDRETURNVOID) (void *, bool);
typedef void (*EMBEDRETURNVOIDEPG) (uint, const QString &, TV *, bool, bool, int);
typedef void (*EMBEDRETURNVOIDFINDER) (TV *, bool, bool);
typedef void (*EMBEDRETURNVOIDSCHEDIT) (const ProgramInfo *, void *);

// Locking order
//
// playerLock -> askAllowLock    -> osdLock
//            -> progListLock    -> osdLock
//            -> chanEditMapLock -> osdLock
//            -> lastProgramLock
//            -> is_tunable_cache_lock
//            -> recorderPlaybackInfoLock
//            -> timerIdLock
//            -> mainLoopCondLock
//            -> channelGroupLock
//
// When holding one of these locks, you may lock any lock of  the locks to
// the right of the current lock, but may not lock any lock to the left of
// this lock (which will cause a deadlock). Nor should you lock any other
// lock in the TV class without first adding it to the locking order list
// above.
//
// Note: Taking a middle lock such as askAllowLock, without taking a
// playerLock first does not violate these rules, but once you are
// holding it, you cannot later lock playerLock.
//
// It goes without saying that any locks outside of this class should only
// be taken one at a time, and should be taken last and released first of
// all the locks required for any algorithm. (Unless you use tryLock and
// release all locks if it can't gather them all at once, see the
// "multi_lock()" function as an example; but this is not efficient nor
// desirable and should be avoided when possible.)
//

enum scheduleEditTypes {
    kScheduleProgramGuide = 0,
    kScheduleProgramFinder,
    kScheduledRecording,
    kViewSchedule,
    kPlaybackBox
};

/**
 * Type of message displayed in ShowNoRecorderDialog()
 */
typedef enum
{
    kNoRecorders = 0,  ///< No free recorders
    kNoCurrRec = 1,    ///< No current recordings
    kNoTuners = 2,     ///< No capture cards configured
} NoRecorderMsg;

typedef enum
{
    kRewind = 4,
    kForward = 8,
    kSticky = 16,
    kSlippery = 32,
    kRelative = 64,
    kAbsolute = 128,
    kIgnoreCutlist = 256,
    kWhenceMask = 3,
    kNoSeekFlags = 0
} SeekFlags;

enum {
    kStartTVNoFlags          = 0x00,
    kStartTVInGuide          = 0x01,
    kStartTVInPlayList       = 0x02,
    kStartTVByNetworkCommand = 0x04,
    kStartTVIgnoreBookmark   = 0x08,
};

class AskProgramInfo
{
  public:
    AskProgramInfo() :
        has_rec(false),                has_later(false),
        is_in_same_input_group(false), is_conflicting(false),
        info(NULL) {}
    AskProgramInfo(const QDateTime &e, bool r, bool l, ProgramInfo *i) :
        expiry(e), has_rec(r), has_later(l),
        is_in_same_input_group(false), is_conflicting(false),
        info(i) {}

    QDateTime    expiry;
    bool         has_rec;
    bool         has_later;
    bool         is_in_same_input_group;
    bool         is_conflicting;
    ProgramInfo *info;
};

class MTV_PUBLIC TV : public QObject
{
    friend class PlaybackBox;
    friend class GuideGrid;
    friend class ProgFinder;
    friend class ViewScheduled;
    friend class ScheduleEditor;
    friend class TvPlayWindow;
    friend class TVBrowseHelper;
    friend class DDLoader;

    Q_OBJECT
  public:
    // Check whether we already have a TV object
    static bool    IsTVRunning(void);
    // Start media playback
    static bool    StartTV(ProgramInfo *tvrec = NULL,
                        uint flags = kStartTVNoFlags);

    // Public event handling
    bool event(QEvent *e);
    bool eventFilter(QObject *o, QEvent *e);

    // Public PlaybackBox methods
    /// true iff program is the same as the one in the selected player
    bool IsSameProgram(int player_idx, const ProgramInfo *p) const;

    // Public recorder methods
    void FinishRecording(int player_idx); ///< Finishes player's recording

    // static functions
    static void InitKeys(void);
    static void ReloadKeys(void);
    static void SetFuncPtr(const char *, void *);
    static int  ConfiguredTunerCards(void);

    /// \brief Helper class for Sleep Timer code.
    class SleepTimerInfo
    {
      public:
        SleepTimerInfo(QString str, unsigned long secs)
            : dispString(str), seconds(secs) { ; }
        QString   dispString;
        unsigned long seconds;
    };

    bool doNoIgnore(const QString &action);
    bool doNoIgnoreEsc(const QString &action);

    bool doEditEscape(const QString &action);
    bool doEditMenu(const QString &action);
    bool doEditSelect(const QString &action);

    bool doBrowseUp(const QString &action);
    bool doBrowseDown(const QString &action);
    bool doBrowseLeft(const QString &action);
    bool doBrowseRight(const QString &action);
    bool doBrowseNextFav(const QString &action);
    bool doBrowseSelect(const QString &action);
    bool doBrowseEscape(const QString &action);
    bool doBrowseToggleRecord(const QString &action);
    bool doBrowseDigit(const QString &action);

    bool doManualZoomUp(const QString &action);
    bool doManualZoomDown(const QString &action);
    bool doManualZoomLeft(const QString &action);
    bool doManualZoomRight(const QString &action);
    bool doManualZoomAspectUp(const QString &action);
    bool doManualZoomAspectDown(const QString &action);
    bool doManualZoomEscape(const QString &action);
    bool doManualZoomSelect(const QString &action);
    bool doManualZoomIn(const QString &action);
    bool doManualZoomOut(const QString &action);

    bool doPicAttrLeft(const QString &action);
    bool doPicAttrRight(const QString &action);

    bool doTimeStretchLeft(const QString &action);
    bool doTimeStretchRight(const QString &action);
    bool doTimeStretchDown(const QString &action);
    bool doTimeStretchUp(const QString &action);
    bool doTimeStretchAdjStretch(const QString &action);

    bool doAudioSyncLeft(const QString &action);
    bool doAudioSyncRight(const QString &action);
    bool doAudioSyncUp(const QString &action);
    bool doAudioSyncDown(const QString &action);
    bool doAudioSyncToggle(const QString &action);

    bool do3dSideBySide(const QString &action);
    bool do3dSideBySideDiscard(const QString &action);
    bool do3dTopAndBottom(const QString &action);
    bool do3dTopAndBottomDiscard(const QString &action);

    bool doActiveSkipCommercial(const QString &action);
    bool doActiveSkipCommBack(const QString &action);
    bool doActiveQueueTranscode(const QString &action);
    bool doActiveQueueTranscodeAuto(const QString &action);
    bool doActiveQueueTranscodeHigh(const QString &action);
    bool doActiveQueueTranscodeMed(const QString &action);
    bool doActiveQueueTranscodeLow(const QString &action);
    bool doActivePlay(const QString &action);
    bool doActivePause(const QString &action);
    bool doActiveSpeedInc(const QString &action);
    bool doActiveSpeedDec(const QString &action);
    bool doActiveAdjStretch(const QString &action);
    bool doActiveCycleCommSkipMode(const QString &action);
    bool doActiveNextScan(const QString &action);
    bool doActiveSeekArb(const QString &action);
    bool doActiveJumpRwnd(const QString &action);
    bool doActiveJumpFfwd(const QString &action);
    bool doActiveJumpBkmrk(const QString &action);
    bool doActiveJumpStart(const QString &action);
    bool doActiveClearOsd(const QString &action);
    bool doActiveViewScheduled(const QString &action);
    bool doActiveSignalMon(const QString &action);
    bool doActiveScreenshot(const QString &action);
    bool doActiveStop(const QString &action);
    bool doActiveExitNoPrompt(const QString &action);
    bool doActiveEscape(const QString &action);
    bool doActiveEnableUpmix(const QString &action);
    bool doActiveDisableUpmix(const QString &action);
    bool doActiveVolDown(const QString &action);
    bool doActiveVolUp(const QString &action);
    bool doActiveCycleAudioChan(const QString &action);
    bool doActiveMuteAudio(const QString &action);
    bool doActiveStretchInc(const QString &action);
    bool doActiveStretchDec(const QString &action);
    bool doActiveMenu(const QString &action);
    bool doActiveInfo(const QString &action);
    bool doActiveToggleOsdDebug(const QString &action);

    bool doJumpProgPrevChan(const QString &action);
    bool doJumpProgProgram(const QString &action);
    bool doJumpProgRecord(const QString &action);

    bool doSeekFfwd(const QString &action);
    bool doSeekFfwdSticky(const QString &action);
    bool doSeekRight(const QString &action);
    bool doSeekRwnd(const QString &action);
    bool doSeekRwndSticky(const QString &action);
    bool doSeekLeft(const QString &action);

    bool doTrackToggleExtText(const QString &action);
    bool doTrackEnableExtText(const QString &action);
    bool doTrackDisableExtText(const QString &action);
    bool doTrackEnableForcedSubs(const QString &action);
    bool doTrackDisableForcedSubs(const QString &action);
    bool doTrackEnableSubs(const QString &action);
    bool doTrackDisableSubs(const QString &action);
    bool doTrackToggleSubs(const QString &action);
    bool doTrackToggle(const QString &action);
    bool doTrackSelect(const QString &action);
    bool doTrackNextPrev(const QString &action);

    bool doFFRwndDigit(const QString &action);

    bool doToggleAspect(const QString &action);
    bool doToggleFill(const QString &action);
    bool doToggleAudioSync(const QString &action);
    bool doToggleVisualisation(const QString &action);
    bool doToggleEnableVis(const QString &action);
    bool doToggleDisableVis(const QString &action);
    bool doTogglePicControls(const QString &action);
    bool doToggleStudioLevels(const QString &action);
    bool doToggleNightMode(const QString &action);
    bool doToggleStretch(const QString &action);
    bool doToggleUpMix(const QString &action);
    bool doToggleSleep(const QString &action);
    bool doToggleRecord(const QString &action);
    bool doToggleFav(const QString &action);
    bool doToggleChanControls(const QString &action);
    bool doToggleRecControls(const QString &action);
    bool doToggleInputs(const QString &action);
    bool doToggleBrowse(const QString &action);
    bool doToggleEdit(const QString &action);

    bool doPipEnqueueAction(const QString &action);
    bool doPipNextPipWindow(const QString &action);

    bool doPostQSelect(const QString &action);
    bool doPostQNextFav(const QString &action);
    bool doPostQNextSource(const QString &action);
    bool doPostQPrevSource(const QString &action);
    bool doPostQNextInput(const QString &action);
    bool doPostQNextCard(const QString &action);
    bool doPostQGuide(const QString &action);
    bool doPostQPrevChan(const QString &action);
    bool doPostQChanUp(const QString &action);
    bool doPostQChanDown(const QString &action);
    bool doPostQDelete(const QString &action);
    bool doPostQDVDRootMenu(const QString &action);
    bool doPostQPopupMenu(const QString &action);
    bool doPostQFinder(const QString &action);

    bool doOSDDialog(const QString &action);
    bool doOSDPause(const QString &action);
    bool doOSDStop(const QString &action);
    bool doOSDFfwd(const QString &action);
    bool doOSDRwnd(const QString &action);
    bool doOSDDeinterlace(const QString &action);
    bool doOSDToggleDebug(const QString &action);
    bool doOSDToggleManualZoom(const QString &action);
    bool doOSDToggleStretch(const QString &action);
    bool doOSDEnableUpmix(const QString &action);
    bool doOSDDisableUpmix(const QString &action);
    bool doOSDAdjustStretch(const QString &action);
    bool doOSDSelectScan(const QString &action);
    bool doOSDToggleAudioSync(const QString &action);
    bool doOSDToggleVis(const QString &action);
    bool doOSDEnableVis(const QString &action);
    bool doOSDDisableVis(const QString &action);
    bool doOSDToggleSleep(const QString &action);
    bool doOSDTogglePicControls(const QString &action);
    bool doOSDToggleStudioLevels(const QString &action);
    bool doOSDToggleNightMode(const QString &action);
    bool doOSDToggleAspect(const QString &action);
    bool doOSDToggleFill(const QString &action);
    bool doOSDAutodetectFill(const QString &action);
    bool doOSDGuide(const QString &action);
    bool doOSDChangroup(const QString &action);
    bool doOSDFinder(const QString &action);
    bool doOSDSchedule(const QString &action);
    bool doOSDViewScheduled(const QString &action);
    bool doOSDVisualiser(const QString &action);
    bool doOSD3d(const QString &action);

    bool doOSDDialogMenu(const QString &action);
    bool doOSDDialogRecord(const QString &action);
    bool doOSDDialogExit(const QString &action);
    bool doOSDDialogSleep(const QString &action);
    bool doOSDDialogIdle(const QString &action);
    bool doOSDDialogInfo(const QString &action);
    bool doOSDDialogEditing(const QString &action);
    bool doOSDDialogAskAllow(const QString &action);
    bool doOSDDialogEditor(const QString &action);
    bool doOSDDialogCutpoint(const QString &action);
    bool doOSDDialogDelete(const QString &action);
    bool doOSDDialogPlay(const QString &action);
    bool doOSDDialogConfirm(const QString &action);

    bool doOSDLiveTVToggleBrowse(const QString &action);
    bool doOSDLiveTVPrevChan(const QString &action);
    bool doOSDLiveTVSwitchInput(const QString &action);
    bool doOSDLiveTVEdit(const QString &action);

    bool doOSDPlayingDVDMenu(const QString &action);
    bool doOSDPlayingJumpChapter(const QString &action);
    bool doOSDPlayingSwitchTitle(const QString &action);
    bool doOSDPlayingSwitchAngle(const QString &action);
    bool doOSDPlayingEdit(const QString &action);
    bool doOSDPlayingToggleAutoexpire(const QString &action);
    bool doOSDPlayingToggleCommSkip(const QString &action);
    bool doOSDPlayingTranscode(const QString &action);
    bool doOSDPlayingTranscodeAuto(const QString &action);
    bool doOSDPlayingTranscodeHigh(const QString &action);
    bool doOSDPlayingTranscodeMed(const QString &action);
    bool doOSDPlayingTranscodeLow(const QString &action);

    bool doOSDAskAllowCancelRecord(const QString &action);
    bool doOSDAskAllowCancelConflict(const QString &action);
    bool doOSDAskAllowWatch(const QString &action);
    bool doOSDAskAllowExit(const QString &action);

    bool doNCChanId(const QString &action);
    bool doNCChannel(const QString &action);
    bool doNCSpeed(const QString &action);
    bool doNCStop(const QString &action);
    bool doNCSeek(const QString &action);
    bool doNCVolume(const QString &action);
    bool doNCQuery(const QString &action);

    bool doMythEventSetVolume(const QString &action);
    bool doMythEventSetAudioSync(const QString &action);
    bool doMythEventSetBrightness(const QString &action);
    bool doMythEventSetContrast(const QString &action);
    bool doMythEventSetColor(const QString &action);
    bool doMythEventSetHue(const QString &action);
    bool doMythEventJumpChapter(const QString &action);
    bool doMythEventSwitchTitle(const QString &action);
    bool doMythEventSwitchAngle(const QString &action);
    bool doMythEventSeekAbsolute(const QString &action);
    bool doMythEventScreenshot(const QString &action);
    bool doMythEventGetStatus(const QString &action);
    bool doMythEventDoneRecording(const QString &action);
    bool doMythEventAskRecording(const QString &action);
    bool doMythEventQuitLiveTV(const QString &action);
    bool doMythEventLiveTVWatch(const QString &action);
    bool doMythEventLiveTVChain(const QString &action);
    bool doMythEventExitToMenu(const QString &action);
    bool doMythEventSignal(const QString &action);
    bool doMythEventNetworkControl(const QString &action);
    bool doMythEventStartEPG(const QString &action);
    bool doMythEventExiting(const QString &action);
    bool doMythEventCommflagStart(const QString &action);
    bool doMythEventCommflagUpdate(const QString &action);

    bool doOSDVideoExitDelRecord(const QString &action);
    bool doOSDVideoExitDelete(const QString &action);
    bool doOSDVideoExitConfDelete(const QString &action);
    bool doOSDVideoExitSaveExit(const QString &action);
    bool doOSDVideoExitKeepWatching(const QString &action);

    bool doOSDChanEditProbe(const QString &action);
    bool doOSDChanEditOk(const QString &action);
    bool doOSDChanEditQuit(const QString &action);

  public slots:
    void HandleOSDClosed(int osdType);
    void timerEvent(QTimerEvent*);

  protected:
    // Protected event handling
    void customEvent(QEvent *e);

    static QStringList lastProgramStringList;
    static EMBEDRETURNVOID RunPlaybackBoxPtr;
    static EMBEDRETURNVOID RunViewScheduledPtr;
    static EMBEDRETURNVOIDEPG RunProgramGuidePtr;
    static EMBEDRETURNVOIDFINDER RunProgramFinderPtr;
    static EMBEDRETURNVOIDSCHEDIT RunScheduleEditorPtr;

  private:
    TV();
   ~TV();
    static TV*     GetTV(void);
    static void    ReleaseTV(TV* tv);
    static QMutex *gTVLock;
    static TV     *gTV;

    // Private initialisation
    bool Init(bool createWindow = true);
    void InitFromDB(void);

    // Top level playback methods
    bool LiveTV(bool showDialogs = true);
    bool StartLiveTVInGuide(void) { return db_start_in_guide; }
    int  Playback(const ProgramInfo &rcinfo);
    void PlaybackLoop(void);

    // Private event handling
    bool ProcessKeypress(PlayerContext*, QKeyEvent *e);
    void ProcessNetworkControlCommand(PlayerContext *, const QString &command);

    bool HandleTrackAction(PlayerContext*, const QStringList &actions);
    bool ActiveHandleAction(PlayerContext*, const QStringList &actions);
    bool BrowseHandleAction(PlayerContext*, const QStringList &actions);
    void OSDDialogEvent(int result, const QString &text, const QString &action);
    bool PxPHandleAction(PlayerContext*,const QStringList &actions);
    bool ToggleHandleAction(PlayerContext*, const QStringList &actions);
    bool FFRewHandleAction(PlayerContext*, const QStringList &actions);
    bool ActivePostQHandleAction(PlayerContext*, const QStringList &actions);
    bool HandleJumpToProgramAction(PlayerContext*, const QStringList &actions);
    bool SeekHandleAction(PlayerContext *actx, const QStringList &actions);
    bool TimeStretchHandleAction(PlayerContext*, const QStringList &actions);
    bool DiscMenuHandleAction(PlayerContext*, const QStringList &actions);
    bool Handle3D(PlayerContext *ctx, const QString &action);

    // Timers and timer events
    int  StartTimer(int interval, int line);
    void KillTimer(int id);

    void SetSpeedChangeTimer(uint when, int line);
    void HandleEndOfPlaybackTimerEvent(void);
    void HandleIsNearEndWhenEmbeddingTimerEvent(void);
    void HandleEndOfRecordingExitPromptTimerEvent(void);
    void HandleVideoExitDialogTimerEvent(void);
    void HandlePseudoLiveTVTimerEvent(void);
    void HandleSpeedChangeTimerEvent(void);
    void ToggleSleepTimer(const PlayerContext*);
    void ToggleSleepTimer(const PlayerContext*, const QString &time);
    bool HandlePxPTimerEvent(void);
    bool HandleLCDTimerEvent(void);
    void HandleLCDVolumeTimerEvent(void);

    // Commands used by frontend UI screens (PlaybackBox, GuideGrid etc)
    void EditSchedule(const PlayerContext*,
                      int editType = kScheduleProgramGuide);
    bool StartEmbedding(const QRect&);
    void StopEmbedding(void);
    bool IsTunable(const PlayerContext*, uint chanid, bool use_cache = false);
    QSet<uint> IsTunableOn(const PlayerContext*, uint chanid,
                           bool use_cache, bool early_exit);
    void ClearTunableCache(void);
    void ChangeChannel(const PlayerContext*, const DBChanList &options);
    void DrawUnusedRects(void);
    void DoEditSchedule(int editType = kScheduleProgramGuide);
    QString GetRecordingGroup(int player_idx) const;
    void ChangeVolume(PlayerContext*, bool up, int newvolume = -1);
    void ToggleMute(PlayerContext*, const bool muteIndividualChannels = false);
    void UpdateChannelList(int groupID);

    // Lock handling
    OSD *GetOSDL(const char *, int);
    OSD *GetOSDL(const PlayerContext*,const char *, int);
    void ReturnOSDLock(const PlayerContext*,OSD*&);
    PlayerContext       *GetPlayerWriteLock(
        int which, const char *file, int location);
    PlayerContext       *GetPlayerReadLock(
        int which, const char *file, int location);
    const PlayerContext *GetPlayerReadLock(
        int which, const char *file, int location) const;
    PlayerContext       *GetPlayerHaveLock(
        PlayerContext*,
        int which, const char *file, int location);
    const PlayerContext *GetPlayerHaveLock(
        const PlayerContext*,
        int which, const char *file, int location) const;
    void ReturnPlayerLock(PlayerContext*&);
    void ReturnPlayerLock(const PlayerContext*&) const;

    // Other toggles
    void ToggleAutoExpire(PlayerContext*);
    void ToggleRecord(PlayerContext*);

    // General TV state
    static bool StateIsRecording(TVState state);
    static bool StateIsPlaying(TVState state);
    static bool StateIsLiveTV(TVState state);

    TVState GetState(int player_idx) const;
    TVState GetState(const PlayerContext*) const;
    void HandleStateChange(PlayerContext *mctx, PlayerContext *ctx);
    void GetStatus(void);
    void ForceNextStateNone(PlayerContext*);
    void ScheduleStateChange(PlayerContext*);
    void SetErrored(PlayerContext*);
    void setInPlayList(bool setting) { inPlaylist = setting; }
    void setUnderNetworkControl(bool setting) { underNetworkControl = setting; }
    void PrepToSwitchToRecordedProgram(PlayerContext*,
                                       const ProgramInfo &);
    enum BookmarkAction {
        kBookmarkAlways,
        kBookmarkNever,
        kBookmarkAuto // set iff db_playback_exit_prompt==2
    };
    void PrepareToExitPlayer(PlayerContext*, int line,
                             BookmarkAction bookmark = kBookmarkAuto);
    void SetExitPlayer(bool set_it, bool wants_to);

    bool RequestNextRecorder(PlayerContext *, bool);
    void DeleteRecorder();

    bool StartRecorder(PlayerContext *ctx, int maxWait=-1);
    void StopStuff(PlayerContext *mctx, PlayerContext *ctx,
                   bool stopRingbuffers, bool stopPlayers, bool stopRecorders);
    void TeardownPlayer(PlayerContext *mctx, PlayerContext *ctx);


    bool StartPlayer(PlayerContext *mctx, PlayerContext *ctx,
                     TVState desiredState);

    vector<long long> TeardownAllPlayers(PlayerContext*);
    void RestartAllPlayers(PlayerContext *lctx,
                           const vector<long long> &pos,
                           MuteState mctx_mute);
    void RestartMainPlayer(PlayerContext *mctx);

    /// Returns true if we are currently in the process of switching recorders.
    bool IsSwitchingCards(void)  const { return switchToRec; }
    /// Returns true if the user told Mythtv to allow re-recording of the show
    bool getAllowRerecord(void) const { return allowRerecord;  }
    /// This is set to true if the player reaches the end of the
    /// recording without the user explicitly exiting the player.
    bool getEndOfRecording(void) const { return endOfRecording; }
    /// This is set if the user asked MythTV to jump to the previous
    /// recording in the playlist.
    bool getJumpToProgram(void)  const { return jumpToProgram; }
    bool IsDeleteAllowed(const PlayerContext*) const;

    // Channels
    void ToggleChannelFavorite(PlayerContext *ctx);
    void ToggleChannelFavorite(PlayerContext*, const QString &);
    void ChangeChannel(PlayerContext*, int direction);
    void ChangeChannel(PlayerContext*, uint chanid, const QString &channum);

    void ShowPreviousChannel(PlayerContext*);
    void PopPreviousChannel(PlayerContext*, bool immediate_change);

    // key queue commands
    void AddKeyToInputQueue(PlayerContext*, char key);
    void ClearInputQueues(const PlayerContext*, bool hideosd);
    bool CommitQueuedInput(PlayerContext*);
    bool ProcessSmartChannel(const PlayerContext*, QString&);

    // query key queues
    bool HasQueuedInput(void) const
        { return !GetQueuedInput().isEmpty(); }
    bool HasQueuedChannel(void) const
        { return queuedChanID || !GetQueuedChanNum().isEmpty(); }

    // get queued up input
    QString GetQueuedInput(void)   const;
    int     GetQueuedInputAsInt(bool *ok = NULL, int base = 10) const;
    QString GetQueuedChanNum(void) const;
    uint    GetQueuedChanID(void)  const { return queuedChanID; }

    // Source and input
    void SwitchSource(PlayerContext*, uint source_direction);
    void SwitchInputs(PlayerContext*, uint inputid);
    void ToggleInputs(PlayerContext*, uint inputid = 0);
    void SwitchCards(PlayerContext*,
                     uint chanid = 0, QString channum = "", uint inputid = 0);

    // Pause/play
    void PauseLiveTV(PlayerContext*);
    void UnpauseLiveTV(PlayerContext*, bool bQuietly = false);
    void DoPlay(PlayerContext*);
    float DoTogglePauseStart(PlayerContext*);
    void DoTogglePauseFinish(PlayerContext*, float time, bool showOSD);
    void DoTogglePause(PlayerContext*, bool showOSD);
    vector<bool> DoSetPauseState(PlayerContext *lctx, const vector<bool>&);
    bool ContextIsPaused(PlayerContext *ctx, const char *file, int location);

    // Program jumping stuff
    void SetLastProgram(const ProgramInfo *rcinfo);
    ProgramInfo *GetLastProgram(void) const;

    // Seek, skip, jump, speed
    void DoSeek(PlayerContext*, float time, const QString &mesg,
                bool timeIsOffset, bool honorCutlist);
    bool DoPlayerSeek(PlayerContext*, float time);
    enum ArbSeekWhence {
        ARBSEEK_SET = 0,
        ARBSEEK_REWIND,
        ARBSEEK_FORWARD,
        ARBSEEK_END
    };
    void DoSeekAbsolute(PlayerContext *ctx, long long seconds, bool honorCutlist);
    void DoArbSeek(PlayerContext*, ArbSeekWhence whence, bool honorCutlist);
    void DoJumpFFWD(PlayerContext *ctx);
    void DoJumpRWND(PlayerContext *ctx);
    void NormalSpeed(PlayerContext*);
    void ChangeSpeed(PlayerContext*, int direction);
    void ToggleTimeStretch(PlayerContext*);
    void ChangeTimeStretch(PlayerContext*, int dir, bool allowEdit = true);
    void DVDJumpBack(PlayerContext*);
    void DVDJumpForward(PlayerContext*);
    float StopFFRew(PlayerContext*);
    void ChangeFFRew(PlayerContext*, int direction);
    void SetFFRew(PlayerContext*, int index);

    // Private audio methods
    void EnableUpmix(PlayerContext*, bool enable, bool toggle = false);
    void ChangeAudioSync(PlayerContext*, int dir, int newsync = -9999);
    bool AudioSyncHandleAction(PlayerContext*, const QStringList &actions);
    void PauseAudioUntilBuffered(PlayerContext *ctx);

    // Chapters, titles and angles
    int  GetNumChapters(const PlayerContext*) const;
    void GetChapterTimes(const PlayerContext*, QList<long long> &times) const;
    int  GetCurrentChapter(const PlayerContext*) const;
    int  GetNumTitles(const PlayerContext *ctx) const;
    int  GetCurrentTitle(const PlayerContext *ctx) const;
    int  GetTitleDuration(const PlayerContext *ctx, int title) const;
    QString GetTitleName(const PlayerContext *ctx, int title) const;
    void DoSwitchTitle(PlayerContext*, int title);
    int  GetNumAngles(const PlayerContext *ctx) const;
    int  GetCurrentAngle(const PlayerContext *ctx) const;
    QString GetAngleName(const PlayerContext *ctx, int angle) const;
    void DoSwitchAngle(PlayerContext*, int angle);
    void DoJumpChapter(PlayerContext*, int chapter);

    // Commercial skipping
    void DoSkipCommercials(PlayerContext*, int direction);
    void SetAutoCommercialSkip(const PlayerContext*,
                               CommSkipMode skipMode = kCommSkipOff);

    // Transcode
    void DoQueueTranscode(PlayerContext*, const QString &profile);

    // Bookmarks
    bool IsBookmarkAllowed(const PlayerContext*) const;
    void SetBookmark(PlayerContext* ctx, bool clear = false);

    // OSD
    bool ClearOSD(const PlayerContext*);
    void ToggleOSD(PlayerContext*, bool includeStatusOSD);
    void ToggleOSDDebug(PlayerContext*);
    void UpdateOSDDebug(const PlayerContext *ctx);
    void UpdateOSDProgInfo(const PlayerContext*, const char *whichInfo);
    void UpdateOSDStatus(const PlayerContext *ctx, QString title, QString desc,
                         QString value, int type, QString units,
                         int position = 0,
                         enum OSDTimeout timeout = kOSDTimeout_Med);
    void UpdateOSDStatus(const PlayerContext *ctx, osdInfo &info,
                         int type, enum OSDTimeout timeout);

    void UpdateOSDSeekMessage(const PlayerContext*,
                              const QString &mesg, enum OSDTimeout timeout);
    void UpdateOSDInput(const PlayerContext*, QString inputname = QString(""));
    void UpdateOSDSignal(const PlayerContext*, const QStringList &strlist);
    void UpdateOSDTimeoutMessage(PlayerContext*);
    void UpdateOSDAskAllowDialog(PlayerContext*);
    void SetUpdateOSDPosition(bool set_it);

    // Captions/subtitles
    bool SubtitleZoomHandleAction(PlayerContext *ctx,
                                  const QStringList &actions);
    void ChangeSubtitleZoom(PlayerContext *ctx, int dir);

    // PxP handling
    bool CreatePBP(PlayerContext *lctx, const ProgramInfo *info);
    bool CreatePIP(PlayerContext *lctx, const ProgramInfo *info);
    bool ResizePIPWindow(PlayerContext*);
    bool IsPBPSupported(const PlayerContext *ctx = NULL) const;
    bool IsPIPSupported(const PlayerContext *ctx = NULL) const;
    void PxPToggleView(  PlayerContext *actx, bool wantPBP);
    void PxPCreateView(  PlayerContext *actx, bool wantPBP);
    void PxPTeardownView(PlayerContext *actx);
    void PxPToggleType(  PlayerContext *mctx, bool wantPBP);
    void PxPSwap(        PlayerContext *mctx, PlayerContext *pipctx);
    bool PIPAddPlayer(   PlayerContext *mctx, PlayerContext *ctx);
    bool PIPRemovePlayer(PlayerContext *mctx, PlayerContext *ctx);
    void PBPRestartMainPlayer(PlayerContext *mctx);
    void SetActive(PlayerContext *lctx, int index, bool osd_msg);

    // Video controls
    void ToggleAspectOverride(PlayerContext*,
                              AspectOverrideMode aspectMode = kAspect_Toggle);
    void ToggleAdjustFill(PlayerContext*,
                          AdjustFillMode adjustfillMode = kAdjustFill_Toggle);
    void DoToggleStudioLevels(const PlayerContext *ctx);
    void DoToggleNightMode(const PlayerContext*);
    void DoTogglePictureAttribute(const PlayerContext*,
                                  PictureAdjustType type);
    void DoChangePictureAttribute(
        PlayerContext*,
        PictureAdjustType type, PictureAttribute attr,
        bool up, int newvalue = -1);
    bool PictureAttributeHandleAction(PlayerContext*,
                                      const QStringList &actions);
    static PictureAttribute NextPictureAdjustType(
        PictureAdjustType type, MythPlayer *mp, PictureAttribute attr);
    void HandleDeinterlacer(PlayerContext* ctx, const QString &action);

    // Sundry on screen
    void ITVRestart(PlayerContext*, bool isLive);
    void EnableVisualisation(const PlayerContext*, bool enable,
                             bool toggle = false,
                             const QString &action = QString(""));

    // Manual zoom mode
    void SetManualZoom(const PlayerContext *, bool enabled, const QString &msg);
    bool ManualZoomHandleAction(PlayerContext *actx,
                                const QStringList &actions);

    // Channel editing support
    void StartChannelEditMode(PlayerContext*);
    bool HandleOSDChannelEdit(PlayerContext*, const QString &action);
    void ChannelEditAutoFill(const PlayerContext*, InfoMap&) const;
    void ChannelEditAutoFill(const PlayerContext*, InfoMap&,
                             const QMap<QString,bool>&) const;
    void ChannelEditXDSFill(const PlayerContext*, InfoMap&) const;
    void ChannelEditDDFill(InfoMap&, const QMap<QString,bool>&, bool) const;
    QString GetDataDirect(QString key,   QString value,
                          QString field, bool    allow_partial = false) const;
    bool LoadDDMap(uint sourceid);
    void RunLoadDDMap(uint sourceid);

    // General dialog handling
    bool DialogIsVisible(PlayerContext *ctx, const QString &dialog);
    void HandleOSDInfo(PlayerContext *ctx, const QString &action);
    void ShowNoRecorderDialog(const PlayerContext*,
                              NoRecorderMsg msgType = kNoRecorders);

    // AskAllow dialog handling
    void ShowOSDAskAllow(PlayerContext *ctx);
    void HandleOSDAskAllow(PlayerContext *ctx, const QString &action);
    void AskAllowRecording(PlayerContext*, const QStringList&, int, bool, bool);

    // Program editing support
    void ShowOSDCutpoint(PlayerContext *ctx, const QString &type);
    bool HandleOSDCutpoint(PlayerContext *ctx, const QString &action,
                           long long frame);
    void StartProgramEditMode(PlayerContext*);

    // Already editing dialog
    void ShowOSDAlreadyEditing(PlayerContext *ctx);
    void HandleOSDAlreadyEditing(PlayerContext *ctx, const QString &action,
                                 bool was_paused);

    // Sleep dialog handling
    void ShowOSDSleep(void);
    void HandleOSDSleep(PlayerContext *ctx, const QString &action);
    void SleepDialogTimeout(void);

    // Idle dialog handling
    void ShowOSDIdle(void);
    void HandleOSDIdle(PlayerContext *ctx, const QString &action);
    void IdleDialogTimeout(void);

    // Exit/delete dialog handling
    void ShowOSDStopWatchingRecording(PlayerContext *ctx);
    void ShowOSDPromptDeleteRecording(PlayerContext *ctx, const QString &title,
                                      bool force = false);
    bool HandleOSDVideoExit(PlayerContext *ctx, const QString &action);

    // Menu dialog
    void ShowOSDMenu(const PlayerContext*, const QString category = "",
                     const QString selected = "");

    void FillOSDMenuAudio    (const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction);
    void FillOSDMenuVideo    (const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction);
    void FillOSDMenuSubtitles(const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction);
    void FillOSDMenuNavigate (const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction);
    void FillOSDMenuJobs     (const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction);
    void FillOSDMenuPlayback (const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction);
    void FillOSDMenuSchedule (const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction);
    void FillOSDMenuSource   (const PlayerContext *ctx, OSD *osd,
                              QString category, const QString selected,
                              QString &currenttext, QString &backaction);
    void FillOSDMenuJumpRec  (PlayerContext* ctx, const QString category = "",
                              int level = 0, const QString selected = "");

    // LCD
    void UpdateLCD(void);
    void ShowLCDChannelInfo(const PlayerContext*);
    void ShowLCDDVDInfo(const PlayerContext*);

    // Other stuff
    int GetLastRecorderNum(int player_idx) const;
    static QStringList GetValidRecorderList(uint chanid);
    static QStringList GetValidRecorderList(const QString &channum);
    static QStringList GetValidRecorderList(uint, const QString&);

    static TVState RemoveRecording(TVState state);
    void RestoreScreenSaver(const PlayerContext*);

    // for temp debugging only..
    int find_player_index(const PlayerContext*) const;

  private:
    // Configuration variables from database
    QString baseFilters;
    QString db_channel_format;
    uint    db_idle_timeout;
    int     db_playback_exit_prompt;
    uint    db_autoexpire_default;
    bool    db_auto_set_watched;
    bool    db_end_of_rec_exit_prompt;
    bool    db_jump_prefer_osd;
    bool    db_use_gui_size_for_tv;
    bool    db_start_in_guide;
    bool    db_toggle_bookmark;
    bool    db_run_jobs_on_remote;
    bool    db_continue_embedded;
    bool    db_use_fixed_size;
    bool    db_browse_always;
    bool    db_browse_all_tuners;
    bool    db_use_channel_groups;
    bool    db_remember_last_channel_group;
    ChannelGroupList db_channel_groups;

    CommSkipMode autoCommercialSkip;
    bool    tryUnflaggedSkip;

    bool    smartForward;
    float   ff_rew_repos;
    bool    ff_rew_reverse;
    bool    jumped_back; ///< Used by PromptDeleteRecording
    vector<int> ff_rew_speeds;

    uint    vbimode;

    QTime ctorTime;
    uint switchToInputId;

    QMutex         initFromDBLock;
    bool           initFromDBDone;
    QWaitCondition initFromDBWait;

    /// True if the user told MythTV to stop plaback. If this is false
    /// when we exit the player, we display an error screen.
    mutable bool wantsToQuit;
    bool stretchAdjustment; ///< True if time stretch is turned on
    bool audiosyncAdjustment; ///< True if audiosync is turned on
    bool subtitleZoomAdjustment; ///< True if subtitle zoom is turned on
    bool editmode;          ///< Are we in video editing mode
    bool zoomMode;
    bool sigMonMode;     ///< Are we in signal monitoring mode?
    bool endOfRecording; ///< !player->IsPlaying() && StateIsPlaying()
    bool requestDelete;  ///< User wants last video deleted
    bool allowRerecord;  ///< User wants to rerecord the last video if deleted
    bool doSmartForward;
    bool queuedTranscode;
    /// Picture attribute type to modify.
    PictureAdjustType adjustingPicture;
    /// Picture attribute to modify (on arrow left or right)
    PictureAttribute  adjustingPictureAttribute;

    // Ask Allow state
    QMap<QString,AskProgramInfo> askAllowPrograms;
    QMutex                       askAllowLock;

    MythDeque<QString> changePxP;
    QMutex progListsLock;
    QMap<QString,ProgramList> progLists;

    mutable QMutex chanEditMapLock; ///< Lock for chanEditMap and ddMap
    InfoMap   chanEditMap;          ///< Channel Editing initial map

    DDKeyMap  ddMap;                ///< DataDirect channel map
    uint      ddMapSourceId;        ///< DataDirect channel map sourceid
    DDLoader *ddMapLoader;          ///< DataDirect map loader runnable

    /// Vector or sleep timer sleep times in seconds,
    /// with the appropriate UI message.
    vector<SleepTimerInfo> sleep_times;
    uint      sleep_index;          ///< Index into sleep_times.
    uint      sleepTimerTimeout;    ///< Current sleep timeout in msec
    int       sleepTimerId;         ///< Timer for turning off playback.
    int       sleepDialogTimerId;   ///< Timer for sleep dialog.
    /// Timer for turning off playback after idle period.
    int       idleTimerId;
    int       idleDialogTimerId; ///< Timer for idle dialog.

    /// Queue of unprocessed key presses.
    MythTimer keyRepeatTimer; ///< Timeout timer for repeat key filtering

    // CC/Teletex input state variables
    /// Are we in CC/Teletext page/stream selection mode?
    bool  ccInputMode;

    // Arbitrary Seek input state variables
    /// Are we in Arbitrary seek input mode?
    bool  asInputMode;

    // Channel changing state variables
    /// Input key presses queued up so far...
    QString         queuedInput;
    /// Input key presses queued up so far to form a valid ChanNum
    mutable QString queuedChanNum;
    /// Queued ChanID (from EPG channel selector)
    uint            queuedChanID;

    // Channel changing timeout notification variables
    QTime   lockTimer;
    bool    lockTimerOn;
    QDateTime lastLockSeenTime;

    // Channel browsing state variables
    TVBrowseHelper *browsehelper;

    // Program Info for currently playing video
    // (or next video if InChangeState() is true)
    mutable QMutex lastProgramLock;
    ProgramInfo *lastProgram;   ///< last program played with this player
    bool         inPlaylist; ///< show is part of a playlist
    bool         underNetworkControl; ///< initial show started via by the network control interface

    // Program Jumping
    PIPState     jumpToProgramPIPState;
    bool         jumpToProgram;

    // Video Players
    vector<PlayerContext*>  player;
    /// Video Player to which events are sent to
    int                     playerActive;
    /// lock on player and playerActive changes
    mutable QReadWriteLock  playerLock;

    bool noHardwareDecoders;

    // Remote Encoders
    /// Main recorder to use after a successful SwitchCards() call.
    RemoteEncoder *switchToRec;

    // OSD info
    QMap<OSD*,const PlayerContext*> osd_lctx;

    // LCD Info
    QString   lcdTitle;
    QString   lcdSubtitle;
    QString   lcdCallsign;

    // Window info (GUI is optional, transcoding, preview img, etc)
    TvPlayWindow *myWindow;   ///< Our screen, if it exists
    ///< player bounds, for after DoEditSchedule() returns to normal playing.
    QRect player_bounds;
    ///< Prior GUI window bounds, for DoEditSchedule() and player exit().
    QRect saved_gui_bounds;
    /// true if this instance disabled MythUI drawing.
    bool  weDisabledGUI;
    /// true if video chromakey and frame should not be drawn
    bool  disableDrawUnusedRects;

    // embedded status
    bool         isEmbedded;       ///< are we currently embedded
    bool         ignoreKeyPresses; ///< should we ignore keypresses
    vector<bool> saved_pause;      ///< saved pause state before embedding

    // IsTunable() cache, used by embedded program guide
    mutable QMutex                 is_tunable_cache_lock;
    QMap< uint,vector<InputInfo> > is_tunable_cache_inputs;

    // Channel group stuff
    /// \brief Lock necessary when modifying channel group variables.
    /// These are only modified in UI thread, so no lock is needed
    /// to read this value in the UI thread.
    mutable QMutex channelGroupLock;
    volatile int   channelGroupId;
    DBChanList     channelGroupChannelList;

    // Network Control stuff
    MythDeque<QString> networkControlCommands;

    // Timers
    typedef QMap<int,PlayerContext*>       TimerContextMap;
    typedef QMap<int,const PlayerContext*> TimerContextConstMap;
    mutable QMutex       timerIdLock;
    volatile int         lcdTimerId;
    volatile int         lcdVolumeTimerId;
    volatile int         networkControlTimerId;
    volatile int         jumpMenuTimerId;
    volatile int         pipChangeTimerId;
    volatile int         switchToInputTimerId;
    volatile int         ccInputTimerId;
    volatile int         asInputTimerId;
    volatile int         queueInputTimerId;
    volatile int         browseTimerId;
    volatile int         updateOSDPosTimerId;
    volatile int         updateOSDDebugTimerId;
    volatile int         endOfPlaybackTimerId;
    volatile int         embedCheckTimerId;
    volatile int         endOfRecPromptTimerId;
    volatile int         videoExitDialogTimerId;
    volatile int         pseudoChangeChanTimerId;
    volatile int         speedChangeTimerId;
    volatile int         errorRecoveryTimerId;
    mutable volatile int exitPlayerTimerId;
    TimerContextMap      stateChangeTimerId;
    TimerContextMap      signalMonitorTimerId;
    TimerContextMap      tvchainUpdateTimerId;

  public:
    // Constants
    static const int kInitFFRWSpeed; ///< 1x, default to normal speed
    static const uint kInputKeysMax; ///< When to start discarding early keys
    static const uint kNextSource;
    static const uint kPreviousSource;
    static const uint kMaxPIPCount;
    static const uint kMaxPBPCount;

    ///< Timeout for entry modes in msec
    static const uint kInputModeTimeout;
    /// Timeout for updating LCD info in msec
    static const uint kLCDTimeout;
    /// Timeout for browse mode exit in msec
    static const uint kBrowseTimeout;
    /// Seek key repeat timeout in msec
    static const uint kKeyRepeatTimeout;
    /// How long to wait before applying all previous channel keypresses in msec
    static const uint kPrevChanTimeout;
    /// How long to display sleep timer dialog in msec
    static const uint kSleepTimerDialogTimeout;
    /// How long to display idle timer dialog in seconds
    static const uint kIdleTimerDialogTimeout;
    /// How long to display idle timer dialog in msec
    static const uint kVideoExitDialogTimeout;

    static const uint kEndOfPlaybackCheckFrequency;
    static const uint kEmbedCheckFrequency;
    static const uint kSpeedChangeCheckFrequency;
    static const uint kErrorRecoveryCheckFrequency;
    static const uint kEndOfRecPromptCheckFrequency;
    static const uint kEndOfPlaybackFirstCheckTimer;

  private:
    MythActions<TV> *m_ignoreActions;
    MythActions<TV> *m_editActions;
    MythActions<TV> *m_browseActions;
    MythActions<TV> *m_browsePassActions;
    MythActions<TV> *m_manualZoomActions;
    MythActions<TV> *m_manualZoomPassActions;
    MythActions<TV> *m_picAttrActions;
    MythActions<TV> *m_timeStretchActions;
    MythActions<TV> *m_audioSyncActions;
    MythActions<TV> *m_3dActions;
    MythActions<TV> *m_activeActions;
    MythActions<TV> *m_jumpProgramActions;
    MythActions<TV> *m_seekActions;
    MythActions<TV> *m_trackActions;
    MythActions<TV> *m_ffrwndActions;
    MythActions<TV> *m_toggleActions;
    MythActions<TV> *m_pipActions;
    MythActions<TV> *m_activePostQActions;

    MythActions<TV> *m_osdActions;
    MythActions<TV> *m_osdExitActions;
    MythActions<TV> *m_osdLiveTVActions;
    MythActions<TV> *m_osdPlayingActions;
    MythActions<TV> *m_osdDialogActions;
    MythActions<TV> *m_osdAskAllowActions;
    MythActions<TV> *m_osdVideoExitActions;
    MythActions<TV> *m_osdChanEditActions;

    MythActions<TV> *m_networkControlActions;
    MythActions<TV> *m_mythEventActions;

    PlayerContext   *m_actionContext;
    bool m_actionEndManualZoom;
    StereoscopicMode m_action3dMode;
    SeekFlags m_actionSeekFlags;

    PlayerContext   *m_actionOSDContext;
    QStringList m_actionOSDArgs;
    bool m_actionOSDHide;
    QString m_actionOSDText;

    PlayerContext   *m_actionNCContext;
    QStringList m_actionNCArgs;

    QStringList m_actionMythEventArgs;
};

#endif

/* vim: set expandtab tabstop=4 shiftwidth=4: */

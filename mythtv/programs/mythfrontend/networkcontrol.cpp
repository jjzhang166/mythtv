#include <unistd.h>

#include <QCoreApplication>
#include <QRegExp>
#include <QStringList>
#include <QTextStream>
#include <QDir>
#include <QKeyEvent>
#include <QEvent>
#include <QMap>

#include "mythcorecontext.h"
#include "mythversion.h"
#include "networkcontrol.h"
#include "programinfo.h"
#include "remoteutil.h"
#include "previewgenerator.h"
#include "compat.h"
#include "mythsystemevent.h"
#include "mythdirs.h"
#include "mythlogging.h"

// libmythui
#include "mythmainwindow.h"
#include "mythuihelper.h"
#include "mythactions.h"

#define LOC QString("NetworkControl: ")
#define LOC_ERR QString("NetworkControl Error: ")

#define FE_SHORT_TO 2000
#define FE_LONG_TO  10000

static QEvent::Type kNetworkControlDataReadyEvent =
    (QEvent::Type) QEvent::registerEventType();
QEvent::Type NetworkControlCloseEvent::kEventType =
    (QEvent::Type) QEvent::registerEventType();

NetworkControl::NetworkControl() :
    ServerPool(), prompt("# "),
    gotAnswer(false), answer(""),
    clientLock(QMutex::Recursive),
    commandThread(new MThread("NetworkControl", this)),
    stopCommandThread(false), m_actions(NULL), m_netCtrlCmdActions(NULL),
    m_playCmdActions(NULL), m_musicCmdActions(NULL), m_chanCmdActions(NULL),
    m_seekCmdActions(NULL), m_speedCmdActions(NULL), m_queryCmdActions(NULL),
    m_helpCmdActions(NULL)
{
    // Eventually this map should be in the jumppoints table
    jumpMap["channelpriorities"]     = "Channel Recording Priorities";
    jumpMap["livetv"]                = "Live TV";
    jumpMap["livetvinguide"]         = "Live TV In Guide";
    jumpMap["mainmenu"]              = "Main Menu";
    jumpMap["managerecordings"]      = "Manage Recordings / Fix Conflicts";
    jumpMap["mythgallery"]           = "MythGallery";
    jumpMap["mythvideo"]             = "Video Default";
    jumpMap["mythweather"]           = "MythWeather";
    jumpMap["mythgame"]              = "MythGame";
    jumpMap["mythnews"]              = "MythNews";
    jumpMap["playdvd"]               = "Play Disc";
    jumpMap["playmusic"]             = "Play music";
    jumpMap["programfinder"]         = "Program Finder";
    jumpMap["programguide"]          = "Program Guide";
    jumpMap["recordingpriorities"]   = "Program Recording Priorities";
    jumpMap["ripcd"]                 = "Rip CD";
    jumpMap["musicplaylists"]        = "Select music playlists";
    jumpMap["deleterecordings"]      = "TV Recording Deletion";
    jumpMap["playbackrecordings"]    = "TV Recording Playback";
    jumpMap["videobrowser"]          = "Video Browser";
    jumpMap["videogallery"]          = "Video Gallery";
    jumpMap["videolistings"]         = "Video Listings";
    jumpMap["videomanager"]          = "Video Manager";
    jumpMap["zoneminderconsole"]     = "ZoneMinder Console";
    jumpMap["zoneminderliveview"]    = "ZoneMinder Live View";
    jumpMap["zoneminderevents"]      = "ZoneMinder Events";

    jumpMap["channelrecpriority"]    = "Channel Recording Priorities";
    jumpMap["viewscheduled"]         = "Manage Recordings / Fix Conflicts";
    jumpMap["previousbox"]           = "Previously Recorded";
    jumpMap["progfinder"]            = "Program Finder";
    jumpMap["guidegrid"]             = "Program Guide";
    jumpMap["programrecpriority"]    = "Program Recording Priorities";
    jumpMap["statusbox"]             = "Status Screen";
    jumpMap["deletebox"]             = "TV Recording Deletion";
    jumpMap["playbackbox"]           = "TV Recording Playback";

    keyMap["up"]                     = Qt::Key_Up;
    keyMap["down"]                   = Qt::Key_Down;
    keyMap["left"]                   = Qt::Key_Left;
    keyMap["right"]                  = Qt::Key_Right;
    keyMap["home"]                   = Qt::Key_Home;
    keyMap["end"]                    = Qt::Key_End;
    keyMap["enter"]                  = Qt::Key_Enter;
    keyMap["return"]                 = Qt::Key_Return;
    keyMap["pageup"]                 = Qt::Key_PageUp;
    keyMap["pagedown"]               = Qt::Key_PageDown;
    keyMap["escape"]                 = Qt::Key_Escape;
    keyMap["tab"]                    = Qt::Key_Tab;
    keyMap["backtab"]                = Qt::Key_Backtab;
    keyMap["space"]                  = Qt::Key_Space;
    keyMap["backspace"]              = Qt::Key_Backspace;
    keyMap["insert"]                 = Qt::Key_Insert;
    keyMap["delete"]                 = Qt::Key_Delete;
    keyMap["plus"]                   = Qt::Key_Plus;
    keyMap["+"]                      = Qt::Key_Plus;
    keyMap["comma"]                  = Qt::Key_Comma;
    keyMap[","]                      = Qt::Key_Comma;
    keyMap["minus"]                  = Qt::Key_Minus;
    keyMap["-"]                      = Qt::Key_Minus;
    keyMap["underscore"]             = Qt::Key_Underscore;
    keyMap["_"]                      = Qt::Key_Underscore;
    keyMap["period"]                 = Qt::Key_Period;
    keyMap["."]                      = Qt::Key_Period;
    keyMap["numbersign"]             = Qt::Key_NumberSign;
    keyMap["poundsign"]              = Qt::Key_NumberSign;
    keyMap["hash"]                   = Qt::Key_NumberSign;
    keyMap["#"]                      = Qt::Key_NumberSign;
    keyMap["bracketleft"]            = Qt::Key_BracketLeft;
    keyMap["["]                      = Qt::Key_BracketLeft;
    keyMap["bracketright"]           = Qt::Key_BracketRight;
    keyMap["]"]                      = Qt::Key_BracketRight;
    keyMap["backslash"]              = Qt::Key_Backslash;
    keyMap["\\"]                     = Qt::Key_Backslash;
    keyMap["dollar"]                 = Qt::Key_Dollar;
    keyMap["$"]                      = Qt::Key_Dollar;
    keyMap["percent"]                = Qt::Key_Percent;
    keyMap["%"]                      = Qt::Key_Percent;
    keyMap["ampersand"]              = Qt::Key_Ampersand;
    keyMap["&"]                      = Qt::Key_Ampersand;
    keyMap["parenleft"]              = Qt::Key_ParenLeft;
    keyMap["("]                      = Qt::Key_ParenLeft;
    keyMap["parenright"]             = Qt::Key_ParenRight;
    keyMap[")"]                      = Qt::Key_ParenRight;
    keyMap["asterisk"]               = Qt::Key_Asterisk;
    keyMap["*"]                      = Qt::Key_Asterisk;
    keyMap["question"]               = Qt::Key_Question;
    keyMap["?"]                      = Qt::Key_Question;
    keyMap["slash"]                  = Qt::Key_Slash;
    keyMap["/"]                      = Qt::Key_Slash;
    keyMap["colon"]                  = Qt::Key_Colon;
    keyMap[":"]                      = Qt::Key_Colon;
    keyMap["semicolon"]              = Qt::Key_Semicolon;
    keyMap[";"]                      = Qt::Key_Semicolon;
    keyMap["less"]                   = Qt::Key_Less;
    keyMap["<"]                      = Qt::Key_Less;
    keyMap["equal"]                  = Qt::Key_Equal;
    keyMap["="]                      = Qt::Key_Equal;
    keyMap["greater"]                = Qt::Key_Greater;
    keyMap[">"]                      = Qt::Key_Greater;
    keyMap["bar"]                    = Qt::Key_Bar;
    keyMap["pipe"]                   = Qt::Key_Bar;
    keyMap["|"]                      = Qt::Key_Bar;
    keyMap["f1"]                     = Qt::Key_F1;
    keyMap["f2"]                     = Qt::Key_F2;
    keyMap["f3"]                     = Qt::Key_F3;
    keyMap["f4"]                     = Qt::Key_F4;
    keyMap["f5"]                     = Qt::Key_F5;
    keyMap["f6"]                     = Qt::Key_F6;
    keyMap["f7"]                     = Qt::Key_F7;
    keyMap["f8"]                     = Qt::Key_F8;
    keyMap["f9"]                     = Qt::Key_F9;
    keyMap["f10"]                    = Qt::Key_F10;
    keyMap["f11"]                    = Qt::Key_F11;
    keyMap["f12"]                    = Qt::Key_F12;
    keyMap["f13"]                    = Qt::Key_F13;
    keyMap["f14"]                    = Qt::Key_F14;
    keyMap["f15"]                    = Qt::Key_F15;
    keyMap["f16"]                    = Qt::Key_F16;
    keyMap["f17"]                    = Qt::Key_F17;
    keyMap["f18"]                    = Qt::Key_F18;
    keyMap["f19"]                    = Qt::Key_F19;
    keyMap["f20"]                    = Qt::Key_F20;
    keyMap["f21"]                    = Qt::Key_F21;
    keyMap["f22"]                    = Qt::Key_F22;
    keyMap["f23"]                    = Qt::Key_F23;
    keyMap["f24"]                    = Qt::Key_F24;

    keyTextMap[Qt::Key_Plus]            = "+";
    keyTextMap[Qt::Key_Comma]           = ",";
    keyTextMap[Qt::Key_Minus]           = "-";
    keyTextMap[Qt::Key_Underscore]      = "_";
    keyTextMap[Qt::Key_Period]          = ".";
    keyTextMap[Qt::Key_NumberSign]      = "#";
    keyTextMap[Qt::Key_BracketLeft]     = "[";
    keyTextMap[Qt::Key_BracketRight]    = "]";
    keyTextMap[Qt::Key_Backslash]       = "\\";
    keyTextMap[Qt::Key_Dollar]          = "$";
    keyTextMap[Qt::Key_Percent]         = "%";
    keyTextMap[Qt::Key_Ampersand]       = "&";
    keyTextMap[Qt::Key_ParenLeft]       = "(";
    keyTextMap[Qt::Key_ParenRight]      = ")";
    keyTextMap[Qt::Key_Asterisk]        = "*";
    keyTextMap[Qt::Key_Question]        = "?";
    keyTextMap[Qt::Key_Slash]           = "/";
    keyTextMap[Qt::Key_Colon]           = ":";
    keyTextMap[Qt::Key_Semicolon]       = ";";
    keyTextMap[Qt::Key_Less]            = "<";
    keyTextMap[Qt::Key_Equal]           = "=";
    keyTextMap[Qt::Key_Greater]         = ">";
    keyTextMap[Qt::Key_Bar]             = "|";

    commandThread->start();

    gCoreContext->addListener(this);

    connect(this, SIGNAL(newConnection(QTcpSocket*)),
            this, SLOT(newConnection(QTcpSocket*)));
}

NetworkControl::~NetworkControl(void)
{
    clientLock.lock();
    while (!clients.isEmpty())
    {
        NetworkControlClient *ncc = clients.takeFirst();
        delete ncc;
    }
    clientLock.unlock();

    nrLock.lock();
    networkControlReplies.push_back(new NetworkCommand(NULL,
        "mythfrontend shutting down, connection closing..."));
    nrLock.unlock();

    notifyDataAvailable();

    ncLock.lock();
    stopCommandThread = true;
    ncCond.wakeOne();
    ncLock.unlock();
    commandThread->wait();
    delete commandThread;
    commandThread = NULL;

    if (m_actions)
        delete m_actions;

    if (m_netCtrlCmdActions)
        delete m_netCtrlCmdActions;

    if (m_playCmdActions)
        delete m_playCmdActions;

    if (m_musicCmdActions)
        delete m_musicCmdActions;

    if (m_chanCmdActions)
        delete m_chanCmdActions;

    if (m_seekCmdActions)
        delete m_seekCmdActions;

    if (m_speedCmdActions)
        delete m_speedCmdActions;

    if (m_queryCmdActions)
        delete m_queryCmdActions;

    if (m_helpCmdActions)
        delete m_helpCmdActions;
}

void NetworkControl::run(void)
{
    QMutexLocker locker(&ncLock);
    while (!stopCommandThread)
    {
        while (networkControlCommands.empty() && !stopCommandThread)
            ncCond.wait(&ncLock);
        if (!stopCommandThread)
        {
            NetworkCommand *nc = networkControlCommands.front();
            networkControlCommands.pop_front();
            locker.unlock();
            processNetworkControlCommand(nc);
            locker.relock();
        }
    }
}

static struct ActionDefStruct<NetworkControl> nccActions[] = {
    { "jump",       &NetworkControl::doNetCtrlJump },
    { "key",        &NetworkControl::doNetCtrlKey },
    { "play",       &NetworkControl::doNetCtrlPlay },
    { "query",      &NetworkControl::doNetCtrlQuery },
    { "set",        &NetworkControl::doNetCtrlSet },
    { "screenshot", &NetworkControl::doNetCtrlScreenshot },
    { "help",       &NetworkControl::doNetCtrlHelp },
    { "message",    &NetworkControl::doNetCtrlMessage },
    { "exit",       &NetworkControl::doNetCtrlExit },
    { "quit",       &NetworkControl::doNetCtrlExit }
};
static int nccActionCount = NELEMS(nccActions);

bool NetworkControl::doNetCtrlJump(const QString &action)
{
    m_netCtrlResult = processJump(m_netCtrlCmd);
    return true;
}

bool NetworkControl::doNetCtrlKey(const QString &action)
{
    m_netCtrlResult = processKey(m_netCtrlCmd);
    return true;
}

bool NetworkControl::doNetCtrlPlay(const QString &action)
{
    m_netCtrlResult = processPlay(m_netCtrlCmd, m_netCtrlClient);
    return true;
}

bool NetworkControl::doNetCtrlQuery(const QString &action)
{
    m_netCtrlResult = processQuery(m_netCtrlCmd);
    return true;
}

bool NetworkControl::doNetCtrlSet(const QString &action)
{
    m_netCtrlResult = processSet(m_netCtrlCmd);
    return true;
}

bool NetworkControl::doNetCtrlScreenshot(const QString &action)
{
    m_netCtrlResult = saveScreenshot(m_netCtrlCmd);
    return true;
}

bool NetworkControl::doNetCtrlHelp(const QString &action)
{
    m_netCtrlResult = processHelp(m_netCtrlCmd);
    return true;
}

bool NetworkControl::doNetCtrlMessage(const QString &action)
{
    m_netCtrlResult = processMessage(m_netCtrlCmd);
    return true;
}

bool NetworkControl::doNetCtrlExit(const QString &action)
{
    QCoreApplication::postEvent(this,
                      new NetworkControlCloseEvent(m_netCtrlCmd->getClient()));
    return true;
}


void NetworkControl::processNetworkControlCommand(NetworkCommand *nc)
{
    QMutexLocker locker(&clientLock);

    m_netCtrlCmd = nc;
    m_netCtrlClient = clients.indexOf(nc->getClient());

    if (!m_netCtrlCmdActions)
        m_netCtrlCmdActions =
            new MythActions<NetworkControl>(this, nccActions,
                                            nccActionCount, true);
    bool touched;
    bool handled = m_netCtrlCmdActions->handleAction(nc->getArg(0).toLower(),
                                                     touched);

    if (!handled)
        m_netCtrlResult =
            QString("INVALID command '%1', try 'help' for more info")
            .arg(nc->getArg(0));

    nrLock.lock();
    networkControlReplies.push_back(new NetworkCommand(nc->getClient(),
                                                       m_netCtrlResult));
    nrLock.unlock();

    notifyDataAvailable();
}

void NetworkControl::deleteClient(void)
{
    LOG(VB_GENERAL, LOG_INFO, LOC + "Client Socket disconnected");
    QMutexLocker locker(&clientLock);

    gCoreContext->SendSystemEvent("NET_CTRL_DISCONNECTED");

    QList<NetworkControlClient *>::const_iterator it;
    for (it = clients.begin(); it != clients.end(); ++it)
    {
        NetworkControlClient *ncc = *it;
        if (ncc->getSocket()->state() == QTcpSocket::UnconnectedState)
        {
            deleteClient(ncc);
            return;
        }
    }
}

void NetworkControl::deleteClient(NetworkControlClient *ncc)
{
    int index = clients.indexOf(ncc);
    if (index >= 0)
    {
        clients.removeAt(index);

        delete ncc;
    }
    else
        LOG(VB_GENERAL, LOG_ERR, LOC + QString("deleteClient(%1), unable to "
                "locate specified NetworkControlClient").arg((long long)ncc));
}

void NetworkControl::newConnection(QTcpSocket *client)
{
    QString welcomeStr;

    LOG(VB_GENERAL, LOG_INFO, LOC + QString("New connection established."));

    gCoreContext->SendSystemEvent("NET_CTRL_CONNECTED");

    NetworkControlClient *ncc = new NetworkControlClient(client);

    QMutexLocker locker(&clientLock);
    clients.push_back(ncc);

    connect(ncc, SIGNAL(commandReceived(QString&)), this,
            SLOT(receiveCommand(QString&)));
    connect(client, SIGNAL(disconnected()), this, SLOT(deleteClient()));

    welcomeStr = "MythFrontend Network Control\r\n";
    welcomeStr += "Type 'help' for usage information\r\n"
                  "---------------------------------";
    nrLock.lock();
    networkControlReplies.push_back(new NetworkCommand(ncc,welcomeStr));
    nrLock.unlock();

    notifyDataAvailable();
}

NetworkControlClient::NetworkControlClient(QTcpSocket *s)
{
    m_socket = s;
    m_textStream = new QTextStream(s);
    m_textStream->setCodec("UTF-8");
    connect(m_socket, SIGNAL(readyRead()), this, SLOT(readClient()));
}

NetworkControlClient::~NetworkControlClient()
{
    m_socket->close();
    m_socket->deleteLater();

    delete m_textStream;
}

void NetworkControlClient::readClient(void)
{
    QTcpSocket *socket = (QTcpSocket *)sender();
    if (!socket)
        return;

    QString lineIn;
    while (socket->canReadLine())
    {
        lineIn = socket->readLine().simplified();

        if (lineIn.isEmpty())
            continue;

        LOG(VB_NETWORK, LOG_INFO, LOC +
            QString("emit commandReceived(%1)").arg(lineIn));
        emit commandReceived(lineIn);
    }
}

void NetworkControl::receiveCommand(QString &command)
{
    LOG(VB_NETWORK, LOG_INFO, LOC +
        QString("NetworkControl::receiveCommand(%1)").arg(command));
    NetworkControlClient *ncc = static_cast<NetworkControlClient *>(sender());
    if (!ncc)
         return;

    ncLock.lock();
    networkControlCommands.push_back(new NetworkCommand(ncc,command));
    ncCond.wakeOne();
    ncLock.unlock();
}

QString NetworkControl::processJump(NetworkCommand *nc)
{
    QString result = "OK";

    if ((nc->getArgCount() < 2) || (!jumpMap.contains(nc->getArg(1))))
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(nc->getArg(0));

    GetMythMainWindow()->JumpTo(jumpMap[nc->getArg(1)]);

    // Fixme, should do some better checking here, but that would
    // depend on all Locations matching their jumppoints
    QTime timer;
    timer.start();
    while ((timer.elapsed() < FE_SHORT_TO) &&
           (GetMythUI()->GetCurrentLocation().toLower() != nc->getArg(1)))
        usleep(10000);

    return result;
}

QString NetworkControl::processKey(NetworkCommand *nc)
{
    QString result = "OK";
    QKeyEvent *event = NULL;

    if (nc->getArgCount() < 2)
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(nc->getArg(0));

    QObject *keyDest = NULL;

    if (GetMythMainWindow())
        keyDest = GetMythMainWindow();
    else
        return QString("ERROR: Application has no main window!\n");

    if (GetMythMainWindow()->currentWidget())
        keyDest = GetMythMainWindow()->currentWidget()->focusWidget();

    int curToken = 1;
    int tokenLen = 0;
    while (curToken < nc->getArgCount())
    {
        tokenLen = nc->getArg(curToken).length();

        if (nc->getArg(curToken) == "sleep")
        {
            sleep(1);
        }
        else if (keyMap.contains(nc->getArg(curToken)))
        {
            int keyCode = keyMap[nc->getArg(curToken)];
            QString keyText;

            if (keyTextMap.contains(keyCode))
                keyText = keyTextMap[keyCode];

            GetMythUI()->ResetScreensaver();

            event = new QKeyEvent(QEvent::KeyPress, keyCode, Qt::NoModifier,
                                  keyText);
            QCoreApplication::postEvent(keyDest, event);

            event = new QKeyEvent(QEvent::KeyRelease, keyCode, Qt::NoModifier,
                                  keyText);
            QCoreApplication::postEvent(keyDest, event);
        }
        else if (((tokenLen == 1) &&
                  (nc->getArg(curToken)[0].isLetterOrNumber())) ||
                 ((tokenLen >= 1) &&
                  (nc->getArg(curToken).contains("+"))))
        {
            QKeySequence a(nc->getArg(curToken));
            int keyCode = a[0];
            Qt::KeyboardModifiers modifiers = Qt::NoModifier;

            if (tokenLen > 1)
            {
                QStringList tokenParts = nc->getArg(curToken).split('+');

                int partNum = 0;
                while (partNum < (tokenParts.size() - 1))
                {
                    if (tokenParts[partNum].toUpper() == "CTRL")
                        modifiers |= Qt::ControlModifier;
                    if (tokenParts[partNum].toUpper() == "SHIFT")
                        modifiers |= Qt::ShiftModifier;
                    if (tokenParts[partNum].toUpper() == "ALT")
                        modifiers |= Qt::AltModifier;
                    if (tokenParts[partNum].toUpper() == "META")
                        modifiers |= Qt::MetaModifier;

                    partNum++;
                }
            }
            else
            {
                if (nc->getArg(curToken) == nc->getArg(curToken).toUpper())
                    modifiers = Qt::ShiftModifier;
            }

            GetMythUI()->ResetScreensaver();

            event = new QKeyEvent(QEvent::KeyPress, keyCode, modifiers,
                                  nc->getArg(curToken));
            QCoreApplication::postEvent(keyDest, event);

            event = new QKeyEvent(QEvent::KeyRelease, keyCode, modifiers,
                                  nc->getArg(curToken));
            QCoreApplication::postEvent(keyDest, event);
        }
        else
            return QString("ERROR: Invalid syntax at '%1', see 'help %2' for "
                           "usage information")
                           .arg(nc->getArg(curToken)).arg(nc->getArg(0));

        curToken++;
    }

    return result;
}

static struct ActionDefStruct<NetworkControl> ppActions[] = {
    { "file",    &NetworkControl::doPlayFile },
    { "program", &NetworkControl::doPlayProgram },
    { "music",   &NetworkControl::doPlayMusic },
    { "chanid",  &NetworkControl::doPlayChanID },
    { "channel", &NetworkControl::doPlayChannel },
    { "seek",    &NetworkControl::doPlaySeek },
    { "speed",   &NetworkControl::doPlaySpeed },
    { "save",    &NetworkControl::doPlaySave },
    { "stop",    &NetworkControl::doPlayStop },
    { "volume",  &NetworkControl::doPlayVolume }
};
static int ppActionCount = NELEMS(ppActions);

bool NetworkControl::doPlayFile(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() < 3)
        return false;

    if (GetMythUI()->GetCurrentLocation().toLower() != "mainmenu")
    {
        GetMythMainWindow()->JumpTo(jumpMap["mainmenu"]);

        QTime timer;
        timer.start();
        while ((timer.elapsed() < FE_LONG_TO) &&
               (GetMythUI()->GetCurrentLocation().toLower() != "mainmenu"))
            usleep(10000);
    }

    if (GetMythUI()->GetCurrentLocation().toLower() == "mainmenu")
    {
        QStringList args;
        args << m_netCtrlCmd->getFrom(2);
        MythEvent *me = new MythEvent(ACTION_HANDLEMEDIA, args);
        qApp->postEvent(GetMythMainWindow(), me);
    }
    else
        m_netCtrlResult = "Unable to change to main menu to start playback!";

    return true;
}

bool NetworkControl::doPlayProgram(const QString &action)
{
    if ((m_netCtrlCmd->getArgCount() < 4) ||
        !(m_netCtrlCmd->getArg(2).contains(QRegExp("^\\d+$"))) ||
        !(m_netCtrlCmd->getArg(3).contains(QRegExp(
                         "^\\d\\d\\d\\d-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d$"))))
        return false;

    if (GetMythUI()->GetCurrentLocation().toLower() == "playback")
    {
        QString message = QString("NETWORK_CONTROL STOP");
        MythEvent me(message);
        gCoreContext->dispatch(me);

        QTime timer;
        timer.start();
        while ((timer.elapsed() < FE_LONG_TO) &&
               (GetMythUI()->GetCurrentLocation().toLower() == "playback"))
            usleep(10000);
    }

    if (GetMythUI()->GetCurrentLocation().toLower() != "playbackbox")
    {
        GetMythMainWindow()->JumpTo(jumpMap["playbackbox"]);

        QTime timer;
        timer.start();
        while ((timer.elapsed() < 10000) &&
               (GetMythUI()->GetCurrentLocation().toLower() != "playbackbox"))
            usleep(10000);

        timer.start();
        while ((timer.elapsed() < 10000) &&
               (!GetMythUI()->IsTopScreenInitialized()))
            usleep(10000);
    }

    if (GetMythUI()->GetCurrentLocation().toLower() == "playbackbox")
    {
        QString action = "PLAY";
        if (m_netCtrlCmd->getArgCount() == 5 &&
            m_netCtrlCmd->getArg(4) == "resume")
            action = "RESUME";

        QString message = QString("NETWORK_CONTROL %1 PROGRAM %2 %3 %4")
                                  .arg(action).arg(m_netCtrlCmd->getArg(2))
                                  .arg(m_netCtrlCmd->getArg(3).toUpper())
                                  .arg(m_netCtrlClient);

        gotAnswer = false;
        QTime timer;
        timer.start();

        MythEvent me(message);
        gCoreContext->dispatch(me);

        while (timer.elapsed() < FE_LONG_TO && !gotAnswer)
            usleep(10000);

        if (gotAnswer)
            m_netCtrlResult += answer;
        else
            m_netCtrlResult = "ERROR: Timed out waiting for reply from player";
    }
    else
    {
        m_netCtrlResult = QString("ERROR: Unable to change to PlaybackBox from "
                                  "%1, cannot play requested file.")
                         .arg(GetMythUI()->GetCurrentLocation());
    }
    return true;
}

static struct ActionDefStruct<NetworkControl> pmActions[] = {
    { "play",      &NetworkControl::doMusicPlay },
    { "pause",     &NetworkControl::doMusicPause },
    { "stop",      &NetworkControl::doMusicStop },
    { "getvolume", &NetworkControl::doMusicGetVolume },
    { "getmeta",   &NetworkControl::doMusicGetMeta },
    { "setvolume", &NetworkControl::doMusicSetVolume },
    { "track",     &NetworkControl::doMusicTrack },
    { "url",       &NetworkControl::doMusicURL },
    { "file",      &NetworkControl::doMusicFile }
};
static int pmActionCount = NELEMS(pmActions);

bool NetworkControl::doMusicPlay(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() != 3)
        return false;

    QString hostname = gCoreContext->GetHostName();
    m_netCtrlMessage = QString("MUSIC_COMMAND %1 PLAY").arg(hostname);
    return true;
}

bool NetworkControl::doMusicPause(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() != 3)
        return false;

    QString hostname = gCoreContext->GetHostName();
    m_netCtrlMessage = QString("MUSIC_COMMAND %1 PAUSE").arg(hostname);
    return true;
}

bool NetworkControl::doMusicStop(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() != 3)
        return false;

    QString hostname = gCoreContext->GetHostName();
    m_netCtrlMessage = QString("MUSIC_COMMAND %1 STOP").arg(hostname);
    return true;
}

bool NetworkControl::doMusicGetVolume(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() != 3)
        return false;

    QString hostname = gCoreContext->GetHostName();
    gotAnswer = false;

    MythEvent me(QString("MUSIC_COMMAND %1 GET_VOLUME").arg(hostname));
    gCoreContext->dispatch(me);

    QTime timer;
    timer.start();
    while (timer.elapsed() < FE_SHORT_TO && !gotAnswer)
    {
        qApp->processEvents();
        usleep(10000);
    }

    if (gotAnswer)
        m_netCtrlResult = answer;

    m_netCtrlResult = "unknown";
    return true;
}

bool NetworkControl::doMusicGetMeta(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() != 3)
        return false;

    QString hostname = gCoreContext->GetHostName();
    gotAnswer = false;

    MythEvent me(QString("MUSIC_COMMAND %1 GET_METADATA").arg(hostname));
    gCoreContext->dispatch(me);

    QTime timer;
    timer.start();
    while (timer.elapsed() < FE_SHORT_TO && !gotAnswer)
    {
        qApp->processEvents();
        usleep(10000);
    }

    if (gotAnswer)
        m_netCtrlResult = answer;

    m_netCtrlResult = "unknown";
    return true;
}

bool NetworkControl::doMusicSetVolume(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() < 4)
        return false;

    QString hostname = gCoreContext->GetHostName();
    m_netCtrlMessage = QString("MUSIC_COMMAND %1 SET_VOLUME %2") .arg(hostname)
                            .arg(m_netCtrlCmd->getArg(3));
    return true;
}

bool NetworkControl::doMusicTrack(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() < 4)
        return false;

    QString hostname = gCoreContext->GetHostName();
    m_netCtrlMessage = QString("MUSIC_COMMAND %1 PLAY_TRACK %2") .arg(hostname)
                            .arg(m_netCtrlCmd->getArg(3));
    return true;
}

bool NetworkControl::doMusicURL(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() < 4)
        return false;

    QString hostname = gCoreContext->GetHostName();
    m_netCtrlMessage = QString("MUSIC_COMMAND %1 PLAY_URL %2") .arg(hostname)
                            .arg(m_netCtrlCmd->getArg(3));
    return true;
}

bool NetworkControl::doMusicFile(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() < 4)
        return false;

    QString hostname = gCoreContext->GetHostName();
    m_netCtrlMessage = QString("MUSIC_COMMAND %1 PLAY_FILE '%2'") .arg(hostname)
                            .arg(m_netCtrlCmd->getFrom(3));
    return true;
}


bool NetworkControl::doPlayMusic(const QString &action)
{
#if 0
    if (GetMythUI()->GetCurrentLocation().toLower() != "playmusic")
    {
        return QString("ERROR: You are in %1 mode and this command is "
                       "only for MythMusic")
                    .arg(GetMythUI()->GetCurrentLocation());
    }
#endif

    QString hostname = gCoreContext->GetHostName();

    if (!m_musicCmdActions)
        m_musicCmdActions =
            new MythActions<NetworkControl>(this, pmActions,
                                            pmActionCount, true);
    bool touched;
    bool handled =
        m_musicCmdActions->handleAction(m_netCtrlCmd->getArg(2).toLower(),
                                        touched);

    if (!handled)
    {
        m_netCtrlResult =  QString("ERROR: Invalid 'play music' command");
    }

    return true;
}

bool NetworkControl::doPlayChanID(const QString &action)
{
    // Everything below here requires us to be in playback mode so check to
    // see if we are
    if (GetMythUI()->GetCurrentLocation().toLower() != "playback")
    {
        m_netCtrlResult =
            QString("ERROR: You are in %1 mode and this command is only "
                    "for playback mode")
                .arg(GetMythUI()->GetCurrentLocation());
        return true;
    }

    if (m_netCtrlCmd->getArg(2).contains(QRegExp("^\\d+$")))
        m_netCtrlMessage = QString("NETWORK_CONTROL CHANID %1")
            .arg(m_netCtrlCmd->getArg(2));
    else
        m_netCtrlResult = QString("ERROR: See 'help %1' for usage information")
                           .arg(m_netCtrlCmd->getArg(0));
    return true;
}

static struct ActionDefStruct<NetworkControl> pcActions[] = {
    { "up",             &NetworkControl::doChanUp },
    { "down",           &NetworkControl::doChanDown },
    { "^[-\\.\\d_#]+$", &NetworkControl::doChanChannel }
};
static int pcActionCount = NELEMS(pcActions);

bool NetworkControl::doChanUp(const QString &action)
{
    m_netCtrlMessage = "NETWORK_CONTROL CHANNEL UP";
    return true;
}

bool NetworkControl::doChanDown(const QString &action)
{
    m_netCtrlMessage = "NETWORK_CONTROL CHANNEL DOWN";
    return true;
}

bool NetworkControl::doChanChannel(const QString &action)
{
    m_netCtrlMessage = QString("NETWORK_CONTROL CHANNEL %1") .arg(action);
    return true;
}


bool NetworkControl::doPlayChannel(const QString &action)
{
    // Everything below here requires us to be in playback mode so check to
    // see if we are
    if (GetMythUI()->GetCurrentLocation().toLower() != "playback")
    {
        m_netCtrlResult =
            QString("ERROR: You are in %1 mode and this command is only "
                    "for playback mode")
                   .arg(GetMythUI()->GetCurrentLocation());
        return true;
    }

    bool handled = false;

    if (m_netCtrlCmd->getArgCount() >= 3)
    {
        if (!m_chanCmdActions)
            m_chanCmdActions =
                new MythActions<NetworkControl>(this, pcActions,
                                                pcActionCount, true);
        bool touched;
        handled =
            m_chanCmdActions->handleAction(m_netCtrlCmd->getArg(2).toLower(),
                                           touched);
    }

    if (!handled)
        m_netCtrlResult = QString("ERROR: See 'help %1' for usage information")
                               .arg(m_netCtrlCmd->getArg(0));
    return true;
}

static struct ActionDefStruct<NetworkControl> psActions[] = {
    { "beginning",              &NetworkControl::doSeekBegin },
    { "forward",                &NetworkControl::doSeekForward },
    { "rewind",                 &NetworkControl::doSeekBackward },
    { "backward",               &NetworkControl::doSeekBackward },
    { "^\\d\\d:\\d\\d:\\d\\d$", &NetworkControl::doSeekTime }
};
static int psActionCount = NELEMS(psActions);

bool NetworkControl::doSeekBegin(const QString &action)
{
    m_netCtrlMessage = "NETWORK_CONTROL SEEK BEGINNING";
    return true;
}

bool NetworkControl::doSeekForward(const QString &action)
{
    m_netCtrlMessage = "NETWORK_CONTROL SEEK FORWARD";
    return true;
}

bool NetworkControl::doSeekBackward(const QString &action)
{
    m_netCtrlMessage = "NETWORK_CONTROL SEEK BACKWARD";
    return true;
}

bool NetworkControl::doSeekTime(const QString &action)
{
    int hours   = action.mid(0, 2).toInt();
    int minutes = action.mid(3, 2).toInt();
    int seconds = action.mid(6, 2).toInt();
    m_netCtrlMessage = QString("NETWORK_CONTROL SEEK POSITION %1")
                           .arg((hours * 3600) + (minutes * 60) + seconds);
    return true;
}


bool NetworkControl::doPlaySeek(const QString &action)
{
    // Everything below here requires us to be in playback mode so check to
    // see if we are
    if (GetMythUI()->GetCurrentLocation().toLower() != "playback")
    {
        m_netCtrlResult =
            QString("ERROR: You are in %1 mode and this command is only "
                    "for playback mode")
                       .arg(GetMythUI()->GetCurrentLocation());
        return true;
    }

    bool handled = false;

    if (m_netCtrlCmd->getArgCount() >= 3)
    {
        if (!m_seekCmdActions)
            m_seekCmdActions =
                new MythActions<NetworkControl>(this, psActions,
                                                psActionCount, true);
        bool touched;
        handled =
            m_seekCmdActions->handleAction(m_netCtrlCmd->getArg(2).toLower(),
                                           touched);
    }

    if (!handled)
        m_netCtrlResult = QString("ERROR: See 'help %1' for usage information")
                               .arg(m_netCtrlCmd->getArg(0));
    return true;
}

static struct ActionDefStruct<NetworkControl> pspdActions[] = {
    { "normal",             &NetworkControl::doSpeedNormal },
    { "pause",              &NetworkControl::doSpeedPause },
    { "^\\-*\\d+x$",        &NetworkControl::doSpeedSpeed },
    { "^\\-*\\d+\\/\\d+x$", &NetworkControl::doSpeedSpeed },
    { "^\\-*\\d*\\.\\d+x$", &NetworkControl::doSpeedSpeed },
};
static int pspdActionCount = NELEMS(pspdActions);

bool NetworkControl::doSpeedNormal(const QString &action)
{
    m_netCtrlMessage = QString("NETWORK_CONTROL SPEED 1x");
    return true;
}

bool NetworkControl::doSpeedPause(const QString &action)
{
    m_netCtrlMessage = QString("NETWORK_CONTROL SPEED 0x");
    return true;
}

bool NetworkControl::doSpeedSpeed(const QString &action)
{
    m_netCtrlMessage = QString("NETWORK_CONTROL SPEED %1").arg(action);
    return true;
}


bool NetworkControl::doPlaySpeed(const QString &action)
{
    // Everything below here requires us to be in playback mode so check to
    // see if we are
    if (GetMythUI()->GetCurrentLocation().toLower() != "playback")
    {
        m_netCtrlResult =
            QString("ERROR: You are in %1 mode and this command is only "
                    "for playback mode")
                       .arg(GetMythUI()->GetCurrentLocation());
        return true;
    }

    bool handled = false;

    if (m_netCtrlCmd->getArgCount() >= 3)
    {
        if (!m_speedCmdActions)
            m_speedCmdActions =
                new MythActions<NetworkControl>(this, pspdActions,
                                                pspdActionCount, true);
        bool touched;
        handled =
            m_speedCmdActions->handleAction(m_netCtrlCmd->getArg(2).toLower(),
                                            touched);
    }

    if (!handled)
        m_netCtrlResult = QString("ERROR: See 'help %1' for usage information")
                               .arg(m_netCtrlCmd->getArg(0));
    return true;
}

bool NetworkControl::doPlaySave(const QString &action)
{
    // Everything below here requires us to be in playback mode so check to
    // see if we are
    if (GetMythUI()->GetCurrentLocation().toLower() != "playback")
    {
        m_netCtrlMessage =
            QString("ERROR: You are in %1 mode and this command is only "
                    "for playback mode")
                       .arg(GetMythUI()->GetCurrentLocation());
        return true;
    }

    QString arg(m_netCtrlCmd->getArg(2));
    QString comparg("screenshot");
    if (arg.size() > comparg.size())
        arg = arg.left(comparg.size());
    else
        comparg = comparg.left(arg.size());

    if (arg.toLower() == comparg)
        m_netCtrlResult = saveScreenshot(m_netCtrlCmd);

    return true;
}

bool NetworkControl::doPlayStop(const QString &action)
{
    // Everything below here requires us to be in playback mode so check to
    // see if we are
    if (GetMythUI()->GetCurrentLocation().toLower() != "playback")
    {
        m_netCtrlResult =
            QString("ERROR: You are in %1 mode and this command is only "
                    "for playback mode")
                       .arg(GetMythUI()->GetCurrentLocation());
        return true;
    }

    m_netCtrlMessage = QString("NETWORK_CONTROL STOP");
    return true;
}

bool NetworkControl::doPlayVolume(const QString &action)
{
    // Everything below here requires us to be in playback mode so check to
    // see if we are
    if (GetMythUI()->GetCurrentLocation().toLower() != "playback")
    {
        m_netCtrlResult =
            QString("ERROR: You are in %1 mode and this command is only "
                    "for playback mode")
                       .arg(GetMythUI()->GetCurrentLocation());
        return true;
    }

    if ((m_netCtrlCmd->getArgCount() < 3) ||
        (!m_netCtrlCmd->getArg(2).contains(QRegExp("^\\d+%?$"))))
    {
        m_netCtrlResult = QString("ERROR: See 'help %1' for usage information")
                              .arg(m_netCtrlCmd->getArg(0));
        return true;
    }

    m_netCtrlMessage = QString("NETWORK_CONTROL VOLUME %1")
                           .arg(m_netCtrlCmd->getArg(2));
    return true;
}


QString NetworkControl::processPlay(NetworkCommand *nc, int clientID)
{
    QString result = "OK";

    if (nc->getArgCount() < 2)
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(nc->getArg(0));

    if (!m_playCmdActions)
        m_playCmdActions = new MythActions<NetworkControl>(this, ppActions,
                                                           ppActionCount, true);
    bool touched;
    m_netCtrlCmd = nc;
    m_netCtrlClient = clientID;
    m_netCtrlResult.clear();
    m_netCtrlMessage.clear();
    bool handled = m_playCmdActions->handleAction(nc->getArg(1).toLower(),
                                                     touched);

    if (!handled)
        m_netCtrlResult = QString("ERROR: See 'help %1' for usage information")
                              .arg(m_netCtrlCmd->getArg(0));

    if (!m_netCtrlMessage.isEmpty())
    {
        MythEvent me(m_netCtrlMessage);
        gCoreContext->dispatch(me);
    }

    if (!m_netCtrlResult.isEmpty())
        return m_netCtrlResult;

    return result;
}

static struct ActionDefStruct<NetworkControl> pqActions[] = {
    { "location",   &NetworkControl::doQueryLocation },
    { "verbose",    &NetworkControl::doQueryVerbose },
    { "livetv",     &NetworkControl::doQueryLiveTV },
    { "version",    &NetworkControl::doQueryVersion },
    { "time",       &NetworkControl::doQueryTime },
    { "uptime",     &NetworkControl::doQueryUptime },
    { "load",       &NetworkControl::doQueryLoad },
    { "memstats",   &NetworkControl::doQueryMemstats },
    { "volume",     &NetworkControl::doQueryVolume },
    { "recording",  &NetworkControl::doQueryRecording },
    { "recordings", &NetworkControl::doQueryRecordings },
    { "channels",   &NetworkControl::doQueryChannels }
};
static int pqActionCount = NELEMS(pqActions);

bool NetworkControl::doQueryLocation(const QString &action)
{
    bool fullPath = false;
    bool mainStackOnly = true;

    if (m_netCtrlCmd->getArgCount() > 2)
        fullPath = (m_netCtrlCmd->getArg(2).toLower() == "true" ||
                    m_netCtrlCmd->getArg(2) == "1");
    if (m_netCtrlCmd->getArgCount() > 3)
        mainStackOnly = (m_netCtrlCmd->getArg(3).toLower() == "true" ||
                         m_netCtrlCmd->getArg(3) == "1");

    QString location = GetMythUI()->GetCurrentLocation(fullPath, mainStackOnly);
    m_netCtrlResult = location;

    // if we're playing something, then find out what
    if (location == "Playback")
    {
        m_netCtrlResult += " ";
        gotAnswer = false;
        QString message = QString("NETWORK_CONTROL QUERY POSITION");
        MythEvent me(message);
        gCoreContext->dispatch(me);

        QTime timer;
        timer.start();
        while (timer.elapsed() < FE_SHORT_TO  && !gotAnswer)
            usleep(10000);

        if (gotAnswer)
            m_netCtrlResult += answer;
        else
            m_netCtrlResult = "ERROR: Timed out waiting for reply from player";
    }
    return true;
}

bool NetworkControl::doQueryVerbose(const QString &action)
{
    m_netCtrlResult = verboseString;
    return true;
}

bool NetworkControl::doQueryLiveTV(const QString &action)
{
    if(m_netCtrlCmd->getArgCount() == 3) // has a channel ID
        m_netCtrlResult = listSchedule(m_netCtrlCmd->getArg(2));
    else
        m_netCtrlResult = listSchedule();
    return true;
}

bool NetworkControl::doQueryVersion(const QString &action)
{
    int dbSchema = gCoreContext->GetNumSetting("DBSchemaVer");

    m_netCtrlResult = QString("VERSION: %1/%2 %3 %4 QT/%5 DBSchema/%6")
                          .arg(MYTH_SOURCE_VERSION)
                          .arg(MYTH_SOURCE_PATH)
                          .arg(MYTH_BINARY_VERSION)
                          .arg(MYTH_PROTO_VERSION)
                          .arg(QT_VERSION_STR)
                          .arg(dbSchema);
    return true;
}

bool NetworkControl::doQueryTime(const QString &action)
{
    m_netCtrlResult = QDateTime::currentDateTime().toString(Qt::ISODate);
    return true;
}

bool NetworkControl::doQueryUptime(const QString &action)
{
    time_t  uptime;

    if (getUptime(uptime))
        m_netCtrlResult = QString::number(uptime);
    else
        m_netCtrlResult = QString("Could not determine uptime.");
    return true;
}

bool NetworkControl::doQueryLoad(const QString &action)
{
    double  loads[3];

    if (getloadavg(loads,3) == -1)
        m_netCtrlResult = QString("getloadavg() failed");
    else
        m_netCtrlResult = QString("%1 %2 %3").arg(loads[0]).arg(loads[1])
                              .arg(loads[2]);
    return true;
}

bool NetworkControl::doQueryMemstats(const QString &action)
{
    int     totalMB, freeMB, totalVM, freeVM;

    if (getMemStats(totalMB, freeMB, totalVM, freeVM))
        m_netCtrlResult = QString("%1 %2 %3 %4")
                              .arg(totalMB).arg(freeMB).arg(totalVM)
                              .arg(freeVM);
    else
        m_netCtrlResult = QString("Could not determine memory stats.");
    return true;
}

bool NetworkControl::doQueryVolume(const QString &action)
{
    m_netCtrlResult = "0%";

    QString location = GetMythUI()->GetCurrentLocation(false, false);

    if (location != "Playback")
        return true;

    gotAnswer = false;
    QString message = QString("NETWORK_CONTROL QUERY VOLUME");
    MythEvent me(message);
    gCoreContext->dispatch(me);

    QTime timer;
    timer.start();
    while (timer.elapsed() < FE_SHORT_TO  && !gotAnswer)
        usleep(10000);

    if (gotAnswer)
        m_netCtrlResult = answer;
    else
        m_netCtrlResult = "ERROR: Timed out waiting for reply from player";

    return true;
}

bool NetworkControl::doQueryRecording(const QString &action)
{
    if ((m_netCtrlCmd->getArgCount() == 4) &&
        (m_netCtrlCmd->getArg(2).contains(QRegExp("^\\d+$"))) &&
        (m_netCtrlCmd->getArg(3).contains(QRegExp(
                         "^\\d\\d\\d\\d-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d$"))))
    {
        m_netCtrlResult = listRecordings(m_netCtrlCmd->getArg(2),
                                         m_netCtrlCmd->getArg(3).toUpper());
        return true;
    }
    return false;
}

bool NetworkControl::doQueryRecordings(const QString &action)
{
    m_netCtrlResult = listRecordings();
    return true;
}

bool NetworkControl::doQueryChannels(const QString &action)
{
    if (m_netCtrlCmd->getArgCount() == 2)
        m_netCtrlResult = listChannels(0, 0);  // give us all you can
    else if (m_netCtrlCmd->getArgCount() == 4)
        m_netCtrlResult =
            listChannels(m_netCtrlCmd->getArg(2).toLower().toUInt(),
                         m_netCtrlCmd->getArg(3).toLower().toUInt());
    else
        m_netCtrlResult =
            QString("ERROR: See 'help %1' for usage information "
                    "(parameters mismatch)").arg(m_netCtrlCmd->getArg(0));
    return true;
}


QString NetworkControl::processQuery(NetworkCommand *nc)
{
    QString result = "OK";

    if (nc->getArgCount() < 2)
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(nc->getArg(0));

    if (!m_queryCmdActions)
        m_queryCmdActions = new MythActions<NetworkControl>(this, pqActions,
                                                           pqActionCount, true);
    bool touched;
    m_netCtrlCmd = nc;
    m_netCtrlResult.clear();
    m_netCtrlMessage.clear();
    bool handled = m_queryCmdActions->handleAction(nc->getArg(1).toLower(),
                                                   touched);
    if (!handled)
        m_netCtrlResult = QString("ERROR: See 'help %1' for usage information")
                       .arg(m_netCtrlCmd->getArg(0));

    if (!m_netCtrlResult.isEmpty())
        return m_netCtrlResult;

    return result;
}

QString NetworkControl::processSet(NetworkCommand *nc)
{
    if (nc->getArgCount() == 1)
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(nc->getArg(0));

    if (nc->getArg(1) == "verbose")
    {
        if (nc->getArgCount() > 3)
            return QString("ERROR: Separate filters with commas with no "
                           "space: playback,audio\r\n See 'help %1' for usage "
                           "information").arg(nc->getArg(0));

        QString oldVerboseString = verboseString;
        QString result = "OK";

        int pva_result = verboseArgParse(nc->getArg(2));

        if (pva_result != 0 /*GENERIC_EXIT_OK */)
            result = "Failed";

        result += "\r\n";
        result += " Previous filter: " + oldVerboseString + "\r\n";
        result += "      New Filter: " + verboseString + "\r\n";

        LOG(VB_GENERAL, LOG_NOTICE,
            QString("Verbose mask changed, new level is: %1")
                .arg(verboseString));

        return result;
    }

    return QString("ERROR: See 'help %1' for usage information")
                   .arg(nc->getArg(0));
}

static struct ActionDefStruct<NetworkControl> phActions[] = {
    { "jump",       &NetworkControl::doHelpJump },
    { "key",        &NetworkControl::doHelpKey },
    { "play",       &NetworkControl::doHelpPlay },
    { "query",      &NetworkControl::doHelpQuery },
    { "set",        &NetworkControl::doHelpSet },
    { "screenshot", &NetworkControl::doHelpScreenshot },
    { "exit",       &NetworkControl::doHelpExit },
    { "message",    &NetworkControl::doHelpMessage }
};
static int phActionCount = NELEMS(phActions);

bool NetworkControl::doHelpJump(const QString &action)
{
    QMap<QString, QString>::Iterator it;
    m_netCtrlResult +=
        "Usage: jump JUMPPOINT\r\n"
        "\r\n"
        "Where JUMPPOINT is one of the following:\r\n";

    for (it = jumpMap.begin(); it != jumpMap.end(); ++it)
    {
        m_netCtrlResult += it.key().leftJustified(20, ' ', true) + " - " +
                           *it + "\r\n";
    }
    return true;
}

bool NetworkControl::doHelpKey(const QString &action)
{
    m_netCtrlResult +=
        "key LETTER           - Send the letter key specified\r\n"
        "key NUMBER           - Send the number key specified\r\n"
        "key CODE             - Send one of the following key codes\r\n"
        "\r\n";

    QMap<QString, int>::Iterator it;
    bool first = true;
    for (it = keyMap.begin(); it != keyMap.end(); ++it)
    {
        if (first)
            first = false;
        else
            m_netCtrlResult += ", ";

        m_netCtrlResult += it.key();
    }
    m_netCtrlResult += "\r\n";
    return true;
}

bool NetworkControl::doHelpPlay(const QString &action)
{
    m_netCtrlResult +=
        "play volume NUMBER%    - Change volume to given percentage value\r\n"
        "play channel up        - Change channel Up\r\n"
        "play channel down      - Change channel Down\r\n"
        "play channel NUMBER    - Change to a specific channel number\r\n"
        "play chanid NUMBER     - Change to a specific channel id (chanid)\r\n"
        "play file FILENAME     - "
        "Play FILENAME (FILENAME may be a file or a myth:// URL)\r\n"
        "play program CHANID yyyy-MM-ddThh:mm:ss\r\n"
        "                       - Play program with chanid & starttime\r\n"
        "play program CHANID yyyy-MM-ddThh:mm:ss resume\r\n"
        "                       - Resume program with chanid & starttime\r\n"
        "play save preview\r\n"
        "                       - Save preview image from current position\r\n"
        "play save preview FILENAME\r\n"
        "                       - Save preview image to FILENAME\r\n"
        "play save preview FILENAME WxH\r\n"
        "                       - Save preview image of size WxH\r\n"
        "play seek beginning    - Seek to the beginning of the recording\r\n"
        "play seek forward      - Skip forward in the video\r\n"
        "play seek backward     - Skip backwards in the video\r\n"
        "play seek HH:MM:SS     - Seek to a specific position\r\n"
        "play speed pause       - Pause playback\r\n"
        "play speed normal      - Playback at normal speed\r\n"
        "play speed 1x          - Playback at normal speed\r\n"
        "play speed SPEEDx      - Playback where SPEED must be a decimal\r\n"
        "play speed 1/8x        - Playback at 1/8x speed\r\n"
        "play speed 1/4x        - Playback at 1/4x speed\r\n"
        "play speed 1/3x        - Playback at 1/3x speed\r\n"
        "play speed 1/2x        - Playback at 1/2x speed\r\n"
        "play stop              - Stop playback\r\n"
        "play music play        - Resume playback (MythMusic)\r\n"
        "play music pause       - Pause playback (MythMusic)\r\n"
        "play music stop        - Stop Playback (MythMusic)\r\n"
        "play music setvolume N - Set volume to number (MythMusic)\r\n"
        "play music getvolume   - Get current volume (MythMusic)\r\n"
        "play music getmeta     - "
        "Get metadata for current track (MythMusic)\r\n"
        "play music file NAME   - Play specified file (MythMusic)\r\n"
        "play music track N     - Switch to specified track (MythMusic)\r\n"
        "play music url URL     - Play specified URL (MythMusic)\r\n";
    return true;
}

bool NetworkControl::doHelpQuery(const QString &action)
{
    m_netCtrlResult +=
        "query location        - Query current screen or location\r\n"
        "query volume          - Query the current playback volume\r\n"
        "query recordings      - List currently available recordings\r\n"
        "query recording CHANID STARTTIME\r\n"
        "                      - List info about the specified program\r\n"
        "query liveTV          - List current TV schedule\r\n"
        "query liveTV CHANID   - "
        "Query current program for specified channel\r\n"
        "query load            - List 1/5/15 load averages\r\n"
        "query memstats        - "
        "List free and total, physical and swap memory\r\n"
        "query time            - Query current time on frontend\r\n"
        "query uptime          - Query machine uptime\r\n"
        "query verbose         - Get current VERBOSE mask\r\n"
        "query version         - Query Frontend version details\r\n"
        "query channels        - Query available channels\r\n"
        "query channels START LIMIT - "
        "Query available channels from START and limit results to LIMIT "
        "lines\r\n";
    return true;
}

bool NetworkControl::doHelpSet(const QString &action)
{
    m_netCtrlResult +=
        "set verbose debug-mask - "
        "Change the VERBOSE mask to 'debug-mask'\r\n"
        "                         (i.e. 'set verbose playback,audio')\r\n"
        "                         use 'set verbose default' to revert\r\n"
        "                         back to the default level of\r\n";
    return true;
}

bool NetworkControl::doHelpScreenshot(const QString &action)
{
    m_netCtrlResult +=
        "screenshot               - "
        "Takes a screenshot and saves it as screenshot.png\r\n"
        "screenshot WxH           - "
        "Saves the screenshot as a WxH size image\r\n";
    return true;
}

bool NetworkControl::doHelpExit(const QString &action)
{
    m_netCtrlResult +=
        "exit                  - Terminates session\r\n\r\n";
    return true;
}

bool NetworkControl::doHelpMessage(const QString &action)
{
    m_netCtrlResult +=
        "message               - Displays a simple text message popup\r\n";
    return true;
}


QString NetworkControl::processHelp(NetworkCommand *nc)
{
    QString command;

    if (!m_helpCmdActions)
        m_helpCmdActions = new MythActions<NetworkControl>(this, phActions,
                                                           phActionCount, true);
    if (nc->getArgCount() >= 1)
    {
        QString arg(nc->getArg(0));
        QString cmparg("help");

        if (arg.size() > cmparg.size())
            arg = arg.left(cmparg.size());
        else
            cmparg = cmparg.left(arg.size());

        if (arg.toLower() == cmparg.toLower())
        {
            if (nc->getArgCount() >= 2)
                command = nc->getArg(1);
            else
                command.clear();
        }
        else
        {
            command = nc->getArg(0);
        }
    }

    bool touched;
    m_netCtrlCmd = nc;
    m_netCtrlResult.clear();
    m_netCtrlMessage.clear();
    bool handled = m_helpCmdActions->handleAction(command, touched);

    if (!m_netCtrlResult.isEmpty())
        return m_netCtrlResult;

    if (!handled && !command.isEmpty())
            m_netCtrlResult += QString("Unknown command '%1'\r\n\r\n")
                                   .arg(command);

    m_netCtrlResult +=
        "Valid Commands:\r\n"
        "---------------\r\n"
        "jump               - Jump to a specified location in Myth\r\n"
        "key                - Send a keypress to the program\r\n"
        "play               - Playback related commands\r\n"
        "query              - Queries\r\n"
        "set                - Changes\r\n"
        "screenshot         - Capture screenshot\r\n"
        "message            - Display a simple text message\r\n"
        "exit               - Exit Network Control\r\n"
        "\r\n"
        "Type 'help COMMANDNAME' for help on any specific command.\r\n";

    return m_netCtrlResult;
}

QString NetworkControl::processMessage(NetworkCommand *nc)
{
    if (nc->getArgCount() < 2)
        return QString("ERROR: See 'help %1' for usage information")
                       .arg(nc->getArg(0));

    QString message = nc->getCommand().remove(0, 7).trimmed();
    MythMainWindow *window = GetMythMainWindow();
    MythEvent* me = new MythEvent(MythEvent::MythUserMessage, message);
    qApp->postEvent(window, me);
    return QString("OK");
}

void NetworkControl::notifyDataAvailable(void)
{
    QCoreApplication::postEvent(
        this, new QEvent(kNetworkControlDataReadyEvent));
}

void NetworkControl::sendReplyToClient(NetworkControlClient *ncc,
                                       QString &reply)
{
    if (!clients.contains(ncc))
        // NetworkControl instance is unaware of control client
        // assume connection to client has been terminated and bail
        return;

    QRegExp crlfRegEx("\r\n$");
    QRegExp crlfcrlfRegEx("\r\n.*\r\n");

    QTcpSocket  *client = ncc->getSocket();
    QTextStream *clientStream = ncc->getTextStream();

    if (client && clientStream && client->state() == QTcpSocket::ConnectedState)
    {
        *clientStream << reply;

        if ((!reply.contains(crlfRegEx)) ||
            ( reply.contains(crlfcrlfRegEx)))
            *clientStream << "\r\n" << prompt;

        clientStream->flush();
        client->flush();
    }
}

static struct ActionDefStruct<NetworkControl> ncActions[] = {
    { "^MUSIC_CONTROL",   &NetworkControl::doMusicControl },
    { "^NETWORK_CONTROL", &NetworkControl::doNetworkControl }
};
static int ncActionCount = NELEMS(ncActions);

bool NetworkControl::doMusicControl(const QString &action)
{
    QStringList tokens = action.simplified().split(" ");
    tokens.removeFirst();
    int size = tokens.size();

    if ((size >= 3) && (tokens[0] == "ANSWER") &&
        (tokens[1] == gCoreContext->GetHostName()))
    {
        answer = tokens[2];
        for (int i = 3; i < size; i++)
            answer += QString(" ") + tokens[i];
        gotAnswer = true;
    } 

    return true;
}

bool NetworkControl::doNetworkControl(const QString &action)
{
    QStringList tokens = action.simplified().split(" ");
    tokens.removeFirst();
    int size = tokens.size();

    if ((size >= 2) && (tokens[0] == "ANSWER"))
    {
        answer = tokens[1];
        for (int i = 2; i < size; i++)
            answer += QString(" ") + tokens[i];
        gotAnswer = true;
    }
    else if ((size >= 3) && (tokens[0] == "RESPONSE"))
    {
//        int clientID = tokens[1].toInt();
        answer = tokens[2];
        for (int i = 3; i < size; i++)
            answer += QString(" ") + tokens[i];
        gotAnswer = true;
    }
    return true;
}

void NetworkControl::customEvent(QEvent *e)
{
    if ((MythEvent::Type)(e->type()) == MythEvent::MythEventMessage)
    {
        MythEvent *me = (MythEvent *)e;
        QString message = me->Message();

        if (!m_actions)
            m_actions = new MythActions<NetworkControl>(this, ncActions,
                                                        ncActionCount);
        bool touched;
        m_actions->handleAction(message, touched);
    }
    else if (e->type() == kNetworkControlDataReadyEvent)
    {
        NetworkCommand *nc;
        QString reply;

        QMutexLocker locker(&clientLock);
        QMutexLocker nrLocker(&nrLock);

        while (!networkControlReplies.isEmpty())
        {
            nc = networkControlReplies.front();
            networkControlReplies.pop_front();

            reply = nc->getCommand();

            NetworkControlClient * ncc = nc->getClient();
            if (ncc)
            {
                sendReplyToClient(ncc, reply);
            }
            else //send to all clients
            {
                QList<NetworkControlClient *>::const_iterator it;
                for (it = clients.begin(); it != clients.end(); ++it)
                {
                    NetworkControlClient *ncc = *it;
                    if (ncc)
                        sendReplyToClient(ncc, reply);
                }
            }
            delete nc;
        }
    }
    else if (e->type() == NetworkControlCloseEvent::kEventType)
    {
        NetworkControlCloseEvent *ncce =
            static_cast<NetworkControlCloseEvent*>(e);
        NetworkControlClient     *ncc  = ncce->getClient();

        deleteClient(ncc);
    }
}

QString NetworkControl::listSchedule(const QString& chanID) const
{
    QString result("");
    MSqlQuery query(MSqlQuery::InitCon());
    bool appendCRLF = true;
    QString queryStr("SELECT chanid, starttime, endtime, title, subtitle "
                         "FROM program "
                         "WHERE starttime < :START AND endtime > :END ");

    if (!chanID.isEmpty())
    {
        queryStr += " AND chanid = :CHANID";
        appendCRLF = false;
    }

    queryStr += " ORDER BY starttime, endtime, chanid";

    query.prepare(queryStr);
    query.bindValue(":START", QDateTime::currentDateTime());
    query.bindValue(":END", QDateTime::currentDateTime());
    if (!chanID.isEmpty())
    {
        query.bindValue(":CHANID", chanID);
    }

    if (query.exec())
    {
        while (query.next())
        {
            QString title = query.value(3).toString();
            QString subtitle = query.value(4).toString();

            if (!subtitle.isEmpty())
                title += QString(" -\"%1\"").arg(subtitle);
            QByteArray atitle = title.toLocal8Bit();

            result +=
                QString("%1 %2 %3 %4")
                        .arg(QString::number(query.value(0).toInt())
                             .rightJustified(5, ' '))
                        .arg(query.value(1).toDateTime().toString(Qt::ISODate))
                        .arg(query.value(2).toDateTime().toString(Qt::ISODate))
                        .arg(atitle.constData());

            if (appendCRLF)
                result += "\r\n";
        }
    }
    else
    {
       result = "ERROR: Unable to retrieve current schedule list.";
    }
    return result;
}

QString NetworkControl::listRecordings(QString chanid, QString starttime)
{
    QString result;
    MSqlQuery query(MSqlQuery::InitCon());
    QString queryStr;
    bool appendCRLF = true;

    queryStr = "SELECT chanid, starttime, title, subtitle "
               "FROM recorded WHERE deletepending = 0 ";

    if ((!chanid.isEmpty()) && (!starttime.isEmpty()))
    {
        queryStr += "AND chanid = " + chanid + " "
                    "AND starttime = '" + starttime + "' ";
        appendCRLF = false;
    }

    queryStr += "ORDER BY starttime, title;";

    query.prepare(queryStr);
    if (query.exec())
    {
        QString episode, title, subtitle;
        while (query.next())
        {
            title = query.value(2).toString();
            subtitle = query.value(3).toString();

            if (!subtitle.isEmpty())
                episode = QString("%1 -\"%2\"")
                                  .arg(title)
                                  .arg(subtitle);
            else
                episode = title;

            result +=
                QString("%1 %2 %3").arg(query.value(0).toInt())
                        .arg(query.value(1).toDateTime().toString(Qt::ISODate))
                        .arg(episode);

            if (appendCRLF)
                result += "\r\n";
        }
    }
    else
        result = "ERROR: Unable to retrieve recordings list.";

    return result;
}

QString NetworkControl::listChannels(const uint start, const uint limit) const
{
    QString result;
    MSqlQuery query(MSqlQuery::InitCon());
    QString queryStr;
    uint cnt;
    uint maxcnt;
    uint sqlStart = start;

    // sql starts at zero, we want to start at 1
    if (sqlStart > 0)
        sqlStart--;

    queryStr = "select chanid, callsign, name from channel where visible=1";
    queryStr += " ORDER BY callsign";

    if (limit > 0)  // only if a limit is specified, we limit the results
    {
      QString limitStr = QString(" LIMIT %1,%2").arg(sqlStart).arg(limit);
      queryStr += limitStr;
    }

    query.prepare(queryStr);
    if (!query.exec())
    {
        result = "ERROR: Unable to retrieve channel list.";
        return result;
    }

    maxcnt = query.size();
    cnt = 0;
    if (maxcnt == 0)    // Feedback we have no usefull information
    {
        result += QString("0:0 0 \"Invalid\" \"Invalid\"");
        return result;
    }

    while (query.next())
    {
        // Feedback is as follow:
        // <current line count>:<max line count to expect> <channelid> <callsign name> <channel name>\r\n
        cnt++;
        result += QString("%1:%2 %3 \"%4\" \"%5\"\r\n")
                          .arg(cnt).arg(maxcnt).arg(query.value(0).toInt())
                          .arg(query.value(1).toString())
                          .arg(query.value(2).toString());
    }

    return result;
}

QString NetworkControl::saveScreenshot(NetworkCommand *nc)
{
    int width = 0;
    int height = 0;

    if (nc->getArgCount() == 2)
    {
        QStringList size = nc->getArg(1).split('x');
        if (size.size() == 2)
        {
            width  = size[0].toInt();
            height = size[1].toInt();
        }
    }

    MythMainWindow *window = GetMythMainWindow();
    QStringList args;
    if (width && height)
    {
        args << QString::number(width);
        args << QString::number(height);
    }
    MythEvent* me = new MythEvent(MythEvent::MythEventMessage,
                                  ACTION_SCREENSHOT, args);
    qApp->postEvent(window, me);
    return "OK";
}

QString NetworkCommand::getFrom(int arg)
{
    QString c = m_command;
    for(int i=0 ; i<arg ; i++) {
        QString arg = c.simplified().split(" ")[0];
        c = c.mid(arg.length()).trimmed();
    }
    return c;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */


#ifndef NETWORKCONTROL_H_
#define NETWORKCONTROL_H_

#include <deque>
using namespace std;

#include <QWaitCondition>
#include <QStringList>
#include <QTcpSocket>
#include <QRunnable>
#include <QMutex>
#include <QEvent>

#include "mthread.h"
#include "serverpool.h"
#include "mythactions.h"

class MainServer;
class QTextStream;

// Locking order
// clientLock -> ncLock
//            -> nrLock

class NetworkControlClient : public QObject
{
    Q_OBJECT
  public:
    NetworkControlClient(QTcpSocket *);
   ~NetworkControlClient();

    QTcpSocket  *getSocket()     { return m_socket; }
    QTextStream *getTextStream() { return m_textStream; }

  signals:
    void commandReceived(QString&);

  public slots:
    void readClient();

  private:
    QTcpSocket  *m_socket;
    QTextStream *m_textStream;
};

class NetworkCommand : public QObject
{
    Q_OBJECT
  public:
    NetworkCommand(NetworkControlClient *cli, QString c)
    {
        m_command = c.trimmed();
        m_client = cli;
        m_args = m_command.simplified().split(" ");
    }

    NetworkCommand &operator=(NetworkCommand const &nc)
    {
        m_command = nc.m_command;
        m_client = nc.m_client;
        m_args = m_command.simplified().split(" ");
        return *this;
    }

    QString               getCommand()    { return m_command; }
    NetworkControlClient *getClient()     { return m_client; }
    QString               getArg(int arg) { return m_args[arg]; }
    int                   getArgCount()   { return m_args.size(); }
    QString               getFrom(int arg);

  private:
    QString               m_command;
    NetworkControlClient *m_client;
    QStringList           m_args;
};

class NetworkControlCloseEvent : public QEvent
{
  public:
    NetworkControlCloseEvent(NetworkControlClient *ncc) :
        QEvent(kEventType), m_networkControlClient(ncc) {}

    NetworkControlClient *getClient() { return m_networkControlClient; }

    static Type kEventType;

  private:
    NetworkControlClient * m_networkControlClient;
};

class NetworkControl;

class NetworkControl : public ServerPool, public QRunnable
{
    Q_OBJECT

  public:
    NetworkControl();
    ~NetworkControl();

    bool doMusicControl(const QString &action);
    bool doNetworkControl(const QString &action);

    bool doNetCtrlJump(const QString &action);
    bool doNetCtrlKey(const QString &action);
    bool doNetCtrlPlay(const QString &action);
    bool doNetCtrlQuery(const QString &action);
    bool doNetCtrlSet(const QString &action);
    bool doNetCtrlScreenshot(const QString &action);
    bool doNetCtrlHelp(const QString &action);
    bool doNetCtrlMessage(const QString &action);
    bool doNetCtrlExit(const QString &action);

    bool doPlayFile(const QString &action);
    bool doPlayProgram(const QString &action);
    bool doPlayMusic(const QString &action);
    bool doPlayChanID(const QString &action);
    bool doPlayChannel(const QString &action);
    bool doPlaySeek(const QString &action);
    bool doPlaySpeed(const QString &action);
    bool doPlaySave(const QString &action);
    bool doPlayStop(const QString &action);
    bool doPlayVolume(const QString &action);

    bool doMusicPlay(const QString &action);
    bool doMusicPause(const QString &action);
    bool doMusicStop(const QString &action);
    bool doMusicGetVolume(const QString &action);
    bool doMusicGetMeta(const QString &action);
    bool doMusicSetVolume(const QString &action);
    bool doMusicTrack(const QString &action);
    bool doMusicURL(const QString &action);
    bool doMusicFile(const QString &action);

    bool doChanUp(const QString &action);
    bool doChanDown(const QString &action);
    bool doChanChannel(const QString &action);

    bool doSeekBegin(const QString &action);
    bool doSeekForward(const QString &action);
    bool doSeekBackward(const QString &action);
    bool doSeekTime(const QString &action);

    bool doSpeedNormal(const QString &action);
    bool doSpeedPause(const QString &action);
    bool doSpeedSpeed(const QString &action);

    bool doQueryLocation(const QString &action);
    bool doQueryVerbose(const QString &action);
    bool doQueryLiveTV(const QString &action);
    bool doQueryVersion(const QString &action);
    bool doQueryTime(const QString &action);
    bool doQueryUptime(const QString &action);
    bool doQueryLoad(const QString &action);
    bool doQueryMemstats(const QString &action);
    bool doQueryVolume(const QString &action);
    bool doQueryRecording(const QString &action);
    bool doQueryRecordings(const QString &action);
    bool doQueryChannels(const QString &action);

    bool doHelpJump(const QString &action);
    bool doHelpKey(const QString &action);
    bool doHelpPlay(const QString &action);
    bool doHelpQuery(const QString &action);
    bool doHelpSet(const QString &action);
    bool doHelpScreenshot(const QString &action);
    bool doHelpExit(const QString &action);
    bool doHelpMessage(const QString &action);

  private slots:
    void newConnection(QTcpSocket *socket);
    void receiveCommand(QString &command);
    void deleteClient(void);

  protected:
    void run(void); // QRunnable

  private:
    QString processJump(NetworkCommand *nc);
    QString processKey(NetworkCommand *nc);
    QString processLiveTV(NetworkCommand *nc);
    QString processPlay(NetworkCommand *nc, int clientID);
    QString processQuery(NetworkCommand *nc);
    QString processSet(NetworkCommand *nc);
    QString processMessage(NetworkCommand *nc);
    QString processHelp(NetworkCommand *nc);

    void notifyDataAvailable(void);
    void sendReplyToClient(NetworkControlClient *ncc, QString &reply);
    void customEvent(QEvent *e);

    QString listRecordings(QString chanid = "", QString starttime = "");
    QString listSchedule(const QString& chanID = "") const;
    QString listChannels(const uint start, const uint limit) const;
    QString saveScreenshot(NetworkCommand *nc);

    void processNetworkControlCommand(NetworkCommand *nc);

    void deleteClient(NetworkControlClient *ncc);

    QString prompt;
    bool gotAnswer;
    QString answer;
    QMap <QString, QString> jumpMap;
    QMap <QString, int> keyMap;
    QMap <int, QString> keyTextMap;

    mutable QMutex  clientLock;
    QList<NetworkControlClient*> clients;

    QList<NetworkCommand*> networkControlCommands; // protected by ncLock
    QMutex ncLock;
    QWaitCondition ncCond; // protected by ncLock

    QList<NetworkCommand*> networkControlReplies;
    QMutex nrLock;

    MThread *commandThread;
    bool stopCommandThread; // protected by ncLock

    MythActions<NetworkControl> *m_actions;
    MythActions<NetworkControl> *m_netCtrlCmdActions;
    MythActions<NetworkControl> *m_playCmdActions;
    MythActions<NetworkControl> *m_musicCmdActions;
    MythActions<NetworkControl> *m_chanCmdActions;
    MythActions<NetworkControl> *m_seekCmdActions;
    MythActions<NetworkControl> *m_speedCmdActions;
    MythActions<NetworkControl> *m_queryCmdActions;
    MythActions<NetworkControl> *m_helpCmdActions;

    NetworkCommand *m_netCtrlCmd;
    int m_netCtrlClient;
    QString m_netCtrlResult;
    QString m_netCtrlMessage;
};

#endif

/* vim: set expandtab tabstop=4 shiftwidth=4: */


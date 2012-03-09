#ifndef MYTHFLASHPLAYER_H
#define MYTHFLASHPLAYER_H

#include <mythscreentype.h>
#include "mythactions.h"

class MythUIWebBrowser;

class MythFlashPlayer : public MythScreenType
{
  Q_OBJECT

  public:
    MythFlashPlayer(MythScreenStack *parent, QStringList &urlList);
    ~MythFlashPlayer();

    bool Create(void);
    bool keyPressEvent(QKeyEvent *);

    bool doPause(const QString &action);
    bool doInfo(const QString &action);
    bool doSeekFfwd(const QString &action);
    bool doSeekRwnd(const QString &action);
    bool doChanUp(const QString &action);
    bool doChanDown(const QString &action);
    bool doVolumeUp(const QString &action);
    bool doVolumeDown(const QString &action);

  private:
    QVariant evaluateJavaScript(const QString&);
    MythUIWebBrowser*         m_browser;
    QString                   m_url;
    int                       m_fftime;
    int                       m_rewtime;
    int                       m_jumptime;

    MythActions<MythFlashPlayer> *m_actions;
};

#endif

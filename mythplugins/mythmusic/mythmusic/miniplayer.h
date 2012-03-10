#ifndef MINIPLAYER_H_
#define MINIPLAYER_H_

#include <mythscreentype.h>

#include "musiccommon.h"
#include "mythactions.h"

class QTimer;

class MPUBLIC MiniPlayer : public MusicCommon
{
  Q_OBJECT

  public:
    MiniPlayer(MythScreenStack *parent);
    ~MiniPlayer();

    bool Create(void);
    bool keyPressEvent(QKeyEvent *);

    bool doSelect(const QString &action);
    bool doEscape(const QString &action);
    bool doMenu(const QString &action);

  public slots:
    void timerTimeout(void);

  private:
    QTimer *m_displayTimer;

    MythActions<MiniPlayer> *m_actions;
};

#endif

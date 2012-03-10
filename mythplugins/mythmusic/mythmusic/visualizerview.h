#ifndef VISUALIZERVIEW_H_
#define VISUALIZERVIEW_H_

// qt
#include <QEvent>
#include <QVector>

// mythui
#include <mythscreentype.h>

// mythmusic
#include <musiccommon.h>
#include "mythactions.h"

class MythUIVideo;

class VisualizerView : public MusicCommon
{
    Q_OBJECT
  public:
    VisualizerView(MythScreenStack *parent);
    ~VisualizerView(void);

    bool Create(void);
    bool keyPressEvent(QKeyEvent *);

    virtual void ShowMenu(void);

    bool doInfo(const QString &action);

  protected:
    void customEvent(QEvent *event);

  private slots:
    void showTrackInfoPopup(void);

  private:
    MythActions<VisualizerView> *m_actions;
};

class MPUBLIC TrackInfoPopup : public MythScreenType
{
  Q_OBJECT
  public:
    TrackInfoPopup(MythScreenStack *parent, Metadata *mdata);
    ~TrackInfoPopup(void);

    bool Create(void);
    bool keyPressEvent(QKeyEvent *event);

    bool doInfo(const QString &action);

  protected:
    Metadata *m_metadata;
    QTimer   *m_displayTimer;

  private:
    MythActions<TrackInfoPopup> *m_actions;
};

#endif

#ifndef MYTHWIDGETS_H_
#define MYTHWIDGETS_H_

#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QDialog>
#include <QCheckBox>
#include <QRadioButton>
#include <QImage>
#include <QLabel>
#include <QTimer>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QTextEdit>
#include <QListWidget>

#include <vector>

using namespace std;

#include "virtualkeyboard_qt.h"
#include "mythactions.h"

#include "mythexp.h"

// These widgets follow these general navigation rules:
//
// - Up and Down shift focus to the previous/next widget in the tab
// order
// - Left and Right adjust the current setting
// - Space selects

class MythComboBox;
class MPUBLIC MythComboBox: public QComboBox
{
    Q_OBJECT

  public:
    MythComboBox(bool rw, QWidget* parent=0, const char* name="MythComboBox");

    void setHelpText(const QString &help);

    void setAcceptOnSelect(bool Accept)      { AcceptOnSelect = Accept; }
    void setStep(int _step = 1)              { step = _step; }
    void setAllowVirtualKeyboard(bool allowKbd = true)
    { allowVirtualKeyboard = allowKbd; }
    void setPopupPosition(PopupPositionQt pos) { popupPosition = pos; }
    PopupPositionQt getPopupPosition(void)     { return popupPosition; }

    bool doUp(const QString &action);
    bool doDown(const QString &action);
    bool doLeft(const QString &action);
    bool doRight(const QString &action);
    bool doPgDown(const QString &action);
    bool doPgUp(const QString &action);
    bool doSelect(const QString &action);

  signals:
    void changeHelpText(QString);
    void accepted(int);
    void gotFocus();

  public slots:
    virtual void deleteLater(void);
    void insertItem(const QString &item)
    {
        QComboBox::insertItem(count()+1, item);
    }

  protected:
    void Teardown(void);
    virtual ~MythComboBox(); // use deleteLater for thread safety
    virtual void keyPressEvent (QKeyEvent *e);
    virtual void focusInEvent(QFocusEvent *e);
    virtual void focusOutEvent(QFocusEvent *e);
    void Init(void);
    virtual void popupVirtualKeyboard(void);

  private:
    VirtualKeyboardQt *popup;
    QString helptext;
    bool AcceptOnSelect;
    bool useVirtualKeyboard;
    bool allowVirtualKeyboard;
    PopupPositionQt popupPosition;
    int step;

    MythActions<MythComboBox> *m_actions;
    QKeyEvent *m_actionEvent;
};

class MPUBLIC MythSpinBox: public QSpinBox
{
    Q_OBJECT

  public:
    MythSpinBox(QWidget* parent = NULL, const char* name = "MythSpinBox",
                bool allow_single_step = false);
    ~MythSpinBox();

    void setHelpText(const QString&);

    bool allowSingleStep(void)               { return allowsinglestep; }
    void setAllowSingleStep(bool arg = true) { allowsinglestep = arg; }

    bool doUp(const QString &action);
    bool doDown(const QString &action);
    bool doLeft(const QString &action);
    bool doRight(const QString &action);
    bool doPgDown(const QString &action);
    bool doPgUp(const QString &action);
    bool doSelect(const QString &action);

  signals:
    void changeHelpText(QString);

  protected:
    virtual void keyPressEvent(QKeyEvent* e);
    virtual void focusInEvent(QFocusEvent *e);
    virtual void focusOutEvent(QFocusEvent *e);

  private:
    QString helptext;
    bool allowsinglestep;

    MythActions<MythSpinBox> *m_actions;
};

class MPUBLIC MythSlider: public QSlider
{
    Q_OBJECT

  public:
    MythSlider(QWidget* parent=0, const char* name="MythSlider");
    ~MythSlider();

    void setHelpText(const QString&);
    bool doUp(const QString &action);
    bool doDown(const QString &action);
    bool doLeft(const QString &action);
    bool doRight(const QString &action);
    bool doSelect(const QString &action);

  signals:
    void changeHelpText(QString);

  protected:
    virtual void keyPressEvent (QKeyEvent* e);
    virtual void focusInEvent(QFocusEvent *e);
    virtual void focusOutEvent(QFocusEvent *e);

  private:
    QString helptext;

    MythActions<MythSlider> *m_actions;
};

class MPUBLIC MythLineEdit : public QLineEdit
{
    Q_OBJECT

  public:
    MythLineEdit(QWidget *parent=NULL, const char *name="MythLineEdit");
    MythLineEdit(const QString &text,
                 QWidget *parent=NULL, const char *name="MythLineEdit");

    void setHelpText(const QString&);
    void setRW(bool readwrite = true) { rw = readwrite; };
    void setRO() { rw = false; };
    void setAllowVirtualKeyboard(bool allowKbd = true)
        { allowVirtualKeyboard = allowKbd; }
    // muthui's MythUITextEdit m_Filter & FilterNumeric
    // may be a better way to do it
    //void setSmartVirtualKeyboard(bool allowKbd = true)
    //       { allowSmartKeyboard   = allowKbd; }
    void setPopupPosition(PopupPositionQt pos) { popupPosition = pos; }
    PopupPositionQt getPopupPosition(void) { return popupPosition; }

    virtual QString text();

    bool doUp(const QString &action);
    bool doDown(const QString &action);
    bool doSelect(const QString &action);

  public slots:
    virtual void deleteLater(void);
    virtual void setText(const QString &text);

  signals:
    void changeHelpText(QString);

  protected:
    void Teardown(void);
    virtual ~MythLineEdit(); // use deleteLater for thread safety

    virtual void keyPressEvent(QKeyEvent *e);
    virtual void focusInEvent(QFocusEvent *e);
    virtual void focusOutEvent(QFocusEvent *e);
    virtual void hideEvent(QHideEvent *e);
    virtual void mouseDoubleClickEvent(QMouseEvent *e);
    virtual void popupVirtualKeyboard(void);

  private:
    VirtualKeyboardQt *popup;
    QString helptext;
    bool rw;
    bool useVirtualKeyboard;
    bool allowVirtualKeyboard;
    PopupPositionQt popupPosition;

    MythActions<MythLineEdit> *m_actions;
    QKeyEvent *m_actionEvent;
};

/**
 * A LineEdit that does special things when you press number keys
 * (enter letters with multiple presses, just like a phone keypad)
 */
class MPUBLIC MythRemoteLineEdit : public QTextEdit
{
    Q_OBJECT

  public:
    MythRemoteLineEdit(QWidget *parent,
                       const char *name = "MythRemoteLineEdit");
    MythRemoteLineEdit(const QString &contents, QWidget *parent,
                       const char *name = "MythRemoteLineEdit");
    MythRemoteLineEdit(QFont *a_font, QWidget *parent,
                       const char *name = "MythRemoteLineEdit");
    MythRemoteLineEdit(int lines, QWidget *parent,
                       const char *name = "MythRemoteLineEdit");

    void setHelpText(const QString&);
    void setCycleTime(float desired_interval); // in seconds
    void setCharacterColors(QColor unselected, QColor selected, QColor special);
    void insert(QString text);
    void backspace();
    void del();
    void setPopupPosition(PopupPositionQt pos) { popupPosition = pos; };
    PopupPositionQt getPopupPosition(void)     { return popupPosition; };

    virtual QString text();

    bool doUp(const QString &action);
    bool doDown(const QString &action);
    bool doSelect(const QString &action);
    bool doEscape(const QString &action);

  signals:
    void    shiftState(bool);
    void    cycleState(QString current_choice, QString set);
    void    changeHelpText(QString);
    void    gotFocus();
    void    lostFocus();
    void    tryingToLooseFocus(bool up_or_down);
    void    textChanged(QString);

  public slots:
    virtual void deleteLater(void);
    virtual void setText(const QString& text);

  protected:
    void Teardown(void);
    virtual ~MythRemoteLineEdit(); // use deleteLater for thread safety
    virtual void focusInEvent(QFocusEvent *e);
    virtual void focusOutEvent(QFocusEvent *e);
    virtual void keyPressEvent(QKeyEvent *e);
    virtual void popupVirtualKeyboard(void);

  private slots:
    void    startCycle(QString current_choice, QString set);
    void    updateCycle(QString current_choice, QString set);
    void    endCycle(bool select);
    void    endCycle(void) { endCycle(true); }

  private:
    QFont   *my_font;
    void    Init(void);
    void    cycleKeys(QString cycleList);
    void    toggleShift(void);

    bool    shift;
    QTimer  *cycle_timer;
    bool    active_cycle;
    QString current_choice;
    QString current_set;
    int     cycle_time;
    QString helptext;

    int pre_cycle_pos;
    QString pre_cycle_text_before_cursor;
    QString pre_cycle_text_after_cursor;

    QColor  col_unselected;
    QColor  col_selected;
    QColor  col_special;

    QString  hex_unselected;
    QString  hex_selected;
    QString  hex_special;

    int m_lines;

    VirtualKeyboardQt *popup;
    bool             useVirtualKeyboard;
    PopupPositionQt    popupPosition;

    MythActions<MythRemoteLineEdit> *m_actions;
    QKeyEvent *m_actionEvent;
};

class MPUBLIC MythPushButton : public QPushButton
{
    Q_OBJECT

  public:
    MythPushButton(QWidget *parent, const char *name = "MythPushButton");

    MythPushButton(const QString &text, QWidget *parent);

    MythPushButton(const QString &ontext, const QString &offtext,
                   QWidget *parent, bool isOn = true);
    ~MythPushButton();

    void setHelpText(const QString &help);

    void keyPressEvent(QKeyEvent *e);
    void keyReleaseEvent(QKeyEvent *e);

    void toggleText(void);

    bool doSelect(const QString &action);

  signals:
    void changeHelpText(QString);

  protected:
    void focusInEvent(QFocusEvent *e);
    void focusOutEvent(QFocusEvent *e);

  private:
    QColor origcolor;
    QString helptext;
    QString onText;
    QString offText;

    QStringList keyPressActions;

    MythActions<MythPushButton> *m_actions;
};

class MPUBLIC MythCheckBox: public QCheckBox
{
    Q_OBJECT

  public:
    MythCheckBox(QWidget *parent = 0, const char *name = "MythCheckBox");
    MythCheckBox(const QString &text, QWidget *parent = 0,
                 const char *name = "MythCheckBox");
    ~MythCheckBox();

    void setHelpText(const QString&);
    bool doUp(const QString &action);
    bool doDown(const QString &action);
    bool doToggle(const QString &action);

  signals:
    void changeHelpText(QString);

  protected:
    virtual void keyPressEvent(QKeyEvent* e);
    virtual void focusInEvent(QFocusEvent *e);
    virtual void focusOutEvent(QFocusEvent *e);

  private:
    QString helptext;
    MythActions<MythCheckBox> *m_actions;
};

class MPUBLIC MythRadioButton: public QRadioButton
{
    Q_OBJECT

  public:
    MythRadioButton(QWidget* parent = 0, const char* name = "MythRadioButton");
    ~MythRadioButton();

    void setHelpText(const QString&);

    bool doUp(const QString &action);
    bool doDown(const QString &action);
    bool doToggle(const QString &action);

  signals:
    void changeHelpText(QString);

  protected:
    virtual void keyPressEvent(QKeyEvent* e);
    virtual void focusInEvent(QFocusEvent *e);
    virtual void focusOutEvent(QFocusEvent *e);

  private:
    QString helptext;

    MythActions<MythRadioButton> *m_actions;
};

class MPUBLIC MythListBox: public QListWidget
{
    Q_OBJECT

  public:
    MythListBox(QWidget       *parent,
                const QString &name = QString("MythListBox"));
    ~MythListBox();

    virtual void keyPressEvent(QKeyEvent* e);

#if QT_VERSION < 0x040400
    void setCurrentRow(int row) { QListWidget::setCurrentRow(row); }
    void setCurrentRow(int row, QItemSelectionModel::SelectionFlags command)
    {
        selectionModel()->setCurrentIndex(indexFromItem(item(row)), command);
    }
#endif

    QString currentText(void) const { return text(currentRow()); }

    void setTopRow(uint row);
    void insertItem(const QString&);
    void insertStringList(const QStringList&);
    void removeRow(uint row);
    void changeItem(const QString&, uint row);
    int  getIndex(const QList<QListWidgetItem*>&);
    QList<QListWidgetItem*> findItems(
        const QString &text, Qt::MatchFlags flags = Qt::MatchStartsWith) const
    {
        return QListWidget::findItems(text, flags);
    }


    void setHelpText(const QString&);

    bool doUp(const QString &action);
    bool doDown(const QString &action);
    bool doLeft(const QString &action);
    bool doRight(const QString &action);
    bool doPgDown(const QString &action);
    bool doPgUp(const QString &action);
    bool doDigit(const QString &action);
    bool doPrevView(const QString &action);
    bool doNextView(const QString &action);
    bool doMenu(const QString &action);
    bool doEdit(const QString &action);
    bool doDelete(const QString &action);
    bool doSelect(const QString &action);

  protected:
    void focusInEvent(QFocusEvent *e);
    void focusOutEvent(QFocusEvent *e);
    virtual void ensurePolished(void) const;

    bool itemVisible(uint row) const;
    QString text(uint row) const;

  public slots:
    void setCurrentItem(const QString& matchText, bool caseSensitive = true,
                        bool partialMatch = false);

  signals:
    void changeHelpText(QString);
    void accepted(int);
    void menuButtonPressed(int);
    void editButtonPressed(int);
    void deleteButtonPressed(int);
    void highlighted(int);

  private slots:
    void HandleItemSelectionChanged(void);

  private:
    void propagateKey(int key);

    QString helptext;

    MythActions<MythListBox> *m_actions;
};

#endif

/* vim: set expandtab tabstop=4 shiftwidth=4: */

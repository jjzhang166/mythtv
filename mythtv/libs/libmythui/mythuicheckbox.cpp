
// Own header
#include "mythuicheckbox.h"

// Myth headers
#include "mythlogging.h"

// MythUI headers
#include "mythmainwindow.h"
#include "mythgesture.h"
#include "mythactions.h"


static struct ActionDefStruct<MythUICheckBox> mucbActions[] = {
    { "SELECT", &MythUICheckBox::doSelect }
};
static int mucbActionCount = NELEMS(mucbActions);

MythUICheckBox::MythUICheckBox(MythUIType *parent, const QString &name) :
    MythUIType(parent, name), m_actions(NULL)
{
    m_currentCheckState = MythUIStateType::Off;
    m_state = "active";

    m_BackgroundState = m_CheckState = NULL;

    connect(this, SIGNAL(TakingFocus()), this, SLOT(Select()));
    connect(this, SIGNAL(LosingFocus()), this, SLOT(Deselect()));
    connect(this, SIGNAL(Enabling()), this, SLOT(Enable()));
    connect(this, SIGNAL(Disabling()), this, SLOT(Disable()));

    SetCanTakeFocus(true);
}

MythUICheckBox::~MythUICheckBox()
{
    if (m_actions)
        delete m_actions;
}

void MythUICheckBox::SetInitialStates()
{
    m_BackgroundState = dynamic_cast<MythUIStateType *>(GetChild("background"));
    m_CheckState = dynamic_cast<MythUIStateType *>(GetChild("checkstate"));

    if (!m_CheckState || !m_BackgroundState)
        LOG(VB_GENERAL, LOG_ERR,
            QString("Checkbox %1 is missing required elements")
            .arg(objectName()));

    if (m_CheckState)
        m_CheckState->DisplayState(m_currentCheckState);

    if (m_BackgroundState)
        m_BackgroundState->DisplayState(m_state);
}


void MythUICheckBox::toggleCheckState()
{
    bool onOff = false;

    if (m_currentCheckState != MythUIStateType::Full)
    {
        m_currentCheckState = MythUIStateType::Full;
        onOff = true;
    }
    else
    {
        m_currentCheckState = MythUIStateType::Off;
        onOff = false;
    }

    if (m_CheckState)
        m_CheckState->DisplayState(m_currentCheckState);

    emit DependChanged(!onOff);
    emit toggled(onOff);
    emit valueChanged();
}

void MythUICheckBox::SetCheckState(MythUIStateType::StateType state)
{
    m_currentCheckState = state;
    if (m_CheckState)
        m_CheckState->DisplayState(state);

    if (state == MythUIStateType::Off)
        emit DependChanged(true);
    else
        emit DependChanged(false);
    emit valueChanged();
}

void MythUICheckBox::SetCheckState(bool onOff)
{
    if (onOff)
    {
        m_currentCheckState = MythUIStateType::Full;
    }
    else
    {
        m_currentCheckState = MythUIStateType::Off;
    }

    if (m_CheckState)
        m_CheckState->DisplayState(m_currentCheckState);

    emit toggled(onOff);
    emit DependChanged(!onOff);
    emit valueChanged();
}

MythUIStateType::StateType MythUICheckBox::GetCheckState() const
{
    return m_currentCheckState;
}

bool MythUICheckBox::GetBooleanCheckState() const
{
    return m_currentCheckState == MythUIStateType::Full;
}

void MythUICheckBox::Select()
{
    if (!IsEnabled())
        return;

    m_state = "selected";
    if (m_BackgroundState)
        m_BackgroundState->DisplayState(m_state);
}

void MythUICheckBox::Deselect()
{
    if (IsEnabled())
        m_state = "active";
    else
        m_state = "disabled";

    if (m_BackgroundState)
        m_BackgroundState->DisplayState(m_state);
}

void MythUICheckBox::Enable()
{
    m_state = "active";
    if (m_BackgroundState)
        m_BackgroundState->DisplayState(m_state);
}

void MythUICheckBox::Disable()
{
    m_state = "disabled";
    if (m_BackgroundState)
        m_BackgroundState->DisplayState(m_state);
}

/** \brief Mouse click/movement handler, receives mouse gesture events from the
 *         QCoreApplication event loop. Should not be used directly.
 *
 *  \param uitype The mythuitype receiving the event
 *  \param event Mouse event
 */
bool MythUICheckBox::gestureEvent(MythGestureEvent *event)
{
    if (event->gesture() == MythGestureEvent::Click)
    {
        if (IsEnabled())
        {
            toggleCheckState();
            return true;
        }
    }

    return false;
}

bool MythUICheckBox::doSelect(const QString &action)
{
    toggleCheckState();
    return true;
}

/** \brief Key event handler
 *
 *  \param event Keypress event
 */
bool MythUICheckBox::keyPressEvent(QKeyEvent *event)
{
    QStringList actions;
    bool handled = false;
    handled = GetMythMainWindow()->TranslateKeyPress("Global", event, actions);

    if (!handled)
    {
        if (!m_actions)
            m_actions = new MythActions<MythUICheckBox>(this, mucbActions,
                                                        mucbActionCount);
        handled = m_actions->handleActions(actions);
    }

    return handled;
}

/**
 *  \copydoc MythUIType::CreateCopy()
 */
void MythUICheckBox::CreateCopy(MythUIType *parent)
{
    MythUICheckBox *checkbox = new MythUICheckBox(parent, objectName());
    checkbox->CopyFrom(this);
}

/**
 *  \copydoc MythUIType::CopyFrom()
 */
void MythUICheckBox::CopyFrom(MythUIType *base)
{
    MythUICheckBox *button = dynamic_cast<MythUICheckBox *>(base);

    if (!button)
    {
        LOG(VB_GENERAL, LOG_ERR, "Dynamic cast of base failed");
        return;
    }

    MythUIType::CopyFrom(base);

    SetInitialStates();
}

/**
 *  \copydoc MythUIType::Finalize()
 */
void MythUICheckBox::Finalize()
{
    SetInitialStates();
}

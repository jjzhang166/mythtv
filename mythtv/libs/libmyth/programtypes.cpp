// -*- Mode: c++ -*-

#include <QDateTime>
#include <QMutex>

#include "programtypes.h"
#include "mythactions.h"

const char *kPlayerInUseID           = "player";
const char *kPIPPlayerInUseID        = "pipplayer";
const char *kPBPPlayerInUseID        = "pbpplayer";
const char *kImportRecorderInUseID   = "import_recorder";
const char *kRecorderInUseID         = "recorder";
const char *kFileTransferInUseID     = "filetransfer";
const char *kTruncatingDeleteInUseID = "truncatingdelete";
const char *kFlaggerInUseID          = "flagger";
const char *kTranscoderInUseID       = "transcoder";
const char *kPreviewGeneratorInUseID = "preview_generator";
const char *kJobQueueInUseID         = "jobqueue";
const char *kCCExtractorInUseID      = "ccextractor";

template <> struct RecTypeItem<MarkEnumType> MarkType::m_items[] =
{
    REC_ITEM(MARK_ALL, "ALL", ""),
    REC_ITEM(MARK_UNKNOWN, "UNKNOWN", ""),
    REC_ITEM(MARK_UNSET, "UNSET", ""),
    REC_ITEM(MARK_TMP_CUT_END, "TMP_CUT_END", ""),
    REC_ITEM(MARK_TMP_CUT_START, "TMP_CUT_START", ""),
    REC_ITEM(MARK_UPDATED_CUT, "UPDATED_CUT", ""),
    REC_ITEM(MARK_PLACEHOLDER, "PLACEHOLDER", ""),
    REC_ITEM(MARK_CUT_END, "CUT_END", ""),
    REC_ITEM(MARK_CUT_START, "CUT_START", ""),
    REC_ITEM(MARK_BOOKMARK, "BOOKMARK", ""),
    REC_ITEM(MARK_BLANK_FRAME, "BLANK_FRAME", ""),
    REC_ITEM(MARK_COMM_START, "COMM_START", ""),
    REC_ITEM(MARK_COMM_END, "COMM_END", ""),
    REC_ITEM(MARK_GOP_START, "GOP_START", ""),
    REC_ITEM(MARK_KEYFRAME, "KEYFRAME", ""),
    REC_ITEM(MARK_SCENE_CHANGE, "SCENE_CHANGE", ""),
    REC_ITEM(MARK_GOP_BYFRAME, "GOP_BYFRAME", ""),
    REC_ITEM(MARK_ASPECT_1_1, "ASPECT 1:1", ""),
    REC_ITEM(MARK_ASPECT_4_3, "ASPECT 4:3", ""),
    REC_ITEM(MARK_ASPECT_16_9, "ASPECT 16:9", ""),
    REC_ITEM(MARK_ASPECT_2_21_1, "ASPECT 2.21:1", ""),
    REC_ITEM(MARK_ASPECT_CUSTOM, "ASPECT CUSTOM", ""),
    REC_ITEM(MARK_VIDEO_WIDTH, "VIDEO_WIDTH", ""),
    REC_ITEM(MARK_VIDEO_HEIGHT, "VIDEO_HEIGHT", ""),
    REC_ITEM(MARK_VIDEO_RATE, "VIDEO_RATE", ""),
    REC_ITEM(MARK_DURATION_MS, "DURATION MS", ""),
    REC_ITEM(MARK_TOTAL_FRAMES, "TOTAL_FRAMES", "")
};
template <> int MarkType::m_itemCount = NELEMS(MarkType::m_items);

template <> MarkEnumType MarkType::m_defaultValue = MARK_UNKNOWN;
template <> const char * MarkType::m_defaultString = "Unknown";
template <> QHash<QString, struct RecTypeItem<MarkEnumType> *> *
            MarkType::m_stringHash = NULL;
template <> QHash<MarkEnumType, struct RecTypeItem<MarkEnumType> *> *
            MarkType::m_hash = NULL;

#define REC_STATUS_PRIV_ITEM(t,letter,ui,descr) \
    { (t), (ui), QT_TR_NOOP(letter), "RecStatusChar ##t", QT_TR_NOOP(descr) }

template <> struct RecTypeItem<RecStatusEnumType> RecStatusBaseType::m_items[] =
{
    REC_ITEM(rsMissedFuture, "Missed Future", ""),
    REC_ITEM(rsTuning, "Tuning", ""),
    REC_ITEM(rsFailed, "Recorder Failed", ""),
    REC_ITEM(rsTunerBusy, "Tuner Busy", ""),
    REC_ITEM(rsLowDiskSpace, "Low Disk Space", ""),
    REC_ITEM(rsCancelled, "Manual Cancel", ""),
    REC_ITEM(rsMissed, "Missed", ""),
    REC_ITEM(rsAborted, "Aborted", ""),
    REC_ITEM(rsRecorded, "Recorded", ""),
    REC_ITEM(rsRecording, "Recording", ""),
    REC_ITEM(rsWillRecord, "Will Record", ""),
    REC_ITEM(rsUnknown, "Unknown", ""),
    REC_ITEM(rsDontRecord, "Don't Record", ""),
    REC_ITEM(rsPreviousRecording, "Previously Recorded", ""),
    REC_ITEM(rsCurrentRecording, "Currently Recorded", ""),
    REC_ITEM(rsEarlierShowing, "Earlier Showing", ""),
    REC_ITEM(rsTooManyRecordings, "Max Recordings", ""),
    REC_ITEM(rsNotListed, "Not Listed", ""),
    REC_ITEM(rsConflict, "Conflicting", ""),
    REC_ITEM(rsLaterShowing, "Later Showing", ""),
    REC_ITEM(rsRepeat, "Repeat", ""),
    REC_ITEM(rsInactive, "Inactive", ""),
    REC_ITEM(rsNeverRecord, "Never Record", ""),
    REC_ITEM(rsOffLine, "Recorder Off-Line", ""),
    REC_ITEM(rsOtherShowing, "Other Showing", "")
};
template <> int RecStatusBaseType::m_itemCount =
            NELEMS(RecStatusBaseType::m_items);

template <> RecStatusPrivItem RecStatusType::m_privItems[] = {
    REC_STATUS_PRIV_ITEM(rsMissedFuture, "M", "disabled",
                         "This showing was not recorded because the "
                         "master backend was hung or not running."),
    REC_STATUS_PRIV_ITEM(rsTuning, "t", "running",
                         "Thsi channel is being tuned."),
    REC_STATUS_PRIV_ITEM(rsFailed, "f", "error",
                         "The recorder failed to record."),
    REC_STATUS_PRIV_ITEM(rsTunerBusy, "B", "error",
                         "The tuner card was already being used."),
    REC_STATUS_PRIV_ITEM(rsLowDiskSpace, "K", "disabled",
                         "There wasn't enough disk space available."),
    REC_STATUS_PRIV_ITEM(rsCancelled, "c", "disabled",
                         "This showing was not recorded because it "
                         "was manually cancelled."),
    REC_STATUS_PRIV_ITEM(rsMissed, "M", "error",
                         "This showing was not recorded because the "
                         "master backend was hung or not running."),
    REC_STATUS_PRIV_ITEM(rsAborted, "A", "error",
                         "This showing was recorded but was aborted "
                         "before recording was completed."),
    REC_STATUS_PRIV_ITEM(rsRecorded, "R", "normal",
                         "This showing was recorded."),
    REC_STATUS_PRIV_ITEM(rsRecording, "", "running",
                         "This showing is being recorded."),
    REC_STATUS_PRIV_ITEM(rsWillRecord, "", "normal",
                         "This showing will be recorded."),
    REC_STATUS_PRIV_ITEM(rsUnknown, "-", "disabled",
                         "The status of this showing is unknown."),
    REC_STATUS_PRIV_ITEM(rsDontRecord, "X", "disabled",
                         "it was manually set to not record."),
    REC_STATUS_PRIV_ITEM(rsPreviousRecording, "P", "disabled",
                         "this episode was previously recorded "
                         "according to the duplicate policy chosen "
                         "for this title."),
    REC_STATUS_PRIV_ITEM(rsCurrentRecording, "R", "disabled",
                         "this episode was previously recorded and "
                         "is still available in the list of "
                         "recordings."),
    REC_STATUS_PRIV_ITEM(rsEarlierShowing, "E", "warning",
                         "this episode will be recorded at an "
                         "earlier time instead."),
    REC_STATUS_PRIV_ITEM(rsTooManyRecordings, "T", "warning",
                         "too many recordings of this program have "
                         "already been recorded."),
    REC_STATUS_PRIV_ITEM(rsNotListed, "N", "warning",
                         "this rule does not match any showings in "
                         "the current program listings."),
    REC_STATUS_PRIV_ITEM(rsConflict, "C", "error",
                         "another program with a higher priority "
                         "will be recorded."),
    REC_STATUS_PRIV_ITEM(rsLaterShowing, "L", "warning",
                         "this episode will be recorded at a later time."),
    REC_STATUS_PRIV_ITEM(rsRepeat, "r", "disabled",
                         "this episode is a repeat."),
    REC_STATUS_PRIV_ITEM(rsInactive, "x", "warning",
                         "this recording rule is inactive."),
    REC_STATUS_PRIV_ITEM(rsNeverRecord, "V", "disabled",
                         "it was marked to never be recorded."),
    REC_STATUS_PRIV_ITEM(rsOffLine, "F", "error",
                         "the backend recorder is off-line."),
    REC_STATUS_PRIV_ITEM(rsOtherShowing, "O", "disabled",
                         "this episode will be recorded on a different "
                         "channel in this time slot.")
};
template <> int RecStatusType::m_privItemCount =
            NELEMS(RecStatusType::m_privItems);

template <> RecStatusEnumType RecStatusBaseType::m_defaultValue = rsUnknown;
template <> const char * RecStatusBaseType::m_defaultString = "Unknown";
template <> QHash<QString, struct RecTypeItem<RecStatusEnumType> *> *
            RecStatusBaseType::m_stringHash = NULL;
template <> QHash<RecStatusEnumType, struct RecTypeItem<RecStatusEnumType> *> *
            RecStatusBaseType::m_hash = NULL;
template <> bool RecStatusType::m_privInit = false;
template <> const char * RecStatusType::m_defaultChar = "-";
template <> const char * RecStatusType::m_defaultUI = "warning";
template <> const char * RecStatusType::m_defaultDescr =
            "The status of this showing is unknown.";


void RecStatusType::hashPrivInit(void)
{
    if (m_privInit)
        return;

    if (!m_hash)
        hashInit();

    for (int i = 0; i < m_privItemCount; i++)
    {
        RecStatusPrivItem *privItem = &m_privItems[i];
        struct RecTypeItem<RecStatusEnumType> *item =
            m_hash->value(privItem->type);

        if (item)
            item->priv = privItem;
    }

    m_privInit = true;
}

QString RecStatusType::toUIState(void)
{
    if (!m_hash)
        hashPrivInit();

    struct RecTypeItem<RecStatusEnumType> *item =
        m_hash->value(m_value, NULL);

    if (!item || !item->priv)
        return QString(m_defaultUI);

    RecStatusPrivItem *privItem = (RecStatusPrivItem *)item->priv;
    return QString(privItem->uiText);
}

/// \brief Converts "recstatus" into a human readable string.
QString RecStatusType::toString(uint id)
{
    if (m_value == rsRecording || m_value == rsWillRecord)
        return QString::number(id);

    if (!m_hash)
        hashPrivInit();

    struct RecTypeItem<RecStatusEnumType> *item =
        m_hash->value(m_value, NULL);

    if (!item || !item->priv)
        return QString(m_defaultChar);

    RecStatusPrivItem *privItem = (RecStatusPrivItem *)item->priv;
    if (!privItem->charVal[0])
        return QString(m_defaultChar);

    return QObject::tr(privItem->charVal, privItem->charType);
}

/// \brief Converts "recstatus" into a short human readable description.
QString RecStatusType::toString(RecordingType rectype)
{
    if (m_value == rsUnknown && rectype == kNotRecording)
        return QObject::tr("Not Recording");

    if (!m_hash)
        hashPrivInit();

    struct RecTypeItem<RecStatusEnumType> *item =
        m_hash->value(m_value, NULL);

    if (!item)
        return QObject::tr(m_defaultString);

    return QObject::tr(item->rawString);
}

/// \brief Converts "recstatus" into a long human readable description.
QString RecStatusType::toDescription(RecordingType rectype,
                                     const QDateTime &recstartts)
{
    if (m_value == rsUnknown && rectype == kNotRecording)
        return QObject::tr("This showing is not scheduled to record");

    QString message;
    QDateTime now = QDateTime::currentDateTime();

    if (!m_hash)
        hashPrivInit();

    struct RecTypeItem<RecStatusEnumType> *item =
        m_hash->value(m_value, NULL);

    if (!item || !item->priv)
        return QObject::tr(m_defaultString);

    RecStatusPrivItem *privItem = (RecStatusPrivItem *)item->priv;
    message = QObject::tr(privItem->descrText);
    if (m_value <= rsUnknown)
        return message;

    if (recstartts > now)
        message = QObject::tr("This showing will not be recorded because ") +
                  message;
    else
        message = QObject::tr("This showing was not recorded because ") +
                  message;

    return message;
}

template <> struct RecTypeItem<AvailableStatusEnumType>
            AvailableStatusType::m_items[] =
{
    REC_ITEM(asAvailable, "Available", ""),
    REC_ITEM(asNotYetAvailable, "NotYetAvailable", ""),
    REC_ITEM(asPendingDelete, "PendingDelete", ""),
    REC_ITEM(asFileNotFound, "FileNotFound", ""),
    REC_ITEM(asZeroByte, "ZeroByte", ""),
    REC_ITEM(asDeleted, "Deleted", "")
};
template <> int AvailableStatusType::m_itemCount =
            NELEMS(AvailableStatusType::m_items);

template <> AvailableStatusEnumType AvailableStatusType::m_defaultValue =
            asAvailable;
template <> const char * AvailableStatusType::m_defaultString = "Unknown";
template <> QHash<QString, struct RecTypeItem<AvailableStatusEnumType> *> *
            AvailableStatusType::m_stringHash = NULL;
template <> QHash<AvailableStatusEnumType,
                  struct RecTypeItem<AvailableStatusEnumType> *> *
            AvailableStatusType::m_hash = NULL;

/* vim: set expandtab tabstop=4 shiftwidth=4: */

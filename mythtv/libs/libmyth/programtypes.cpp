// -*- Mode: c++ -*-

#include <QDateTime>
#include <QMutex>

#include "programtypes.h"
#include "mythactions.h"
#include "mythdate.h"

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
    REC_ITEM(MARK_ALL, QT_TRANSLATE_NOOP("RecType", "ALL"), ""),
    REC_ITEM(MARK_UNKNOWN, QT_TRANSLATE_NOOP("RecType", "Unknown"), ""),
    REC_ITEM(MARK_UNSET, QT_TRANSLATE_NOOP("RecType", "UNSET"), ""),
    REC_ITEM(MARK_TMP_CUT_END, QT_TRANSLATE_NOOP("RecType", "TMP_CUT_END"), ""),
    REC_ITEM(MARK_TMP_CUT_START,
             QT_TRANSLATE_NOOP("RecType", "TMP_CUT_START"), ""),
    REC_ITEM(MARK_UPDATED_CUT, QT_TRANSLATE_NOOP("RecType", "UPDATED_CUT"), ""),
    REC_ITEM(MARK_PLACEHOLDER, QT_TRANSLATE_NOOP("RecType", "PLACEHOLDER"), ""),
    REC_ITEM(MARK_CUT_END, QT_TRANSLATE_NOOP("RecType", "CUT_END"), ""),
    REC_ITEM(MARK_CUT_START, QT_TRANSLATE_NOOP("RecType", "CUT_START"), ""),
    REC_ITEM(MARK_BOOKMARK, QT_TRANSLATE_NOOP("RecType", "BOOKMARK"), ""),
    REC_ITEM(MARK_BLANK_FRAME, QT_TRANSLATE_NOOP("RecType", "BLANK_FRAME"), ""),
    REC_ITEM(MARK_COMM_START, QT_TRANSLATE_NOOP("RecType", "COMM_START"), ""),
    REC_ITEM(MARK_COMM_END, QT_TRANSLATE_NOOP("RecType", "COMM_END"), ""),
    REC_ITEM(MARK_GOP_START, QT_TRANSLATE_NOOP("RecType", "GOP_START"), ""),
    REC_ITEM(MARK_KEYFRAME, QT_TRANSLATE_NOOP("RecType", "KEYFRAME"), ""),
    REC_ITEM(MARK_SCENE_CHANGE,
             QT_TRANSLATE_NOOP("RecType", "SCENE_CHANGE"), ""),
    REC_ITEM(MARK_GOP_BYFRAME, QT_TRANSLATE_NOOP("RecType", "GOP_BYFRAME"), ""),
    REC_ITEM(MARK_ASPECT_1_1, QT_TRANSLATE_NOOP("RecType", "ASPECT 1:1"), ""),
    REC_ITEM(MARK_ASPECT_4_3, QT_TRANSLATE_NOOP("RecType", "ASPECT 4:3"), ""),
    REC_ITEM(MARK_ASPECT_16_9, QT_TRANSLATE_NOOP("RecType", "ASPECT 16:9"), ""),
    REC_ITEM(MARK_ASPECT_2_21_1,
             QT_TRANSLATE_NOOP("RecType", "ASPECT 2.21:1"), ""),
    REC_ITEM(MARK_ASPECT_CUSTOM,
             QT_TRANSLATE_NOOP("RecType", "ASPECT CUSTOM"), ""),
    REC_ITEM(MARK_VIDEO_WIDTH, QT_TRANSLATE_NOOP("RecType", "VIDEO_WIDTH"), ""),
    REC_ITEM(MARK_VIDEO_HEIGHT,
             QT_TRANSLATE_NOOP("RecType", "VIDEO_HEIGHT"), ""),
    REC_ITEM(MARK_VIDEO_RATE, QT_TRANSLATE_NOOP("RecType", "VIDEO_RATE"), ""),
    REC_ITEM(MARK_DURATION_MS, QT_TRANSLATE_NOOP("RecType", "DURATION MS"), ""),
    REC_ITEM(MARK_TOTAL_FRAMES,
             QT_TRANSLATE_NOOP("RecType", "TOTAL_FRAMES"), "")
};
template <> int MarkType::m_itemCount = NELEMS(MarkType::m_items);

template <> MarkEnumType MarkType::m_defaultValue = MARK_UNKNOWN;
template <> const char * MarkType::m_defaultString =
            QT_TRANSLATE_NOOP("RecType", "Unknown");
template <> QHash<QString, struct RecTypeItem<MarkEnumType> *> *
            MarkType::m_stringHash = NULL;
template <> QHash<MarkEnumType, struct RecTypeItem<MarkEnumType> *> *
            MarkType::m_hash = NULL;

#define REC_STATUS_PRIV_ITEM(t,letter,ui,descr) \
    { (t), letter, ui, descr }

template <> struct RecTypeItem<RecStatusEnumType> RecStatusBaseType::m_items[] =
{
    REC_ITEM(rsMissedFuture,
             QT_TRANSLATE_NOOP("RecType", "Missed Future"), ""),
    REC_ITEM(rsTuning,
             QT_TRANSLATE_NOOP("RecType", "Tuning"), ""),
    REC_ITEM(rsFailed,
             QT_TRANSLATE_NOOP("RecType", "Recorder Failed"), ""),
    REC_ITEM(rsTunerBusy,
             QT_TRANSLATE_NOOP("RecType", "Tuner Busy"), ""),
    REC_ITEM(rsLowDiskSpace,
             QT_TRANSLATE_NOOP("RecType", "Low Disk Space"), ""),
    REC_ITEM(rsCancelled,
             QT_TRANSLATE_NOOP("RecType", "Manual Cancel"), ""),
    REC_ITEM(rsMissed,
             QT_TRANSLATE_NOOP("RecType", "Missed"), ""),
    REC_ITEM(rsAborted,
             QT_TRANSLATE_NOOP("RecType", "Aborted"), ""),
    REC_ITEM(rsRecorded,
             QT_TRANSLATE_NOOP("RecType", "Recorded"), ""),
    REC_ITEM(rsRecording,
             QT_TRANSLATE_NOOP("RecType", "Recording"), ""),
    REC_ITEM(rsWillRecord,
             QT_TRANSLATE_NOOP("RecType", "Will Record"), ""),
    REC_ITEM(rsUnknown,
             QT_TRANSLATE_NOOP("RecType", "Unknown"), ""),
    REC_ITEM(rsDontRecord,
             QT_TRANSLATE_NOOP("RecType", "Don't Record"), ""),
    REC_ITEM(rsPreviousRecording,
             QT_TRANSLATE_NOOP("RecType", "Previously Recorded"), ""),
    REC_ITEM(rsCurrentRecording,
             QT_TRANSLATE_NOOP("RecType", "Currently Recorded"), ""),
    REC_ITEM(rsEarlierShowing,
             QT_TRANSLATE_NOOP("RecType", "Earlier Showing"), ""),
    REC_ITEM(rsTooManyRecordings,
             QT_TRANSLATE_NOOP("RecType", "Max Recordings"), ""),
    REC_ITEM(rsNotListed,
             QT_TRANSLATE_NOOP("RecType", "Not Listed"), ""),
    REC_ITEM(rsConflict,
             QT_TRANSLATE_NOOP("RecType", "Conflicting"), ""),
    REC_ITEM(rsLaterShowing,
             QT_TRANSLATE_NOOP("RecType", "Later Showing"), ""),
    REC_ITEM(rsRepeat,
             QT_TRANSLATE_NOOP("RecType", "Repeat"), ""),
    REC_ITEM(rsInactive,
             QT_TRANSLATE_NOOP("RecType", "Inactive"), ""),
    REC_ITEM(rsNeverRecord,
             QT_TRANSLATE_NOOP("RecType", "Never Record"), ""),
    REC_ITEM(rsOffLine,
             QT_TRANSLATE_NOOP("RecType", "Recorder Off-Line"), ""),
    REC_ITEM(rsOtherShowing,
             QT_TRANSLATE_NOOP("RecType", "Other Showing"), ""),
    REC_ITEM(rsOtherRecording,
             QT_TRANSLATE_NOOP("RecType", "Other Recording"), ""),
    REC_ITEM(rsOtherTuning,
             QT_TRANSLATE_NOOP("RecType", "Other Tuning"), "")
};
template <> int RecStatusBaseType::m_itemCount =
            NELEMS(RecStatusBaseType::m_items);

template <> RecStatusPrivItem RecStatusType::m_privItems[] = {
    REC_STATUS_PRIV_ITEM(rsMissedFuture,
                         QT_TRANSLATE_NOOP3("RecType", "M",
                                            "RecStatusChar rsMissedFuture"),
                         "disabled",
                         "This showing was not recorded because the "
                         "master backend was hung or not running."),
    REC_STATUS_PRIV_ITEM(rsTuning,
                         QT_TRANSLATE_NOOP3("RecType", "",
                                            "RecStatusChar rsTuning"),
                         "running",
                         "This showing is being tuned."),
    REC_STATUS_PRIV_ITEM(rsFailed,
                         QT_TRANSLATE_NOOP3("RecType", "f",
                                            "RecStatusChar rsFailed"),
                         "error",
                         "The recorder failed to record."),
    REC_STATUS_PRIV_ITEM(rsTunerBusy,
                         QT_TRANSLATE_NOOP3("RecType", "B",
                                            "RecStatusChar rsTunerBusy"),
                         "error",
                         "The tuner card was already being used."),
    REC_STATUS_PRIV_ITEM(rsLowDiskSpace,
                         QT_TRANSLATE_NOOP3("RecType", "K",
                                            "RecStatusChar rsLowDiskSpace"),
                         "disabled",
                         "There wasn't enough disk space available."),
    REC_STATUS_PRIV_ITEM(rsCancelled,
                         QT_TRANSLATE_NOOP3("RecType", "c",
                                            "RecStatusChar rsCancelled"),
                         "disabled",
                         "This showing was not recorded because it "
                         "was manually cancelled."),
    REC_STATUS_PRIV_ITEM(rsMissed,
                         QT_TRANSLATE_NOOP3("RecType", "M",
                                            "RecStatusChar rsMissed"),
                         "error",
                         "This showing was not recorded because the "
                         "master backend was hung or not running."),
    REC_STATUS_PRIV_ITEM(rsAborted,
                         QT_TRANSLATE_NOOP3("RecType", "A",
                                            "RecStatusChar rsAborted"),
                         "error",
                         "This showing was recorded but was aborted "
                         "before recording was completed."),
    REC_STATUS_PRIV_ITEM(rsRecorded,
                         QT_TRANSLATE_NOOP3("RecType", "R",
                                            "RecStatusChar rsRecorded"),
                         "normal",
                         "This showing was recorded."),
    REC_STATUS_PRIV_ITEM(rsRecording,
                         QT_TRANSLATE_NOOP3("RecType", "",
                                            "RecStatusChar rsRecording"),
                         "running",
                         "This showing is being recorded."),
    REC_STATUS_PRIV_ITEM(rsWillRecord,
                         QT_TRANSLATE_NOOP3("RecType", "",
                                            "RecStatusChar rsWillRecord"),
                         "normal",
                         "This showing will be recorded."),
    REC_STATUS_PRIV_ITEM(rsUnknown,
                         QT_TRANSLATE_NOOP3("RecType", "-",
                                            "RecStatusChar rsUnknown"),
                         "disabled",
                         "The status of this showing is unknown."),
    REC_STATUS_PRIV_ITEM(rsDontRecord,
                         QT_TRANSLATE_NOOP3("RecType", "X",
                                            "RecStatusChar rsDontRecord"),
                         "disabled",
                         "it was manually set to not record."),
    REC_STATUS_PRIV_ITEM(rsPreviousRecording,
                         QT_TRANSLATE_NOOP3("RecType", "P",
                                           "RecStatusChar rsPreviousRecording"),
                         "disabled",
                         "this episode was previously recorded "
                         "according to the duplicate policy chosen "
                         "for this title."),
    REC_STATUS_PRIV_ITEM(rsCurrentRecording,
                         QT_TRANSLATE_NOOP3("RecType", "R",
                                            "RecStatusChar rsCurrentRecording"),
                         "disabled",
                         "this episode was previously recorded and "
                         "is still available in the list of "
                         "recordings."),
    REC_STATUS_PRIV_ITEM(rsEarlierShowing,
                         QT_TRANSLATE_NOOP3("RecType", "E",
                                            "RecStatusChar rsEarlierShowing"),
                         "warning",
                         "this episode will be recorded at an "
                         "earlier time instead."),
    REC_STATUS_PRIV_ITEM(rsTooManyRecordings,
                         QT_TRANSLATE_NOOP3("RecType", "T",
                                           "RecStatusChar rsTooManyRecordings"),
                         "warning",
                         "too many recordings of this program have "
                         "already been recorded."),
    REC_STATUS_PRIV_ITEM(rsNotListed,
                         QT_TRANSLATE_NOOP3("RecType", "N",
                                            "RecStatusChar rsNotListed"),
                         "warning",
                         "this rule does not match any showings in "
                         "the current program listings."),
    REC_STATUS_PRIV_ITEM(rsConflict,
                         QT_TRANSLATE_NOOP3("RecType", "C",
                                            "RecStatusChar rsConflict"),
                         "error",
                         "another program with a higher priority "
                         "will be recorded."),
    REC_STATUS_PRIV_ITEM(rsLaterShowing,
                         QT_TRANSLATE_NOOP3("RecType", "L",
                                            "RecStatusChar rsLaterShowing"),
                         "warning",
                         "this episode will be recorded at a later time."),
    REC_STATUS_PRIV_ITEM(rsRepeat,
                         QT_TRANSLATE_NOOP3("RecType", "r",
                                            "RecStatusChar rsRepeat"),
                         "disabled",
                         "this episode is a repeat."),
    REC_STATUS_PRIV_ITEM(rsInactive,
                         QT_TRANSLATE_NOOP3("RecType", "x",
                                            "RecStatusChar rsInactive"),
                         "warning",
                         "this recording rule is inactive."),
    REC_STATUS_PRIV_ITEM(rsNeverRecord,
                         QT_TRANSLATE_NOOP3("RecType", "V",
                                            "RecStatusChar rsNeverRecord"),
                         "disabled",
                         "it was marked to never be recorded."),
    REC_STATUS_PRIV_ITEM(rsOffLine,
                         QT_TRANSLATE_NOOP3("RecType", "F",
                                            "RecStatusChar rsOffLine"),
                         "error",
                         "the backend recorder is off-line."),
    REC_STATUS_PRIV_ITEM(rsOtherShowing,
                         QT_TRANSLATE_NOOP3("RecType", "",
                                            "RecStatusChar rsOtherShowing"),
                         "normal",
                         "this episode will be recorded on a different "
                         "channel in this time slot."),
    REC_STATUS_PRIV_ITEM(rsOtherRecording,
                         QT_TRANSLATE_NOOP3("RecType", "",
                                            "RecStatusChar rsOtherRecording"),
                         "running",
                         "This showing is being recorded on a different "
                         "channel."),
    REC_STATUS_PRIV_ITEM(rsOtherTuning,
                         QT_TRANSLATE_NOOP3("RecType", "",
                                            "RecStatusChar rsOtherTuning"),
                         "running",
                         "This showing is bing tuned on a different channel.")
};
template <> int RecStatusType::m_privItemCount =
            NELEMS(RecStatusType::m_privItems);

template <> RecStatusEnumType RecStatusBaseType::m_defaultValue = rsUnknown;
template <> const char * RecStatusBaseType::m_defaultString =
            QT_TRANSLATE_NOOP("RecType", "Unknown");
template <> QHash<QString, struct RecTypeItem<RecStatusEnumType> *> *
            RecStatusBaseType::m_stringHash = NULL;
template <> QHash<RecStatusEnumType, struct RecTypeItem<RecStatusEnumType> *> *
            RecStatusBaseType::m_hash = NULL;
template <> bool RecStatusType::m_privInit = false;
template <> const char * RecStatusType::m_defaultChar = "-";
template <> const char * RecStatusType::m_defaultUI = "warning";
template <> const char * RecStatusType::m_defaultDescr =
            QT_TRANSLATE_NOOP("RecType",
                              "The status of this showing is unknown.");


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
    switch (m_value)
    {
        case rsRecording:
        case rsTuning:
        case rsWillRecord:
        case rsOtherShowing:
        case rsOtherRecording:
        case rsOtherTuning:
            return QString::number(id);
        default:
            break;
    }

    if (!m_hash)
        hashPrivInit();

    struct RecTypeItem<RecStatusEnumType> *item =
        m_hash->value(m_value, NULL);

    if (!item || !item->priv)
        return QString(m_defaultChar);

    RecStatusPrivItem *privItem = (RecStatusPrivItem *)item->priv;
    if (!privItem->charStr[0] || !privItem->charStr[0][0])
        return QString(m_defaultChar);

    return tr(privItem->charStr[0], privItem->charStr[1]);
}

/// \brief Converts "recstatus" into a short human readable description.
QString RecStatusType::toString(RecordingType rectype)
{
    if (m_value == rsUnknown && rectype == kNotRecording)
        return tr("Not Recording");

    if (!m_hash)
        hashPrivInit();

    struct RecTypeItem<RecStatusEnumType> *item =
        m_hash->value(m_value, NULL);

    if (!item)
        return tr(m_defaultString);

    return tr(item->rawString);
}

/// \brief Converts "recstatus" into a long human readable description.
QString RecStatusType::toDescription(RecordingType rectype,
                                     const QDateTime &recstartts)
{
    if (m_value == rsUnknown && rectype == kNotRecording)
        return tr("This showing is not scheduled to record");

    QString message;
    QDateTime now = MythDate::current();

    if (!m_hash)
        hashPrivInit();

    struct RecTypeItem<RecStatusEnumType> *item =
        m_hash->value(m_value, NULL);

    if (!item || !item->priv)
        return tr(m_defaultString);

    RecStatusPrivItem *privItem = (RecStatusPrivItem *)item->priv;
    message = tr(privItem->descrText);
    if (m_value <= rsUnknown)
        return message;

    if (recstartts > now)
        message = tr("This showing will not be recorded because ") +
                  message;
    else
        message = tr("This showing was not recorded because ") +
                  message;

    return message;
}

template <> struct RecTypeItem<AvailableStatusEnumType>
            AvailableStatusType::m_items[] =
{
    REC_ITEM(asAvailable,
             QT_TRANSLATE_NOOP("RecType", "Available"), ""),
    REC_ITEM(asNotYetAvailable,
             QT_TRANSLATE_NOOP("RecType", "NotYetAvailable"), ""),
    REC_ITEM(asPendingDelete,
             QT_TRANSLATE_NOOP("RecType", "PendingDelete"), ""),
    REC_ITEM(asFileNotFound,
             QT_TRANSLATE_NOOP("RecType", "FileNotFound"), ""),
    REC_ITEM(asZeroByte,
             QT_TRANSLATE_NOOP("RecType", "ZeroByte"), ""),
    REC_ITEM(asDeleted,
             QT_TRANSLATE_NOOP("RecType", "Deleted"), "")
};
template <> int AvailableStatusType::m_itemCount =
            NELEMS(AvailableStatusType::m_items);

template <> AvailableStatusEnumType AvailableStatusType::m_defaultValue =
            asAvailable;
template <> const char * AvailableStatusType::m_defaultString = 
            QT_TRANSLATE_NOOP("RecType", "Unknown");
template <> QHash<QString, struct RecTypeItem<AvailableStatusEnumType> *> *
            AvailableStatusType::m_stringHash = NULL;
template <> QHash<AvailableStatusEnumType,
                  struct RecTypeItem<AvailableStatusEnumType> *> *
            AvailableStatusType::m_hash = NULL;

/* vim: set expandtab tabstop=4 shiftwidth=4: */

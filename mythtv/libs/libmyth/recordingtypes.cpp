#include <QObject>
#include <QHash>

#include "recordingtypes.h"
#include "mythactions.h"

#define REC_TYPE_PRIV_ITEM(t,p,letter) \
    { (t), (p), letter }

template <> struct RecTypeItem<RecordingEnumType> RecordingBaseType::m_items[] =
{
    REC_ITEM(kNotRecording,
             QT_TRANSLATE_NOOP("RecType", "Not Recording"), "not"),
    REC_ITEM(kSingleRecord,
             QT_TRANSLATE_NOOP("RecType", "Single Record"), "single"),
    REC_ITEM(kTimeslotRecord,
             QT_TRANSLATE_NOOP("RecType", "Record Daily"), "daily"),
    REC_ITEM(kChannelRecord,
             QT_TRANSLATE_NOOP("RecType", "Channel Record"), "channel"),
    REC_ITEM(kAllRecord,
             QT_TRANSLATE_NOOP("RecType", "Record All"), "all"),
    REC_ITEM(kWeekslotRecord,
             QT_TRANSLATE_NOOP("RecType", "Record Weekly"), "weekly"),
    REC_ITEM(kFindOneRecord,
             QT_TRANSLATE_NOOP("RecType", "Find One"), "findone"),
    REC_ITEM(kOverrideRecord,
             QT_TRANSLATE_NOOP("RecType", "Override Recording"), "override"),
    REC_ITEM(kDontRecord,
             QT_TRANSLATE_NOOP("RecType", "Override Recording"), ""),
    REC_ITEM(kFindDailyRecord,
             QT_TRANSLATE_NOOP("RecType", "Find Daily"), "finddaily"),
    REC_ITEM(kFindWeeklyRecord,
             QT_TRANSLATE_NOOP("RecType", "Find Weekly"), "findweekly")
};
template <> int RecordingBaseType::m_itemCount =
            NELEMS(RecordingBaseType::m_items);

template <> RecordingPrivItem RecordingType::m_privItems[] = {
    REC_TYPE_PRIV_ITEM(kNotRecording, 0,
                       QT_TRANSLATE_NOOP3("RecType", " ",
                                          "RecTypeChar kNotRecording")),
    REC_TYPE_PRIV_ITEM(kSingleRecord, 3, 
                       QT_TRANSLATE_NOOP3("RecType", "S",
                                          "RecTypeChar kSingleRecord")),
    REC_TYPE_PRIV_ITEM(kTimeslotRecord, 7, 
                       QT_TRANSLATE_NOOP3("RecType", "T",
                                          "RecTypeChar kTimeslotRecord")),
    REC_TYPE_PRIV_ITEM(kChannelRecord, 9, 
                       QT_TRANSLATE_NOOP3("RecType", "C",
                                          "RecTypeChar kChannelRecord")),
    REC_TYPE_PRIV_ITEM(kAllRecord, 10, 
                       QT_TRANSLATE_NOOP3("RecType", "A",
                                          "RecTypeChar kAllRecord")),
    REC_TYPE_PRIV_ITEM(kWeekslotRecord, 5, 
                       QT_TRANSLATE_NOOP3("RecType", "W",
                                          "RecTypeChar kWeekslotRecord")),
    REC_TYPE_PRIV_ITEM(kFindOneRecord, 4, 
                       QT_TRANSLATE_NOOP3("RecType", "F",
                                          "RecTypeChar kFindOneRecord")),
    REC_TYPE_PRIV_ITEM(kOverrideRecord, 2, 
                       QT_TRANSLATE_NOOP3("RecType", "O",
                                          "RecTypeChar kOverrideRecord")),
    REC_TYPE_PRIV_ITEM(kDontRecord, 1, 
                       QT_TRANSLATE_NOOP3("RecType", "O",
                                          "RecTypeChar kOverrideRecord")),
    REC_TYPE_PRIV_ITEM(kFindDailyRecord, 8, 
                       QT_TRANSLATE_NOOP3("RecType", "d",
                                          "RecTypeChar kFindDailyRecord")),
    REC_TYPE_PRIV_ITEM(kFindWeeklyRecord, 6, 
                       QT_TRANSLATE_NOOP3("RecType", "w",
                                          "RecTypeChar kFindWeeklyRecord")),
};
template <> int RecordingType::m_privItemCount =
            NELEMS(RecordingType::m_privItems);
template <> RecordingEnumType RecordingBaseType::m_defaultValue = kNotRecording;
template <> const char * RecordingBaseType::m_defaultString = 
            QT_TRANSLATE_NOOP("RecType", "Not Recording");
template <> QHash<QString, struct RecTypeItem<RecordingEnumType> *> *
            RecordingBaseType::m_stringHash = NULL;
template <> QHash<RecordingEnumType, struct RecTypeItem<RecordingEnumType> *> *
            RecordingBaseType::m_hash = NULL;
template <> bool RecordingType::m_privInit = false;
template <> const char * RecordingType::m_defaultChar = " ";
template <> int RecordingType::m_defaultPriority = 11;



template <> struct RecTypeItem<RecordingDupInEnumType>
            RecordingDupInType::m_items[] = {
    REC_ITEM(kDupsInRecorded, 
             QT_TRANSLATE_NOOP("RecType", "Current Recordings"), "current"),
    REC_ITEM(kDupsInOldRecorded,
             QT_TRANSLATE_NOOP("RecType", "Previous Recordings"), "previous"),
    REC_ITEM(kDupsInAll,
             QT_TRANSLATE_NOOP("RecType", "All Recordings"), "all"),
    REC_ITEM(kDupsNewEpi,
             QT_TRANSLATE_NOOP("RecType", "New Episodes Only"), "new")
};
template <> int RecordingDupInType::m_itemCount =
            NELEMS(RecordingDupInType::m_items);
template <> RecordingDupInEnumType RecordingDupInType::m_defaultValue =
            kDupsInAll;
template <> const char * RecordingDupInType::m_defaultString = 
            QT_TRANSLATE_NOOP("RecType", "Unknown");
template <> QHash<QString, struct RecTypeItem<RecordingDupInEnumType> *> *
            RecordingDupInType::m_stringHash = NULL;
template <> QHash<RecordingDupInEnumType,
                  struct RecTypeItem<RecordingDupInEnumType> *> *
            RecordingDupInType::m_hash = NULL;


template <> struct RecTypeItem<RecordingDupMethodEnumType>
            RecordingDupMethodType::m_items[] = {
    REC_ITEM(kDupCheckNone, QT_TRANSLATE_NOOP("RecType", "None"), ""),
    REC_ITEM(kDupCheckSub, QT_TRANSLATE_NOOP("RecType", "Subtitle"), ""),
    REC_ITEM(kDupCheckDesc, QT_TRANSLATE_NOOP("RecType", "Description"), ""),
    REC_ITEM(kDupCheckSubDesc,
             QT_TRANSLATE_NOOP("RecType", "Subtitle and Description"),
             "subtitleanddescription"),
    REC_ITEM(kDupCheckSubThenDesc,
             QT_TRANSLATE_NOOP("RecType", "Subtitle then Description"),
             "subtitlethendescription")
};
template <> int RecordingDupMethodType::m_itemCount =
            NELEMS(RecordingDupMethodType::m_items);
template <> RecordingDupMethodEnumType RecordingDupMethodType::m_defaultValue =
            kDupCheckSubDesc;
template <> const char * RecordingDupMethodType::m_defaultString =
            QT_TRANSLATE_NOOP("RecType", "Unknown");
template <> QHash<QString, struct RecTypeItem<RecordingDupMethodEnumType> *> *
            RecordingDupMethodType::m_stringHash = NULL;
template <> QHash<RecordingDupMethodEnumType,
                  struct RecTypeItem<RecordingDupMethodEnumType> *> *
            RecordingDupMethodType::m_hash = NULL;


template <> struct RecTypeItem<RecSearchEnumType> RecSearchType::m_items[] = {
    REC_ITEM(kNoSearch, QT_TRANSLATE_NOOP("RecType", "None"), ""),
    REC_ITEM(kPowerSearch, QT_TRANSLATE_NOOP("RecType", "Power Search"),
             "power"),
    REC_ITEM(kTitleSearch, QT_TRANSLATE_NOOP("RecType", "Title Search"),
             "title"),
    REC_ITEM(kKeywordSearch,
             QT_TRANSLATE_NOOP("RecType", "Keyword Search"), "keyword"),
    REC_ITEM(kPeopleSearch,
             QT_TRANSLATE_NOOP("RecType", "People Search"), "people"),
    REC_ITEM(kManualSearch,
             QT_TRANSLATE_NOOP("RecType", "Manual Search"), "manual")
};
template <> int RecSearchType::m_itemCount = NELEMS(RecSearchType::m_items);
template <> RecSearchEnumType RecSearchType::m_defaultValue = kNoSearch;
template <> const char * RecSearchType::m_defaultString = 
            QT_TRANSLATE_NOOP("RecType", "Unknown");
template <> QHash<QString, struct RecTypeItem<RecSearchEnumType> *> *
            RecSearchType::m_stringHash = NULL;
template <> QHash<RecSearchEnumType, struct RecTypeItem<RecSearchEnumType> *> *
            RecSearchType::m_hash = NULL;

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

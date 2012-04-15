#include <QObject>
#include <QHash>

#include "recordingtypes.h"
#include "mythactions.h"

#define REC_ITEM(t,string,listing) \
    { (t), string, \
      QStringList() << QString(string).toLower() << listing, NULL }

#define REC_TYPE_PRIV_ITEM(t,p,letter) \
    { (t), (p), letter, "RecTypeChar ##t" }

template <> struct RecTypeItem<RecordingEnumType> RecordingBaseType::m_items[] =
{
    REC_ITEM(kNotRecording, "Not Recording", "not"),
    REC_ITEM(kSingleRecord, "Single Record", "single"),
    REC_ITEM(kTimeslotRecord, "Record Daily", "daily"),
    REC_ITEM(kChannelRecord, "Channel Record", "channel"),
    REC_ITEM(kAllRecord, "Record All", "all"),
    REC_ITEM(kWeekslotRecord, "Record Weekly", "weekly"),
    REC_ITEM(kFindOneRecord, "Find One", "findone"),
    REC_ITEM(kOverrideRecord, "Override Recording", "override"),
    REC_ITEM(kDontRecord, "Override Recording", ""),
    REC_ITEM(kFindDailyRecord, "Find Daily", "finddaily"),
    REC_ITEM(kFindWeeklyRecord, "Find Weekly", "findweekly")
};
template <> int RecordingBaseType::m_itemCount =
            NELEMS(RecordingBaseType::m_items);

template <> RecordingPrivItem RecordingType::m_privItems[] = {
    REC_TYPE_PRIV_ITEM(kNotRecording, 0, " "),
    REC_TYPE_PRIV_ITEM(kSingleRecord, 3, "S"),
    REC_TYPE_PRIV_ITEM(kTimeslotRecord, 7, "T"),
    REC_TYPE_PRIV_ITEM(kChannelRecord, 9, "C"),
    REC_TYPE_PRIV_ITEM(kAllRecord, 10, "A"),
    REC_TYPE_PRIV_ITEM(kWeekslotRecord, 5, "W"),
    REC_TYPE_PRIV_ITEM(kFindOneRecord, 4, "F"),
    REC_TYPE_PRIV_ITEM(kOverrideRecord, 2, "O"),
    REC_TYPE_PRIV_ITEM(kDontRecord, 1, "O"),
    REC_TYPE_PRIV_ITEM(kFindDailyRecord, 8, "d"),
    REC_TYPE_PRIV_ITEM(kFindWeeklyRecord, 6, "w")
};
template <> int RecordingType::m_privItemCount =
            NELEMS(RecordingType::m_privItems);
template <> RecordingEnumType RecordingBaseType::m_defaultValue = kNotRecording;
template <> const char * RecordingBaseType::m_defaultString = "Not Recording";
template <> QHash<QString, struct RecTypeItem<RecordingEnumType> *> *
            RecordingBaseType::m_stringHash = NULL;
template <> QHash<RecordingEnumType, struct RecTypeItem<RecordingEnumType> *> *
            RecordingBaseType::m_hash = NULL;
template <> bool RecordingType::m_privInit = false;
template <> const char * RecordingType::m_defaultChar = " ";
template <> int RecordingType::m_defaultPriority = 11;



template <> struct RecTypeItem<RecordingDupInEnumType>
            RecordingDupInType::m_items[] = {
    REC_ITEM(kDupsInRecorded, "Current Recordings", "current"),
    REC_ITEM(kDupsInOldRecorded, "Previous Recordings", "previous"),
    REC_ITEM(kDupsInAll, "All Recordings", "all"),
    REC_ITEM(kDupsNewEpi, "New Episodes Only", "new")
};
template <> int RecordingDupInType::m_itemCount =
            NELEMS(RecordingDupInType::m_items);
template <> RecordingDupInEnumType RecordingDupInType::m_defaultValue =
            kDupsInAll;
template <> const char * RecordingDupInType::m_defaultString = "Unknown";
template <> QHash<QString, struct RecTypeItem<RecordingDupInEnumType> *> *
            RecordingDupInType::m_stringHash = NULL;
template <> QHash<RecordingDupInEnumType,
                  struct RecTypeItem<RecordingDupInEnumType> *> *
            RecordingDupInType::m_hash = NULL;


template <> struct RecTypeItem<RecordingDupMethodEnumType>
            RecordingDupMethodType::m_items[] = {
    REC_ITEM(kDupCheckNone, "None", ""),
    REC_ITEM(kDupCheckSub, "Subtitle", ""),
    REC_ITEM(kDupCheckDesc, "Description", ""),
    REC_ITEM(kDupCheckSubDesc, "Subtitle and Description",
             "subtitleanddescription"),
    REC_ITEM(kDupCheckSubThenDesc, "Subtitle then Description",
             "subtitlethendescription")
};
template <> int RecordingDupMethodType::m_itemCount =
            NELEMS(RecordingDupMethodType::m_items);
template <> RecordingDupMethodEnumType RecordingDupMethodType::m_defaultValue =
            kDupCheckSubDesc;
template <> const char * RecordingDupMethodType::m_defaultString = "Unknown";
template <> QHash<QString, struct RecTypeItem<RecordingDupMethodEnumType> *> *
            RecordingDupMethodType::m_stringHash = NULL;
template <> QHash<RecordingDupMethodEnumType,
                  struct RecTypeItem<RecordingDupMethodEnumType> *> *
            RecordingDupMethodType::m_hash = NULL;


template <> struct RecTypeItem<RecSearchEnumType> RecSearchType::m_items[] = {
    REC_ITEM(kNoSearch, "None", ""),
    REC_ITEM(kPowerSearch, "Power Search", "power"),
    REC_ITEM(kTitleSearch, "Title Search", "title"),
    REC_ITEM(kKeywordSearch, "Keyword Search", "keyword"),
    REC_ITEM(kPeopleSearch, "People Search", "people"),
    REC_ITEM(kManualSearch, "Manual Search", "manual")
};
template <> int RecSearchType::m_itemCount = NELEMS(RecSearchType::m_items);
template <> RecSearchEnumType RecSearchType::m_defaultValue = kNoSearch;
template <> const char * RecSearchType::m_defaultString = "Unknown";
template <> QHash<QString, struct RecTypeItem<RecSearchEnumType> *> *
            RecSearchType::m_stringHash = NULL;
template <> QHash<RecSearchEnumType, struct RecTypeItem<RecSearchEnumType> *> *
            RecSearchType::m_hash = NULL;

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

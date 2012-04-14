#include <QObject>
#include <QHash>

#include "recordingtypes.h"
#include "mythactions.h"

typedef struct {
    RecordingType   type;
    int             priority;
    QString         transString;
    QString         rawString;
    QChar           shortString;
    QStringList     stringList;
} RecordingTypeItem;

#define RECORDING_TYPE_ITEM(t,p,string,letter,listing) \
    { (t), (p), QObject::tr(string), QString(string), \
      QChar(QString(QObject::tr(letter, "RecTypeChar ##t"))[0]), \
      QStringList() << QString(string).toLower() << listing }

static RecordingTypeItem recordingTypeItems[] = {
    RECORDING_TYPE_ITEM(kNotRecording, 0, "Not Recording", " ", "not"),
    RECORDING_TYPE_ITEM(kSingleRecord, 3, "Single Record", "S", "single"),
    RECORDING_TYPE_ITEM(kTimeslotRecord, 7, "Record Daily", "T", "daily"),
    RECORDING_TYPE_ITEM(kChannelRecord, 9, "Channel Record", "C", "channel"),
    RECORDING_TYPE_ITEM(kAllRecord, 10, "Record All", "A", "all"),
    RECORDING_TYPE_ITEM(kWeekslotRecord, 5, "Record Weekly", "W", "weekly"),
    RECORDING_TYPE_ITEM(kFindOneRecord, 4, "Find One", "F", "findone"),
    RECORDING_TYPE_ITEM(kOverrideRecord, 2, "Override Recording", "O",
                        "override"),
    RECORDING_TYPE_ITEM(kDontRecord, 1, "Override Recording", "O", ""),
    RECORDING_TYPE_ITEM(kFindDailyRecord, 8, "Find Daily", "d", "finddaily"),
    RECORDING_TYPE_ITEM(kFindWeeklyRecord, 6, "Find Weekly", "w", "findweekly")
};
static int recordingTypeItemCount = NELEMS(recordingTypeItems);

typedef QHash<QString, RecordingTypeItem *> RecordingTypeStringHash;
typedef QHash<RecordingType, RecordingTypeItem *> RecordingTypeHash;

static RecordingTypeStringHash *recTypeStringHash = NULL;
static RecordingTypeHash       *recTypeHash = NULL;


static void RecTypeHashInit(void)
{
    if (recTypeHash)
        delete recTypeHash;

    if (recTypeStringHash)
        delete recTypeStringHash;

    recTypeHash = new RecordingTypeHash;
    recTypeStringHash = new RecordingTypeStringHash;

    for (int i = 0; i < recordingTypeItemCount; i++)
    {
        RecordingTypeItem *item = &recordingTypeItems[i];
        for (QStringList::iterator it = item->stringList.begin();
             it != item->stringList.end(); ++it)
        {
            QString string = *it;
            if (!recTypeStringHash->contains(string) && !string.isEmpty())
                recTypeStringHash->insert(string, item);
        }

        recTypeHash->insert(item->type, item);
    }
}

/// Converts a RecordingType to a simple integer so it's specificity can
/// be compared to another.  Lower number means more specific.
int RecTypePriority(RecordingType rectype)
{
    if (!recTypeHash)
        RecTypeHashInit();

    RecordingTypeItem *item = recTypeHash->value(rectype, NULL);
    if (!item)
        return 11;

    return item->priority;
}

/// \brief Converts "rectype" into a human readable description.
QString toString(RecordingType rectype)
{
    if (!recTypeHash)
        RecTypeHashInit();

    RecordingTypeItem *item = recTypeHash->value(rectype, NULL);
    if (!item)
        return QObject::tr("Not Recording");

    return item->transString;
}

/// \brief Converts "rectype" into an untranslated description.
QString toRawString(RecordingType rectype)
{
    if (!recTypeHash)
        RecTypeHashInit();

    RecordingTypeItem *item = recTypeHash->value(rectype, NULL);
    if (!item)
        return QString("Not Recording");

    return item->rawString;
}

RecordingType recTypeFromString(const QString &type)
{
    if (!recTypeStringHash)
        RecTypeHashInit();

    RecordingTypeItem *item = recTypeStringHash->value(type.toLower(), NULL);
    if (!item)
        return kDontRecord;

    return item->type;
}

/// \brief Converts "rectype" into a human readable character.
QChar toQChar(RecordingType rectype)
{
    if (!recTypeHash)
        RecTypeHashInit();

    RecordingTypeItem *item = recTypeHash->value(rectype, NULL);
    if (!item)
        return QChar(' ');

    return item->shortString;
}


typedef struct {
    RecordingDupInType  type;
    QString             rawString;
    QStringList         stringList;
} RecordingDupInTypeItem;

#define RECORDING_DUP_IN_ITEM(t,string,listing) \
    { (t), QString(string), QStringList() << QString(string).toLower() << \
      listing }

static RecordingDupInTypeItem recordingDupInTypeItems[] = {
    RECORDING_DUP_IN_ITEM(kDupsInRecorded, "Current Recordings", "current"),
    RECORDING_DUP_IN_ITEM(kDupsInOldRecorded, "Previous Recordings",
        "previous"),
    RECORDING_DUP_IN_ITEM(kDupsInAll, "All Recordings", "all"),
    RECORDING_DUP_IN_ITEM(kDupsNewEpi, "New Episodes Only", "new")
};
static int recordingDupInTypeItemCount = NELEMS(recordingDupInTypeItems);

typedef QHash<QString, RecordingDupInTypeItem *> RecordingDupInTypeStringHash;
typedef QHash<RecordingDupInType, RecordingDupInTypeItem *>
            RecordingDupInTypeHash;

static RecordingDupInTypeStringHash *recDupInTypeStringHash = NULL;
static RecordingDupInTypeHash       *recDupInTypeHash = NULL;


static void RecDupInTypeHashInit(void)
{
    if (recDupInTypeHash)
        delete recDupInTypeHash;

    if (recDupInTypeStringHash)
        delete recDupInTypeStringHash;

    recDupInTypeHash = new RecordingDupInTypeHash;
    recDupInTypeStringHash = new RecordingDupInTypeStringHash;

    for (int i = 0; i < recordingDupInTypeItemCount; i++)
    {
        RecordingDupInTypeItem *item = &recordingDupInTypeItems[i];
        for (QStringList::iterator it = item->stringList.begin();
             it != item->stringList.end(); ++it)
        {
            QString string = *it;
            if (!recDupInTypeStringHash->contains(string) && !string.isEmpty())
                recDupInTypeStringHash->insert(string, item);
        }

        recDupInTypeHash->insert(item->type, item);
    }
}


QString toRawString(RecordingDupInType recdupin)
{
    if (!recDupInTypeHash)
        RecDupInTypeHashInit();

    RecordingDupInTypeItem *item = recDupInTypeHash->value(recdupin, NULL);
    if (!item)
        return QString("Unknown");

    return item->rawString;
}

RecordingDupInType dupInFromString(const QString &type)
{
    if (!recDupInTypeStringHash)
        RecDupInTypeHashInit();

    RecordingDupInTypeItem *item = recDupInTypeStringHash->value(type.toLower(),
                                                                 NULL);
    if (!item)
        return kDupsInAll;

    return item->type;
}


typedef struct {
    RecordingDupMethodType  type;
    QString                 rawString;
    QStringList             stringList;
} RecordingDupMethodTypeItem;

#define RECORDING_DUP_METHOD_ITEM(t,string,listing) \
    { (t), QString(string), QStringList() << QString(string).toLower() << \
      listing }

static RecordingDupMethodTypeItem recordingDupMethodTypeItems[] = {
    RECORDING_DUP_METHOD_ITEM(kDupCheckNone, "None", ""),
    RECORDING_DUP_METHOD_ITEM(kDupCheckSub, "Subtitle", ""),
    RECORDING_DUP_METHOD_ITEM(kDupCheckDesc, "Description", ""),
    RECORDING_DUP_METHOD_ITEM(kDupCheckSubDesc, "Subtitle and Description",
        "subtitleanddescription"),
    RECORDING_DUP_METHOD_ITEM(kDupCheckSubThenDesc, "Subtitle then Description",
        "subtitlethendescription")
};
static int recordingDupMethodTypeItemCount =
            NELEMS(recordingDupMethodTypeItems);

typedef QHash<QString, RecordingDupMethodTypeItem *>
            RecordingDupMethodTypeStringHash;
typedef QHash<RecordingDupMethodType, RecordingDupMethodTypeItem *>
            RecordingDupMethodTypeHash;

static RecordingDupMethodTypeStringHash *recDupMethodTypeStringHash = NULL;
static RecordingDupMethodTypeHash       *recDupMethodTypeHash = NULL;


static void RecDupMethodTypeHashInit(void)
{
    if (recDupMethodTypeHash)
        delete recDupMethodTypeHash;

    if (recDupMethodTypeStringHash)
        delete recDupMethodTypeStringHash;

    recDupMethodTypeHash = new RecordingDupMethodTypeHash;
    recDupMethodTypeStringHash = new RecordingDupMethodTypeStringHash;

    for (int i = 0; i < recordingDupMethodTypeItemCount; i++)
    {
        RecordingDupMethodTypeItem *item = &recordingDupMethodTypeItems[i];
        for (QStringList::iterator it = item->stringList.begin();
             it != item->stringList.end(); ++it)
        {
            QString string = *it;
            if (!recDupMethodTypeStringHash->contains(string) &&
                !string.isEmpty())
                recDupMethodTypeStringHash->insert(string, item);
        }

        recDupMethodTypeHash->insert(item->type, item);
    }
}


QString toRawString(RecordingDupMethodType duptype)
{
    if (!recDupMethodTypeHash)
        RecDupMethodTypeHashInit();

    RecordingDupMethodTypeItem *item = recDupMethodTypeHash->value(duptype,
                                                                   NULL);
    if (!item)
        return QString("Unknown");

    return item->rawString;
}

RecordingDupMethodType dupMethodFromString(const QString &type)
{
    if (!recDupMethodTypeStringHash)
        RecDupMethodTypeHashInit();

    RecordingDupMethodTypeItem *item =
        recDupMethodTypeStringHash->value(type.toLower(), NULL);
    if (!item)
        return kDupCheckSubDesc;

    return item->type;
}



typedef struct {
    RecSearchType   type;
    QString         rawString;
    QStringList     stringList;
} RecSearchTypeItem;

#define REC_SEARCH_ITEM(t,string,listing) \
    { (t), QString(string), QStringList() << QString(string).toLower() << \
      listing }

static RecSearchTypeItem recSearchTypeItems[] = {
    REC_SEARCH_ITEM(kNoSearch, "None", ""),
    REC_SEARCH_ITEM(kPowerSearch, "Power Search", "power"),
    REC_SEARCH_ITEM(kTitleSearch, "Title Search", "title"),
    REC_SEARCH_ITEM(kKeywordSearch, "Keyword Search", "keyword"),
    REC_SEARCH_ITEM(kPeopleSearch, "People Search", "people"),
    REC_SEARCH_ITEM(kManualSearch, "Manual Search", "manual")
};
static int recSearchTypeItemCount = NELEMS(recSearchTypeItems);

typedef QHash<QString, RecSearchTypeItem *> RecSearchTypeStringHash;
typedef QHash<RecSearchType, RecSearchTypeItem *> RecSearchTypeHash;

static RecSearchTypeStringHash *recSearchTypeStringHash = NULL;
static RecSearchTypeHash       *recSearchTypeHash = NULL;


static void RecSearchTypeHashInit(void)
{
    if (recSearchTypeHash)
        delete recSearchTypeHash;

    if (recSearchTypeStringHash)
        delete recSearchTypeStringHash;

    recSearchTypeHash = new RecSearchTypeHash;
    recSearchTypeStringHash = new RecSearchTypeStringHash;

    for (int i = 0; i < recSearchTypeItemCount; i++)
    {
        RecSearchTypeItem *item = &recSearchTypeItems[i];
        for (QStringList::iterator it = item->stringList.begin();
             it != item->stringList.end(); ++it)
        {
            QString string = *it;
            if (!recSearchTypeStringHash->contains(string) && !string.isEmpty())
                recSearchTypeStringHash->insert(string, item);
        }

        recSearchTypeHash->insert(item->type, item);
    }
}

QString toRawString(RecSearchType searchtype)
{
    if (!recSearchTypeHash)
        RecSearchTypeHashInit();

    RecSearchTypeItem *item = recSearchTypeHash->value(searchtype, NULL);
    if (!item)
        return QString("Unknown");

    return item->rawString;
}

RecSearchType searchTypeFromString(const QString &type)
{
    if (!recSearchTypeStringHash)
        RecSearchTypeHashInit();

    RecSearchTypeItem *item = recSearchTypeStringHash->value(type.toLower(),
                                                             NULL);
    if (!item)
        return kNoSearch;

    return item->type;
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

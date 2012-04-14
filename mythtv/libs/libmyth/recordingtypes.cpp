#include <QObject>
#include <QHash>

#include "recordingtypes.h"
#include "mythactions.h"

void RecTypeHashInit(void);

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

typedef QHash<QString, RecordingTypeItem *> RecordingTypeStringHash;
typedef QHash<RecordingType, RecordingTypeItem *> RecordingTypeHash;

RecordingTypeStringHash *recTypeStringHash = NULL;
RecordingTypeHash       *recTypeHash = NULL;

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

void RecTypeHashInit(void)
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

QString toRawString(RecordingDupInType recdupin)
{
    switch (recdupin)
    {
        case kDupsInRecorded:
            return QString("Current Recordings");
        case kDupsInOldRecorded:
            return QString("Previous Recordings");
        case kDupsInAll:
            return QString("All Recordings");
        case kDupsNewEpi:
            return QString("New Episodes Only");
        default:
            return QString("Unknown");
    }
}

RecordingDupInType dupInFromString(const QString &type)
{
    QString typeL(type.toLower());

    if (typeL == "current recordings" || typeL == "current")
        return kDupsInRecorded;
    if (typeL == "previous recordings" || typeL == "previous")
        return kDupsInOldRecorded;
    if (typeL == "all recordings" || typeL == "all")
        return kDupsInAll;
    if (typeL == "new episodes only" || typeL == "new")
        return kDupsNewEpi;

    return kDupsInAll;
}

QString toRawString(RecordingDupMethodType duptype)
{
    switch (duptype)
    {
        case kDupCheckNone:
            return QString("None");
        case kDupCheckSub:
            return QString("Subtitle");
        case kDupCheckDesc:
            return QString("Description");
        case kDupCheckSubDesc:
            return QString("Subtitle and Description");
        case kDupCheckSubThenDesc:
            return QString("Subtitle then Description");
        default:
            return QString("Unknown");
    }
}

RecordingDupMethodType dupMethodFromString(const QString &type)
{
    QString typeL(type.toLower());

    if (typeL == "none")
        return kDupCheckNone;
    if (typeL == "subtitle")
        return kDupCheckSub;
    if (typeL == "description")
        return kDupCheckDesc;
    if (typeL == "subtitle and description" ||
        typeL == "subtitleanddescription")
        return kDupCheckSubDesc;
    if (typeL == "subtitle then description" ||
        typeL == "subtitlethendescription")
        return kDupCheckSubThenDesc;

    return kDupCheckSubDesc;
}

QString toRawString(RecSearchType searchtype)
{
    switch (searchtype)
    {
        case kNoSearch:
            return QString("None");
        case kPowerSearch:
            return QString("Power Search");
        case kTitleSearch:
            return QString("Title Search");
        case kKeywordSearch:
            return QString("Keyword Search");
        case kPeopleSearch:
            return QString("People Search");
        case kManualSearch:
            return QString("Manual Search");
        default:
            return QString("Unknown");
    }
}

RecSearchType searchTypeFromString(const QString &type)
{
    QString typeL(type.toLower());

    if (typeL == "none")
        return kNoSearch;
    if (typeL == "power search" || typeL == "power")
        return kPowerSearch;
    if (typeL == "title search" || typeL == "title")
        return kTitleSearch;
    if (typeL == "keyword search" || typeL == "keyword")
        return kKeywordSearch;
    if (typeL == "people search" || typeL == "people")
        return kPeopleSearch;
    if (typeL == "manual search" || typeL == "manual")
        return kManualSearch;

    return kNoSearch;
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

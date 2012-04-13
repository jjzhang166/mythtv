#include <QObject>

#include "recordingtypes.h"

/// Converts a RecordingType to a simple integer so it's specificity can
/// be compared to another.  Lower number means more specific.
int RecTypePriority(RecordingType rectype)
{
    switch (rectype)
    {
        case kNotRecording:   return 0; break;
        case kDontRecord:     return 1; break;
        case kOverrideRecord: return 2; break;
        case kSingleRecord:   return 3; break;
        case kFindOneRecord:  return 4; break;
        case kWeekslotRecord: return 5; break;
        case kFindWeeklyRecord: return 6; break;
        case kTimeslotRecord: return 7; break;
        case kFindDailyRecord: return 8; break;
        case kChannelRecord:  return 9; break;
        case kAllRecord:      return 10; break;
        default: return 11;
     }
}

/// \brief Converts "rectype" into a human readable description.
QString toString(RecordingType rectype)
{
    switch (rectype)
    {
        case kSingleRecord:
            return QObject::tr("Single Record");
        case kTimeslotRecord:
            return QObject::tr("Record Daily");
        case kWeekslotRecord:
            return QObject::tr("Record Weekly");
        case kChannelRecord:
            return QObject::tr("Channel Record");
        case kAllRecord:
            return QObject::tr("Record All");
        case kFindOneRecord:
            return QObject::tr("Find One");
        case kFindDailyRecord:
            return QObject::tr("Find Daily");
        case kFindWeeklyRecord:
            return QObject::tr("Find Weekly");
        case kOverrideRecord:
        case kDontRecord:
            return QObject::tr("Override Recording");
        default:
            return QObject::tr("Not Recording");
    }
}

/// \brief Converts "rectype" into an untranslated description.
QString toRawString(RecordingType rectype)
{
    switch (rectype)
    {
        case kSingleRecord:
            return QString("Single Record");
        case kTimeslotRecord:
            return QString("Record Daily");
        case kWeekslotRecord:
            return QString("Record Weekly");
        case kChannelRecord:
            return QString("Channel Record");
        case kAllRecord:
            return QString("Record All");
        case kFindOneRecord:
            return QString("Find One");
        case kFindDailyRecord:
            return QString("Find Daily");
        case kFindWeeklyRecord:
            return QString("Find Weekly");
        case kOverrideRecord:
        case kDontRecord:
            return QString("Override Recording");
        default:
            return QString("Not Recording");
    }
}

RecordingType recTypeFromString(const QString &type)
{
    QString typeL(type.toLower());

    if (typeL == "not recording" || typeL == "not")
        return kNotRecording;
    if (typeL == "single record" || typeL == "single")
        return kSingleRecord;
    if (typeL == "record daily" || typeL == "daily")
        return kTimeslotRecord;
    if (typeL == "record weekly" || typeL == "weekly")
        return kWeekslotRecord;
    if (typeL == "channel record" || typeL == "channel")
        return kChannelRecord;
    if (typeL == "record all" || typeL == "all")
        return kAllRecord;
    if (typeL == "find one" || typeL == "findone")
        return kFindOneRecord;
    if (typeL == "find daily" || typeL == "finddaily")
        return kFindDailyRecord;
    if (typeL == "find weekly" || typeL == "findweekly")
        return kFindWeeklyRecord;
    if (typeL == "override recording" || typeL == "override")
        return kOverrideRecord;

    return kDontRecord;
}

/// \brief Converts "rectype" into a human readable character.
QChar toQChar(RecordingType rectype)
{
    QString ret;
    switch (rectype)
    {
        case kSingleRecord:
            ret = QObject::tr("S", "RecTypeChar kSingleRecord");     break;
        case kTimeslotRecord:
            ret = QObject::tr("T", "RecTypeChar kTimeslotRecord");   break;
        case kWeekslotRecord:
            ret = QObject::tr("W", "RecTypeChar kWeekslotRecord");   break;
        case kChannelRecord:
            ret = QObject::tr("C", "RecTypeChar kChannelRecord");    break;
        case kAllRecord:
            ret = QObject::tr("A", "RecTypeChar kAllRecord");        break;
        case kFindOneRecord:
            ret = QObject::tr("F", "RecTypeChar kFindOneRecord");    break;
        case kFindDailyRecord:
            ret = QObject::tr("d", "RecTypeChar kFindDailyRecord");  break;
        case kFindWeeklyRecord:
            ret = QObject::tr("w", "RecTypeChar kFindWeeklyRecord"); break;
        case kOverrideRecord:
        case kDontRecord:
            ret = QObject::tr("O", "RecTypeChar kOverrideRecord/kDontRecord");
            break;
        case kNotRecording:
        default:
            ret = " ";
    }
    return (ret.isEmpty()) ? QChar(' ') : ret[0];
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

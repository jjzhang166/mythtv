#ifndef RECORDINGTYPES_H_
#define RECORDINGTYPES_H_

#include <QString>
#include <QStringList>
#include <QChar>

#include "mythexp.h"
#include <stdint.h>


template <typename T>
struct MPUBLIC RecTypeItem
{
    T           type;
    const char *rawString;
    QStringList stringList;
    void        *priv;
};

#define REC_ITEM(t,string,listing) \
    { (t), string, \
      QStringList() << QString(string).toLower() << listing, NULL }

template <typename T>
class MPUBLIC RecType
{
  public:
    RecType<T>() {};
    RecType<T>(int value) : m_value((T)value) {};
    RecType<T>(T value) : m_value(value) {};
    RecType<T>(const RecType<T> &old) : m_value(old.m_value) {};
    RecType<T>(const QString &string)
    {
        if (!m_stringHash)
            hashInit();

        struct RecTypeItem<T> *item =
            m_stringHash->value(string.toLower(), NULL);
        if (!item)
            m_value = m_defaultValue;
        else
            m_value = item->type;
    }

    bool operator==(const RecType<T> &other) const
        { return m_value == other.m_value; };

    bool operator!=(const RecType<T> &other) const
        { return m_value != other.m_value; };

    bool operator<(const RecType<T> &other) const
        { return (m_value < other.m_value); };

    bool operator<=(const RecType<T> &other) const
        { return (m_value <= other.m_value); };

    bool operator>(const RecType<T> &other) const
        { return (m_value > other.m_value); };


    bool operator==(const T &other) const
        { return m_value == other; };

    bool operator!=(const T &other) const
        { return m_value != other; };

    bool operator<(const T &other) const
        { return (m_value < other); };

    bool operator<=(const T &other) const
        { return (m_value <= other); };

    bool operator>(const T &other) const
        { return (m_value > other); };

    T operator&(const T &other) const
        { return (T)((uint64_t)m_value & (uint64_t)other); };

    int8_t  toInt8(void) const  { return (int8_t)m_value; };
    uint8_t toUint8(void) const { return (uint8_t)m_value; };

    T get(void) const { return m_value; };
    void set(T value) { m_value = value; };

    QString toRawString(void)
    {
        if (!m_hash)
            hashInit();

        struct RecTypeItem<T> *item = m_hash->value(m_value, NULL);
        if (!item)
            return m_defaultString;

        return item->rawString;
    }

  protected:
    T       m_value;

    static T m_defaultValue;
    static const char *m_defaultString;
    static struct RecTypeItem<T> m_items[];
    static int m_itemCount;
    static QHash<QString, RecTypeItem<T> *> *m_stringHash;
    static QHash<T, RecTypeItem<T> *> *m_hash;

    static void hashInit(void)
    {
        if (m_hash)
            delete m_hash;

        if (m_stringHash)
            delete m_stringHash;

        m_stringHash = new QHash<QString, struct RecTypeItem<T> *>;
        m_hash = new QHash<T, struct RecTypeItem<T> *>;

        for (int i = 0; i < m_itemCount; i++)
        {
            struct RecTypeItem<T> *item = &m_items[i];
            for (QStringList::iterator it = item->stringList.begin();
                 it != item->stringList.end(); ++it)
            {
                QString string = *it;
                if (!m_stringHash->contains(string) && !string.isEmpty())
                    m_stringHash->insert(string, item);
            }

            m_hash->insert(item->type, item);
        }
    }
};

typedef enum RecordingTypes
{
    kNotRecording = 0,
    kSingleRecord = 1,
    kTimeslotRecord,
    kChannelRecord,
    kAllRecord,
    kWeekslotRecord,
    kFindOneRecord,
    kOverrideRecord,
    kDontRecord,
    kFindDailyRecord,
    kFindWeeklyRecord
} RecordingEnumType; // note stored in uint8_t in ProgramInfo
typedef RecType<RecordingEnumType> RecordingBaseType;

typedef struct {
    RecordingEnumType   type;
    int                 priority;
    const char         *charVal;
    const char         *charType;
} RecordingPrivItem;

class MPUBLIC RecordingType : public RecType<RecordingEnumType>
{
  public:
    RecordingType() : RecType<RecordingEnumType>()
    {
        if (!m_privInit)
            hashPrivInit();
    }

    RecordingType(int value) : RecType<RecordingEnumType>(value)
    {
        if (!m_privInit)
            hashPrivInit();
    }

    RecordingType(RecordingEnumType value) : RecType<RecordingEnumType>(value)
    {
        if (!m_privInit)
            hashPrivInit();
    }

    RecordingType(const RecordingType &old) :
        RecType<RecordingEnumType>(
            static_cast<RecType<RecordingEnumType> >(old))
    {
        if (!m_privInit)
            hashPrivInit();
    }

    RecordingType(const QString &string) : RecType<RecordingEnumType>(string)
    {
        if (!m_privInit)
            hashPrivInit();
    }


    QString toString(void)
    {
        if (!m_hash)
            hashPrivInit();

        struct RecTypeItem<RecordingEnumType> *item =
            m_hash->value(m_value, NULL);

        if (!item || !item->priv)
            return QObject::tr(m_defaultString);

        return QObject::tr(item->rawString);
    }

    int toPriority(void)
    {
        if (!m_hash)
            hashPrivInit();

        struct RecTypeItem<RecordingEnumType> *item =
            m_hash->value(m_value, NULL);

        if (!item || !item->priv)
            return m_defaultPriority;

        RecordingPrivItem *privItem = (RecordingPrivItem *)item->priv;
        return privItem->priority;
    }

    QChar toQChar(void)
    {
        if (!m_hash)
            hashPrivInit();

        struct RecTypeItem<RecordingEnumType> *item =
            m_hash->value(m_value, NULL);

        if (!item || !item->priv)
            return QChar(m_defaultChar[0]);

        RecordingPrivItem *privItem = (RecordingPrivItem *)item->priv;
        QString translated = QObject::tr(privItem->charVal, privItem->charType);
        return QChar(translated[0]);
    }

  private:
    static bool  m_privInit;
    static const char *m_defaultChar;
    static int m_defaultPriority;
    static RecordingPrivItem m_privItems[];
    static int m_privItemCount;

    static void hashPrivInit(void)
    {
        if (m_privInit)
            return;

        if (!m_hash)
            hashInit();

        for (int i = 0; i < m_privItemCount; i++)
        {
            RecordingPrivItem *privItem = &m_privItems[i];
            struct RecTypeItem<RecordingEnumType> *item =
                m_hash->value(privItem->type);

            if (item)
                item->priv = privItem;
        }

        m_privInit = true;
    }
};


typedef enum RecordingDupInTypes
{
    kDupsInRecorded     = 0x01,
    kDupsInOldRecorded  = 0x02,
    kDupsInAll          = 0x0F,
    kDupsNewEpi         = 0x10
} RecordingDupInEnumType; // note stored in uint8_t in ProgramInfo
typedef RecType<RecordingDupInEnumType> RecordingDupInType;

typedef enum RecordingDupMethodTypes
{
    kDupCheckNone     = 0x01,
    kDupCheckSub      = 0x02,
    kDupCheckDesc     = 0x04,
    kDupCheckSubDesc  = 0x06,
    kDupCheckSubThenDesc = 0x08
} RecordingDupMethodEnumType; // note stored in uint8_t in ProgramInfo
typedef RecType<RecordingDupMethodEnumType> RecordingDupMethodType;

typedef enum RecSearchTypes
{
    kNoSearch = 0,
    kPowerSearch,
    kTitleSearch,
    kKeywordSearch,
    kPeopleSearch,
    kManualSearch
} RecSearchEnumType;
typedef RecType<RecSearchEnumType> RecSearchType;

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

#include "hardwareprofile.h"

// qt
#include <QScopedPointer>
#include <QStringList>
#include <QDir>
#include <QTextStream>

// libmythbase
#include "mythcorecontext.h"
#include "mythdirs.h"
#include "mythlogging.h"
#include "mythsystemlegacy.h"
#include "exitcodes.h"
#include "mythdate.h"

const QString SMOLT_SERVER_LOCATION =
                  QString("http://smolt.mythtv.org/");
const QString SMOLT_TOKEN =
                  QString("smolt_token-smolt.mythtv.org");

HardwareProfile::HardwareProfile() :
    m_uuid(QString()),               m_publicuuid(QString()),
    m_lastUpdate(QDateTime()),       m_hardwareProfile(QString())
{
    m_uuid = gCoreContext->GetSetting("HardwareProfileUUID");
    m_publicuuid = gCoreContext->GetSetting("HardwareProfilePublicUUID");
    QString lastupdate = gCoreContext->GetSetting("HardwareProfileLastUpdated");
    m_lastUpdate = MythDate::fromString(lastupdate);
}

HardwareProfile::~HardwareProfile()
{
}

void HardwareProfile::GenerateUUIDs(void)
{
    QString fileprefix = GetConfDir() + "/HardwareProfile";
    QDir dir(fileprefix);
    if (!dir.exists())
        dir.mkdir(fileprefix);

    // Generate the Private Hardware UUID (or recover them from the DB or file)

    QString fileUUID = GetPrivateUUIDFromFile();

    if (fileUUID.isEmpty() && m_uuid.isEmpty())
    {
        LOG(VB_GENERAL, LOG_INFO,
                 "No UUID in DB or File, generating new UUID...");

        QStringList args(GetShareDir() + "hardwareprofile/sendProfile.py");
        args << "-p";
        QScopedPointer<MythSystem> system(
            MythSystem::Create(args, kMSRunShell | kMSStdOut));
        system->Wait();
        m_hardwareProfile = system->GetStandardOutputStream()->readAll();
        m_uuid = GetPrivateUUIDFromFile();
    }
    else if (fileUUID.isEmpty())
    {
        LOG(VB_GENERAL, LOG_INFO,
                 QString("Writing Database UUID to local file: %1")
                         .arg(m_uuid));
        WritePrivateUUIDToFile(m_uuid);
    }
    else if (m_uuid.isEmpty())
    {
        LOG(VB_GENERAL, LOG_INFO,
                 QString("Profile UUID found in local file: %1")
                         .arg(fileUUID));
        m_uuid = fileUUID;
    }

    // Get the Public UUID from file

    m_publicuuid = GetPublicUUIDFromFile();
}

QString HardwareProfile::GetPrivateUUIDFromFile() const
{
    QString ret;

    QString hwuuid_file = GetConfDir() + "/HardwareProfile/hw-uuid";
    QFile file(hwuuid_file);
    if (file.open( QIODevice::ReadOnly ))
    {
        QTextStream stream(&file);
        ret = stream.readLine();
        file.close();
    }

    return ret;
}

QString HardwareProfile::GetPublicUUIDFromFile() const
{
    QString ret;

    QString pubuuid_file = GetConfDir() + "/HardwareProfile/uuiddb.cfg";
    QString pubuuid;
    QFile pubfile(pubuuid_file);
    if (pubfile.open( QIODevice::ReadOnly ))
    {
        QString s;
        QTextStream stream(&pubfile);
        while ( !stream.atEnd() )
        {
            s = stream.readLine();
            if (s.contains(m_uuid))
            {
                ret = s.section("=",1,1);
                ret = ret.trimmed();
            }
        }
        pubfile.close();
    }

    return ret;
}

QString HardwareProfile::GetAdminPasswordFromFile() const
{
    QString ret;

    if (gCoreContext->GetSetting("HardwareProfileUUID").isEmpty())
        return ret;

    QString token_file = GetConfDir() + "/HardwareProfile/" + SMOLT_TOKEN;
    QFile file(token_file);
    if (file.open( QIODevice::ReadOnly ))
    {
        QTextStream stream(&file);
        ret = stream.readLine();
        file.close();
    }

    return ret;
}

bool HardwareProfile::WritePrivateUUIDToFile(QString uuid)
{
    QString hwuuid_file = GetConfDir() + "/HardwareProfile/hw-uuid";
    QFile file(hwuuid_file);
    if (file.open(QIODevice::WriteOnly))
    {
        QTextStream stream(&file);
        stream << uuid;
        file.close();
        return true;
    }
    else
        return false;
}

bool HardwareProfile::NeedsUpdate(void) const
{
    if (!m_lastUpdate.isNull() &&
        (m_lastUpdate.addMonths(1) < MythDate::current()) &&
        !m_uuid.isEmpty())
    {
        LOG(VB_GENERAL, LOG_INFO,
                 "Last hardware profile update was > 30 days ago, update "
                 "required...");
        return true;
    }

    return false;
}

bool HardwareProfile::SubmitProfile(void)
{
    if (m_uuid.isEmpty())
        return false;

    if (!m_hardwareProfile.isEmpty())
        LOG(VB_GENERAL, LOG_INFO,
                 QString("Submitting the following hardware profile:  %1")
                         .arg(m_hardwareProfile));

    QStringList args(GetShareDir() + "hardwareprofile/sendProfile.py");
    args << "--submitOnly" << "-a";
    QScopedPointer<MythSystem> system(
        MythSystem::Create(args, kMSRunShell | kMSStdOut));
    system->Wait();
    if (GENERIC_EXIT_OK == system->GetExitCode())
    {
        GenerateUUIDs();
        gCoreContext->SaveSetting("HardwareProfileUUID", GetPrivateUUID());
        gCoreContext->SaveSetting("HardwareProfilePublicUUID", GetPublicUUID());
        gCoreContext->SaveSetting("HardwareProfileLastUpdated",
                                  MythDate::current_iso_string());
        return true;
    }

    return false;
}

bool HardwareProfile::DeleteProfile(void)
{
    if (m_uuid.isEmpty())
        return false;

    LOG(VB_GENERAL, LOG_INFO,
             QString("Deleting the following hardware profile: %1")
                     .arg(m_uuid));

    QScopedPointer<MythSystem> system(
        MythSystem::Create(GetShareDir() + "hardwareprofile/deleteProfile.py",
                           kMSRunShell | kMSStdOut));
    system->Wait();
    if (GENERIC_EXIT_OK == system->GetExitCode())
    {
        gCoreContext->SaveSetting("HardwareProfileUUID", "");
        gCoreContext->SaveSetting("HardwareProfilePublicUUID", "");
        gCoreContext->SaveSetting("HardwareProfileLastUpdated",
                                  MythDate::current_iso_string());
        return true;
    }

    return false;
}

QString HardwareProfile::GetProfileURL() const
{
    QString ret;

    if (!gCoreContext->GetSetting("HardwareProfileUUID").isEmpty())
    {
        ret = SMOLT_SERVER_LOCATION + "client/show/?uuid=" + m_publicuuid;
    }

    return ret;
}

QString HardwareProfile::GetHardwareProfile() const
{
    QStringList args(GetShareDir() + "hardwareprofile/sendProfile.py");
    args << "-p";
    QScopedPointer<MythSystem> system(
        MythSystem::Create(args, kMSRunShell | kMSStdOut));
    system->Wait();
    return QString(system->GetStandardOutputStream()->readAll());
}

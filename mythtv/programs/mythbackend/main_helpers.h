#ifndef _MAIN_HELPERS_H_
#define _MAIN_HELPERS_H_

// C++ headers
#include <iostream>
#include <fstream>
using namespace std;

// Qt headers
#include <QObject>

class MythBackendCommandLineParser;
class QString;
class QSize;

bool setupTVs(bool ismaster, bool &error);
bool setup_context(MythBackendCommandLineParser &cmdline);
void cleanup(void);
void upnp_rebuild(int);
void showUsage(const MythBackendCommandLineParser &cmdlineparser, const QString &version);
void setupLogfile(void);
bool openPidfile(ofstream &pidfs, const QString &pidfilename);
bool setUser(const QString &username);
int  handle_command(const MythBackendCommandLineParser &cmdline);
int  connect_to_master(void);
int  setup_basics(const MythBackendCommandLineParser &cmdline);
void print_warnings(const MythBackendCommandLineParser &cmdline);
int  run_backend(MythBackendCommandLineParser &cmdline);

namespace
{
    class CleanupGuard
    {
      public:
        typedef void (*CleanupFunc)();

      public:
        CleanupGuard(CleanupFunc cleanFunction) :
            m_cleanFunction(cleanFunction) {}

        ~CleanupGuard()
        {
            m_cleanFunction();
        }

      private:
        CleanupFunc m_cleanFunction;
    };
}

class QTimerEvent;
class MainServer;

class Stage2Init : public QObject
{
    Q_OBJECT
  public:
    Stage2Init(const MythBackendCommandLineParser &cmdline);
    ~Stage2Init();
  private:
    void timerEvent(QTimerEvent*);
    void Init(void);
  private:
    const MythBackendCommandLineParser &m_cmdline;
    MainServer *m_mainServer;
    int         m_timerId;
    int         m_waitTicks;
};


#endif // _MAIN_HELPERS_H_

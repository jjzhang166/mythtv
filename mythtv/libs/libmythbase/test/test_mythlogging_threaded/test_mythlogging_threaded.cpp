#include "test_mythlogging_threaded.h"

int TestMythLoggingThreaded::argc = 1;
char *TestMythLoggingThreaded::argv[3];
bool init_argv()
{
    TestMythLoggingThreaded::argv[0] = "dummy";
    TestMythLoggingThreaded::argv[1] = "dummy";
    TestMythLoggingThreaded::argv[2] = NULL;
    return true;
}
bool dummy = init_argv();


QTEST_APPLESS_MAIN(TestMythLoggingThreaded)


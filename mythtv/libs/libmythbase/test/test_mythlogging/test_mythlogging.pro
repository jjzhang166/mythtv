include ( ../../../../settings.pro )

contains(QT_VERSION, ^4\\.[0-9]\\..*) {
CONFIG += qtestlib
}
contains(QT_VERSION, ^5\\.[0-9]\\..*) {
QT += testlib
}

TEMPLATE = app
TARGET = test_mythlogging
DEPENDPATH += . ../.. ../../logging
INCLUDEPATH += . ../.. ../../logging

contains(QMAKE_CXX, "g++") {
  QMAKE_CXXFLAGS += -O0 -fprofile-arcs -ftest-coverage 
  QMAKE_LFLAGS += -fprofile-arcs 
}

# Input
HEADERS += test_mythlogging.h
SOURCES += test_mythlogging.cpp

HEADERS += ../../mythlogging.h ../../mythlogging_extra.h
HEADERS += ../../verbosedefs.h
SOURCES += ../../mythlogging.cpp

HEADERS += ../../logging/logdeque.h ../../logging/logentry.h
HEADERS += ../../logging/loglevelinfo.h ../../logging/verboseinfo.h
HEADERS += ../../logging/loghandler.h ../../logging/debugloghandler.h
SOURCES += ../../logging/logdeque.cpp ../../logging/logentry.cpp
SOURCES += ../../logging/loghandler.cpp ../../logging/debugloghandler.cpp

QMAKE_CLEAN += $(TARGET) $(TARGETA) $(TARGETD) $(TARGET0) $(TARGET1) $(TARGET2)
QMAKE_CLEAN += ; rm -f *.gcov *.gcda *.gcno

LIBS += $$EXTRA_LIBS $$LATE_LIBS

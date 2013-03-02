include ../config.mak

SUBDIRS = FFmpeg libmythbluray

ifdef CONFIG_HDHOMERUN
    SUBDIRS += libhdhomerun
endif

SUBDIRS_ALL = $(addsuffix -all, ${SUBDIRS})
SUBDIRS_INSTALL = $(addsuffix -install, ${SUBDIRS})
SUBDIRS_UNINSTALL = $(addsuffix -uninstall, ${SUBDIRS})
SUBDIRS_CLEAN = $(addsuffix -clean, ${SUBDIRS})
SUBDIRS_DISTCLEAN = $(addsuffix -distclean, ${SUBDIRS})

default:	all
all:		${SUBDIRS_ALL}
install:	${SUBDIRS_INSTALL}
uninstall:	${SUBDIRS_UNINSTALL}
clean:		${SUBDIRS_CLEAN}
distclean:	${SUBDIRS_DISTCLEAN}


FFmpeg-all FFmpeg-install FFmpeg-uninstall FFmpeg-clean FFmpeg-distclean:
	${MAKE} -C FFmpeg ${@:FFmpeg-%=%} 

libhdhomerun-all libhdhomerun-clean libhdhomerun-distclean libhdhomerun-install libhdhomerun-uninstall:
	${MAKE} -C libhdhomerun -f Makefile.lib ${@:libhdhomerun-%=%}
	${MAKE} -C libhdhomerun -f Makefile.app ${@:libhdhomerun-%=%}

libmythbluray-all libmythbluray-clean libmythbluray-distclean libmythbluray-install libmythbluray-uninstall:
	${MAKE} -C libmythbluray ${@:libmythbluray-%=%}


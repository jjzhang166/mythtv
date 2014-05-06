
TEMPLATE = subdirs

LIBS += -lmythlivemedia-$$LIBVERSION
using_mheg: LIBS += -lmythfreemheg-$$LIBVERSION

# Directories
SUBDIRS = mytharchive mytharchivehelper theme i18n


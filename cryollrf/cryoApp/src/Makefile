TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

USR_CFLAGS += -I$(LIBEVENT)/include

PROD_IOC_Linux = cryo
# cryo.dbd will be created and installed
DBD += cryo.dbd

# cryo.dbd will be made up from these files:
cryo_DBD += base.dbd
cryo_DBD += system.dbd
cryo_DBD += paramtable.dbd
cryo_DBD += drfm.dbd

ifneq ($(DEVIOCSTATS),)
cryo_DBD += devIocStats.dbd
cryo_LIBS += devIocStats
endif

ifneq ($(AUTOSAVE),)
cryo_DBD += asSupport.dbd
cryo_LIBS += autosave
endif

ifneq ($(CAPUTLOG),)
cryo_DBD += caPutLog.dbd
cryo_LIBS += caPutLog
endif

cryo_LIBS += paramtable pvDatabase qsrv pvaClient pvAccessIOC pvAccess pvAccessCA nt pvData

# cryo_registerRecordDeviceDriver.cpp derives from cryo.dbd
cryo_SRCS += cryo_registerRecordDeviceDriver.cpp
cryo_SRCS += drfm.cpp
cryo_SRCS += calc.c

# Build the main IOC entry point on workstation OSs.
cryo_SRCS_DEFAULT += cryoMain.cpp
cryo_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#cryo_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

ifeq ($(POSIX),YES)
cryo_SYS_LIBS += event_pthreads
#event_pthreads_DIR = $(LIBEVENT_$(T_A))/lib
endif

cryo_SYS_LIBS += event_core event_extra

USR_LDFLAGS += -L$(LIBEVENT)/lib

#event_core_DIR = $(LIBEVENT_$(T_A))/lib
#event_extra_DIR = $(LIBEVENT_$(T_A))/lib

# Link QSRV (pvAccess Server) if available
ifdef EPICS_QSRV_MAJOR_VERSION
    cryo_LIBS += qsrv
    cryo_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
    cryo_DBD += PVAServerRegister.dbd
    cryo_DBD += qsrv.dbd
endif

# Finally link to the EPICS Base libraries
cryo_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# Makefile
TOP = ..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

CXXFLAGS += -g -ggdb -O0
CXXFLAGS += -DHAVE_IPMITOOL

LIBRARY_IOC += epicsipmi

DBD += epicsipmi.dbd

epicsipmi_SRCS += epicsshell.cpp
epicsipmi_SRCS += provider.cpp
epicsipmi_SRCS += ipmitool.cpp

epicsipmi_LIBS += $(EPICS_BASE_IOC_LIBS)
epicsipmi_SYS_LIBS += ssl crypto

STATIC_LDLIBS += $(IPMITOOL)/lib/.libs/libipmitool.a
STATIC_LDLIBS += $(IPMITOOL)/src/plugins/.libs/libintf.a

USR_INCLUDES += -I $(IPMITOOL)/include
USR_INCLUDES += -I $(IPMITOOL)/src/plugins/lan
USR_CXXFLAGS += -std=c++11 -fpermissive

include $(TOP)/configure/RULES

#----------------------------------------
#  ADD RULES AFTER THIS LINE

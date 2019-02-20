# Makefile
TOP = ..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

CXXFLAGS += -g -ggdb -O0

LIBRARY_IOC += epicsipmi

DBD += epicsipmi.dbd

PROD_LIBS += freeipmi

epicsipmi_SRCS += epicsshell.cpp
epicsipmi_SRCS += epicsdevice.cpp
epicsipmi_SRCS += common.cpp
epicsipmi_SRCS += print.cpp
epicsipmi_SRCS += dispatcher.cpp
epicsipmi_SRCS += provider.cpp
epicsipmi_SRCS += freeipmiprovider.cpp

epicsipmi_LIBS += $(EPICS_BASE_IOC_LIBS)
epicsipmi_SYS_LIBS += ssl crypto

USR_CXXFLAGS += -std=c++11 -fpermissive

include $(TOP)/configure/RULES

#----------------------------------------
#  ADD RULES AFTER THIS LINE

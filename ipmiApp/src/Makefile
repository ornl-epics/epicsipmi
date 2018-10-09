TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

ipmitool_DIR = $(IPMITOOL)/lib/.libs/

PROD_IOC = ipmi
# ipmi.dbd will be created and installed
DBD += ipmi.dbd

# ipmi.dbd will be made up from these files:
ipmi_DBD += base.dbd
ipmi_DBD += epicsipmi.dbd
#ipmi_DBD += Logfile.dbd

# Include dbd files from all support applications:
#ipmi_DBD += xxx.dbd

# Add all the support libraries needed by this IOC
ipmi_LIBS += ipmitool
ipmi_LIBS += epicsipmi

# ipmi_registerRecordDeviceDriver.cpp derives from ipmi.dbd
ipmi_SRCS += ipmi_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
ipmi_SRCS_DEFAULT += main.cpp
ipmi_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#ipmi_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
ipmi_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE


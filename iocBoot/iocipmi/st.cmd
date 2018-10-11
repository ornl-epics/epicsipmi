#!../../bin/linux-x86_64/ipmi

< envPaths

cd ${TOP}

## Register all support components
dbLoadDatabase "dbd/epicsipmi.dbd"
ipmi_registerRecordDeviceDriver pdbbase

## Load record instances
#dbLoadRecords("db/xxx.db","")

cd ${TOP}/iocBoot/${IOC}
iocInit

ipmiConnect IPMI1 192.168.1.252
#ipmiScan IPMI1 verbose
ipmiDumpDb IPMI1 /tmp/test.db

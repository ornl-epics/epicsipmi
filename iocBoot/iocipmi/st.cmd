#!../../bin/linux-x86_64/ipmi

< envPaths

cd ${TOP}

## Register all support components
dbLoadDatabase "${TOP}/dbd/ipmi.dbd"
ipmi_registerRecordDeviceDriver pdbbase

ipmiConnect IPMI1 192.168.1.252 "" "" "none" "lan" "operator"

## Load record instances
dbLoadRecords("${TOP}/db/test.db","IPMI=IPMI:")

cd ${TOP}/iocBoot/${IOC}
iocInit

ipmiScan IPMI1 sensor
#ipmiDumpDb IPMI1 /tmp/test.db IPMI1:

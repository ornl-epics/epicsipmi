#include "provider.h"
#include "print.h"

#include <epicsExport.h>
#include <iocsh.h>

namespace epicsipmi {
namespace shell {

// ipmiConnect(conn_id, host_name, [username], [password], [protocol], [privlevel])
static const iocshArg ipmiConnectArg0 = { "connection id",  iocshArgString };
static const iocshArg ipmiConnectArg1 = { "host name",      iocshArgString };
static const iocshArg ipmiConnectArg2 = { "username",       iocshArgString };
static const iocshArg ipmiConnectArg3 = { "password",       iocshArgString };
static const iocshArg ipmiConnectArg4 = { "protocol",       iocshArgString };
static const iocshArg ipmiConnectArg5 = { "privlevel",      iocshArgInt };
static const iocshArg* ipmiConnectArgs[] = {
    &ipmiConnectArg0,
    &ipmiConnectArg1,
    &ipmiConnectArg2,
    &ipmiConnectArg3,
    &ipmiConnectArg4,
    &ipmiConnectArg5
};
static const iocshFuncDef ipmiConnectFuncDef = { "ipmiConnect", 6, ipmiConnectArgs };

extern "C" void ipmiConnectCallFunc(const iocshArgBuf* args) {
    if (!args[0].sval || !args[1].sval) {
        printf("Usage: ipmiConnect <conn id> <hostname> [username] [password] [protocol] [privlevel]\n");
        return;
    }

    provider::connect(args[0].sval,                        // connection id
                      args[1].sval,                        // hostname
                      args[2].sval ? args[2].sval : "",    // username
                      args[3].sval ? args[3].sval : "",    // password
                      args[4].sval ? args[4].sval : "lan", // protocol
                      args[5].ival);                       // privlevel
}

// ipmiScan(conn_id, [verbose])
static const iocshArg ipmiScanArg0 = { "connection id",     iocshArgString };
static const iocshArg ipmiScanArg1 = { "verbose",           iocshArgString };
static const iocshArg* ipmiScanArgs[] = {
    &ipmiScanArg0,
    &ipmiScanArg1,
};
static const iocshFuncDef ipmiScanFuncDef = { "ipmiScan", 2, ipmiScanArgs };

extern "C" void ipmiScanCallFunc(const iocshArgBuf* args) {
    if (!args[0].sval) {
        printf("Usage: ipmiScan <conn id> [verbose]\n");
        return;
    }
    auto entities = provider::scan(args[0].sval);

    if (args[1].sval && std::string(args[1].sval) == "verbose")
        print::printScanReportFull(args[0].sval, entities);
    else
        print::printScanReportBrief(args[0].sval, entities);
}

// ipmiDumpDb(conn_id, db_file)
static const iocshArg ipmiDumpDbArg0 = { "connection id",     iocshArgString };
static const iocshArg ipmiDumpDbArg1 = { "output file",       iocshArgString };
static const iocshArg* ipmiDumpDbArgs[] = {
    &ipmiDumpDbArg0,
    &ipmiDumpDbArg1,
};
static const iocshFuncDef ipmiDumpDbFuncDef = { "ipmiDumpDb", 2, ipmiDumpDbArgs };

extern "C" void ipmiDumpDbCallFunc(const iocshArgBuf* args) {
    if (!args[0].sval || !args[1].sval) {
        printf("Usage: ipmiDumpDb <conn id> <output file>\n");
        return;
    }
    auto entities = provider::scan(args[0].sval);
    print::printDatabase(entities, args[1].sval);
}

static void ipmiRegistrar ()
{
    static bool initialized  = false;
    if (!initialized) {
        initialized = false;
        iocshRegister(&ipmiConnectFuncDef, ipmiConnectCallFunc);
        iocshRegister(&ipmiScanFuncDef,    ipmiScanCallFunc);
        iocshRegister(&ipmiDumpDbFuncDef,  ipmiDumpDbCallFunc);
    }
}

extern "C" {
    epicsExportRegistrar(ipmiRegistrar);
}

} // namespace shell
} // namespace epicsipmi

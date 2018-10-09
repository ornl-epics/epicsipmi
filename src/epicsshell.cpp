#include "provider.h"

#include <cstring>
#include <epicsExport.h>
#include <iocsh.h>

namespace epicsipmi {
    namespace shell {
        void printScanReport(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities)
        {
            size_t len = entities.size();
            uint8_t indent = 1;
            while (len > 10) {
                indent++;
                len /= 10;
            }

            size_t i = 0;
            for (auto& entity: entities) {
                printf("%*zu: addr=%s ", indent, ++i, entity.addrspec.c_str());
                if (entities[i].type == provider::EntityInfo::Type::SENSOR) {
                    printf("type=sensor name=%s ", entity.name.c_str());
                } else {
                    printf("type=unknown");
                }
                printf("\n");
            }
        }

        std::string escapeEpicsName(const std::string& name)
        {
            //TODO
            return name;
        }

        void printAnalogRecord(FILE *db, const std::string& conn_id, bool output,
                               const std::string& name, const std::string& addrspec,
                               const std::string& description, const std::string& units,
                               unsigned precision, double lopr, double hopr,
                               double low, double high, double lolo, double hihi)
        {
            fprintf(db, "record(%s, \"$(IPMI)%s\") {\n", output ? "ao" : "ai", escapeEpicsName(name).c_str());
            fprintf(db, "  field(%s,  \"@ipmi(%s,%s)\")\n", output ? "OUT" : "INP", conn_id.c_str(), addrspec.c_str());
            fprintf(db, "  field(DESC, \"%s\")\n", description.substr(0, 40).c_str());
            if (!output)
                fprintf(db, "  field(SCAN, \"I/O Intr\")\n");

            if (precision != 0)
                fprintf(db, "  field(PREC, \"%u\")\n", precision);
            if (lopr != std::numeric_limits<double>::min())
                fprintf(db, "  field(LOPR, \"%f\")\n", lopr);
            if (hopr != std::numeric_limits<double>::max())
                fprintf(db, "  field(HOPR, \"%f\")\n", hopr);
            if (low != std::numeric_limits<double>::min()) {
                fprintf(db, "  field(LOW,  \"%f\")\n", low);
                fprintf(db, "  field(LSV,  \"MINOR\")\n");
            }
            if (low != std::numeric_limits<double>::max()) {
                fprintf(db, "  field(HIGH, \"%f\")\n", high);
                fprintf(db, "  field(HSV,  \"MINOR\")\n");
            }
            if (lolo != std::numeric_limits<double>::min()) {
                fprintf(db, "  field(LOW,  \"%f\")\n", lolo);
                fprintf(db, "  field(LSV,  \"MAJOR\")\n");
            }
            if (hihi != std::numeric_limits<double>::max()) {
                fprintf(db, "  field(HIGH, \"%f\")\n", hihi);
                fprintf(db, "  field(HSV,  \"MAJOR\")\n");
            }
            if (!units.empty())
                fprintf(db, "  field(EGU,  \"%s\")\n", units.c_str());

            fprintf(db, "}\n");
        }

        void printDiscreteRecord(FILE *db, const std::string& conn_id, bool output,
                                 const std::string& name, const std::string& addrspec,
                                 const std::string& description, const std::string& units)
        {
            fprintf(db, "record(%s, \"$(IPMI)%s\") {\n", output ? "longout" : "longin", escapeEpicsName(name).c_str());
            fprintf(db, "  field(%s,  \"@ipmi(%s,%s)\")\n", output ? "OUT" : "INP", conn_id.c_str(), addrspec.c_str());
            fprintf(db, "  field(DESC, \"%s\")\n", description.substr(0, 40).c_str());
            if (!output)
                fprintf(db, "  field(SCAN, \"I/O Intr\")\n");

            if (!units.empty())
                fprintf(db, "  field(EGU,  \"%s\")\n", units.c_str());

            fprintf(db, "}\n");
        }

        void printMenuRecord(FILE *db, const std::string& conn_id, bool output,
                             const std::string& name, const std::string& addrspec,
                             const std::string& description,
                             const std::vector<std::pair<uint8_t,std::string>>& options)
        {
            fprintf(db, "record(%s, \"$(IPMI)%s\") {\n", output ? "mbbo" : "mbbi", escapeEpicsName(name).c_str());
            fprintf(db, "  field(%s,  \"@ipmi(%s,%s)\")\n", output ? "OUT" : "INP", conn_id.c_str(), addrspec.c_str());
            fprintf(db, "  field(DESC, \"%s\")\n", description.substr(0, 40).c_str());
            if (!output)
                fprintf(db, "  field(SCAN, \"I/O Intr\")\n");

            std::vector<std::string> fields = {
                "ZR", "ON", "TW", "TH", "FR", "FV", "SX", "SV",
                "EI", "NI", "TE", "EL", "TV", "TT", "FT", "FF"
            };

            for (size_t i = 0; i < fields.size(); i++) {
                if (i >= options.size())
                    break;
                fprintf(db, "  field(%sVL, \"%u\")\n", fields[i].c_str(), options[i].first);
                fprintf(db, "  field(%sST, \"%s\")\n", fields[i].c_str(), options[i].second.c_str());
            }

            fprintf(db, "}\n");
        }

        void printDatabase(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities, const std::string& path)
        {
            FILE *db = fopen(path.c_str(), "w");
            if (db == nullptr) {
                printf("ERROR: Failed to open file to write database - %s", strerror(errno));
                return;
            }

            for (auto& entity: entities) {
                if (entity.type == provider::EntityInfo::Type::SENSOR) {
                    if (entity.sensor.analog) {
                        printAnalogRecord(db, conn_id, false, entity.name, entity.addrspec,
                                          entity.description, entity.sensor.units, entity.sensor.precision,
                                          entity.sensor.lopr, entity.sensor.hopr, entity.sensor.low,
                                          entity.sensor.high, entity.sensor.lolo, entity.sensor.hihi);
                    } else if (!entity.sensor.options.empty()) {
                        printMenuRecord(db, conn_id, false, entity.name, entity.addrspec,
                                        entity.description, entity.sensor.options);
                    } else {
                        printDiscreteRecord(db, conn_id, false, entity.name, entity.addrspec,
                                            entity.description, entity.sensor.units);
                    }
                }
            }

            fclose(db);
        }

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

        // ipmiScan(conn_id, [db_file])
        static const iocshArg ipmiScanArg0 = { "connection id",     iocshArgString };
        static const iocshArg ipmiScanArg1 = { "database filename", iocshArgString };
        static const iocshArg* ipmiScanArgs[] = {
            &ipmiScanArg0,
            &ipmiScanArg1,
        };
        static const iocshFuncDef ipmiScanFuncDef = { "ipmiScan", 2, ipmiScanArgs };

        extern "C" void ipmiScanCallFunc(const iocshArgBuf* args) {
            if (!args[0].sval) {
                printf("Usage: ipmiScan <conn id> [output db file]\n");
                return;
            }
            auto conn = provider::getConnection(args[0].sval);
            if (!conn) {
                printf("ERROR: No such connection '%s'\n", args[0].sval);
                return;
            }

            std::vector<provider::EntityInfo> entities;
            conn->scan(entities);
            printScanReport(args[0].sval, entities);

            if (args[1].sval)
                printDatabase(args[0].sval, entities, args[1].sval);
        }

        static void ipmiRegistrar ()
        {
            static bool initialized  = false;
            if (!initialized) {
                initialized = false;
                iocshRegister(&ipmiConnectFuncDef, ipmiConnectCallFunc);
                iocshRegister(&ipmiScanFuncDef,    ipmiScanCallFunc);
            }
        }

        extern "C" {
            epicsExportRegistrar(ipmiRegistrar);
        }

    } // namespace shell
} // namespace epicsipmi

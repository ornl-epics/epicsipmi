/* print.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Oct 2018
 */

#include "print.h"

#include <cassert>
#include <cstring>
#include <list>

namespace epicsipmi {
namespace print {

void printSensorRecords(const std::string& conn_id, FILE *dbfile, const std::string& prefix, const provider::EntityInfo& entity)
{
    assert(entity.type == provider::EntityInfo::Type::ANALOG_SENSOR);

    auto valprop = entity.properties.find("VAL");
    if (valprop == entity.properties.end() ||
        (valprop->value.type != provider::EntityInfo::Property::Value::Type::IVAL &&
        valprop->value.type != provider::EntityInfo::Property::Value::Type::DVAL)) {

        return;
    }
    bool analog = (valprop->value.type != provider::EntityInfo::Property::Value::Type::IVAL);

    fprintf(dbfile, "record(%s, \"%s%s\") {\n", (analog ? "ai" : "longin"), prefix.c_str(), entity.name.c_str());
    fprintf(dbfile, "  field(INP,  \"@ipmi(%s SENA %s)\")\n", conn_id.c_str(), entity.addrspec.c_str());
    fprintf(dbfile, "  field(DTYP, \"ipmi\")\n");
    fprintf(dbfile, "  field(DESC, \"%s\")\n", entity.description.substr(0, 40).c_str());

    auto it = entity.properties.find("UNIT");
    if (it != entity.properties.end()) {
        fprintf(dbfile, "  field(EGU,  \"%s\")\n", it->value.sval.c_str());
    }

    if (analog) {

        it = entity.properties.find("VAL");
        if (it != entity.properties.end()) {
            if (analog)
                fprintf(dbfile, "  field(VAL,  \"%f\")\n", it->value.dval);
            else
                fprintf(dbfile, "  field(VAL,  \"%d\")\n", it->value.ival);
        }

        it = entity.properties.find("PREC");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(PREC, \"%u\")\n", it->value.ival);
        }

        it = entity.properties.find("LOPR");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(LOPR, \"%f\")\n", it->value.dval);
        }

        it = entity.properties.find("HOPR");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(HOPR, \"%f\")\n", it->value.dval);
        }

        it = entity.properties.find("LOW");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(LOW,  \"%f\")\n", it->value.dval);
            fprintf(dbfile, "  field(LSV,  \"MINOR\")\n");
        }

        it = entity.properties.find("HIGH");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(HIGH, \"%f\")\n", it->value.dval);
            fprintf(dbfile, "  field(HSV,  \"MINOR\")\n");
        }

        it = entity.properties.find("LOLO");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(LOLO, \"%f\")\n", it->value.dval);
            fprintf(dbfile, "  field(LLSV, \"MAJOR\")\n");
        }

        it = entity.properties.find("HIGHI");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(HIHI, \"%f\")\n", it->value.dval);
            fprintf(dbfile, "  field(HHSV, \"MAJOR\")\n");
        }

        it = entity.properties.find("HYST");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(HYST, \"%f\")\n", it->value.dval);
        }
    }

    fprintf(dbfile, "}\n");

    for (auto& property: entity.properties) {
        if (property.name == "VAL" || property.writable == false)
            continue;

        fprintf(dbfile, "record(%s, \"%s%s:%s\") {\n", (analog ? "ao" : "longout"), prefix.c_str(), entity.name.c_str(), property.name.c_str());
        fprintf(dbfile, "  field(OUT,  \"@ipmi(%s SENA %s %s)\")\n", conn_id.c_str(), entity.addrspec.c_str(), property.name.c_str());
        fprintf(dbfile, "  field(DTYP, \"ipmi\")\n");
        fprintf(dbfile, "  field(FLNK, \"$(IPMI)%s\")\n", entity.addrspec.c_str());
        fprintf(dbfile, "}\n");
    }
}

void printFruRecords(const std::string& conn_id, FILE *dbfile, const std::string& prefix, const provider::EntityInfo& entity)
{
    assert(entity.type == provider::EntityInfo::Type::FRU);

    fprintf(dbfile, "record(stringin, \"%s%s\") {\n", prefix.c_str(), entity.name.c_str());
    fprintf(dbfile, "  field(DESC, \"%s\")\n", entity.description.substr(0, 40).c_str());
    fprintf(dbfile, "}\n");

    for (auto& property: entity.properties) {
        fprintf(dbfile, "record(stringin, \"%s%s:%s\") {\n", prefix.c_str(), entity.name.c_str(), property.name.c_str());
        fprintf(dbfile, "  field(VAL,  \"%s\")\n", property.value.sval.c_str());
        fprintf(dbfile, "  field(INP,  \"@ipmi(%s FRU %s %s)\")\n", conn_id.c_str(), entity.addrspec.c_str(), property.name.c_str());
        fprintf(dbfile, "  field(DTYP, \"ipmi\")\n");
        fprintf(dbfile, "}\n");
    }
}

/*
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
*/

void printDatabase(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities, const std::string& path, const std::string& prefix)
{
    FILE *dbfile = fopen(path.c_str(), "w");
    if (dbfile == nullptr) {
        printf("ERROR: Failed to open file to write database - %s", strerror(errno));
        return;
    }

    for (auto& entity: entities) {
        if (entity.type == provider::EntityInfo::Type::ANALOG_SENSOR) {
            printSensorRecords(conn_id, dbfile, prefix, entity);
        } else if (entity.type == provider::EntityInfo::Type::FRU) {
            printFruRecords(conn_id, dbfile, prefix, entity);
        }
    }

    fclose(dbfile);
}

void printScanReportBrief(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities)
{
    size_t len = entities.size();
    uint8_t indent = 1;
    while (len > 10) {
        indent++;
        len /= 10;
    }

    size_t i = 0;
    for (auto& entity: entities) {
        const char *type = "UNKNOWN";
        if (entities[i].type == provider::EntityInfo::Type::ANALOG_SENSOR) {
            type = "SENSOR";
        } else if (entities[i].type == provider::EntityInfo::Type::FRU) {
            type = "FRU";
        }
        printf("%*zu: %s %s\n", indent, ++i, type, entity.description.c_str());
    }
}

void printScanReportFull(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities)
{
    size_t len = entities.size();
    uint8_t indent = 1;
    while (len > 10) {
        indent++;
        len /= 10;
    }

    size_t i = 0;
    for (auto& entity: entities) {
        const char *type = "UNKNOWN";
        if (entities[i].type == provider::EntityInfo::Type::ANALOG_SENSOR) {
            type = "SENSOR";
        } else if (entities[i].type == provider::EntityInfo::Type::FRU) {
            type = "FRU";
        }
        printf("%*zu: %s (%s %s)\n", indent, ++i, entity.description.c_str(), type, entity.addrspec.c_str());

        for (auto& property: entity.properties) {
            printf("%*s  * %s=%s\n",      indent, "", property.name.c_str(), property.value.sval.c_str());
        }
    }
}

}; // namespace print
}; // namespace epicsipmi

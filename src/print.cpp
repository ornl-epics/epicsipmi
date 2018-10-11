#include "print.h"

#include <cassert>
#include <cstring>
#include <list>

namespace epicsipmi {
namespace print {

void printSensorRecords(FILE *dbfile, const provider::EntityInfo& entity)
{
    assert(entity.type == provider::EntityInfo::Type::SENSOR);

    auto valprop = entity.properties.find("VAL");
    if (valprop == entity.properties.end() ||
        (valprop->valtype != provider::EntityInfo::Property::ValueType::IVAL &&
        valprop->valtype != provider::EntityInfo::Property::ValueType::DVAL)) {

        return;
    }
    bool analog = (valprop->valtype != provider::EntityInfo::Property::ValueType::IVAL);

    fprintf(dbfile, "record(%s, \"$(IPMI)%s\") {\n", (analog ? "ai" : "longin"), entity.name.c_str());
    fprintf(dbfile, "  field(INP,  \"@ipmi(%s)\")\n", valprop->addrspec.c_str());
    fprintf(dbfile, "  field(DESC, \"%s\")\n", entity.description.substr(0, 40).c_str());

    auto it = entity.properties.find("UNIT");
    if (it != entity.properties.end()) {
        fprintf(dbfile, "  field(EGU,  \"%s\")\n", it->sval.c_str());
    }

    if (analog) {

        it = entity.properties.find("PREC");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(PREC, \"%u\")\n", it->ival);
        }

        it = entity.properties.find("LOPR");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(LOPR, \"%f\")\n", it->dval);
        }

        it = entity.properties.find("HOPR");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(HOPR, \"%f\")\n", it->dval);
        }

        it = entity.properties.find("LOW");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(LOW,  \"%f\")\n", it->dval);
            fprintf(dbfile, "  field(LSV,  \"MINOR\")\n");
        }

        it = entity.properties.find("HIGH");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(HIGH, \"%f\")\n", it->dval);
            fprintf(dbfile, "  field(HSV,  \"MINOR\")\n");
        }

        it = entity.properties.find("LOLO");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(LOLO, \"%f\")\n", it->dval);
            fprintf(dbfile, "  field(LLSV, \"MAJOR\")\n");
        }

        it = entity.properties.find("HIGHI");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(HIHI, \"%f\")\n", it->dval);
            fprintf(dbfile, "  field(HHSV, \"MAJOR\")\n");
        }

        it = entity.properties.find("HYST");
        if (it != entity.properties.end()) {
            fprintf(dbfile, "  field(HYST, \"%f\")\n", it->dval);
        }
    }

    fprintf(dbfile, "}\n");
}

void printDeviceRecords(FILE *dbfile, const provider::EntityInfo& entity)
{
    assert(entity.type == provider::EntityInfo::Type::DEVICE);

    fprintf(dbfile, "record(stringin, \"$(IPMI)%s\") {\n", entity.name.c_str());
    fprintf(dbfile, "  field(DESC, \"%s\")\n", entity.description.substr(0, 40).c_str());
    fprintf(dbfile, "}\n");

    for (auto& property: entity.properties) {
        fprintf(dbfile, "record(stringin, \"$(IPMI)%s:%s\") {\n", entity.name.c_str(), property.name.c_str());
        fprintf(dbfile, "  field(VAL,  \"%s\")\n", property.sval.c_str());
        if (!property.addrspec.empty()) {
            fprintf(dbfile, "  field(INP,  \"@ipmi(%s)\")\n", property.addrspec.c_str());
        }
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

void printDatabase(const std::vector<provider::EntityInfo>& entities, const std::string& path)
{
    FILE *dbfile = fopen(path.c_str(), "w");
    if (dbfile == nullptr) {
        printf("ERROR: Failed to open file to write database - %s", strerror(errno));
        return;
    }

    for (auto& entity: entities) {
        if (entity.type == provider::EntityInfo::Type::SENSOR) {
            printSensorRecords(dbfile, entity);
        } else if (entity.type == provider::EntityInfo::Type::DEVICE) {
            printDeviceRecords(dbfile, entity);
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
        if (entities[i].type == provider::EntityInfo::Type::SENSOR) {
            type = "SENSOR";
        } else if (entities[i].type == provider::EntityInfo::Type::DEVICE) {
            type = "DEVICE";
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
        if (entities[i].type == provider::EntityInfo::Type::SENSOR) {
            type = "SENSOR";
        } else if (entities[i].type == provider::EntityInfo::Type::DEVICE) {
            type = "DEVICE";
        }
        printf("%*zu: %s (%s)\n", indent, ++i, entity.description.c_str(), type);

        for (auto& property: entity.properties) {
            if (property.addrspec.empty())
                printf("%*s  * %s=%s\n",      indent, "", property.name.c_str(), property.sval.c_str());
            else
                printf("%*s  * %s=%s (%s)\n", indent, "", property.name.c_str(), property.sval.c_str(), property.addrspec.c_str());
        }
    }
}

}; // namespace print
}; // namespace epicsipmi

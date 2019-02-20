/* print.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Feb 2019
 */

#include "common.h"
#include "print.h"

#include <cstring>
#include <limits>
#include <iomanip>
#include <iostream>

#include <alarm.h>
#include <alarmString.h>

namespace print {

void printScanReport(const std::string& header, const std::vector<Provider::Entity>& entities)
{
    size_t len = entities.size();
    uint8_t indent = 1;
    while (len > 10) {
        indent++;
        len /= 10;
    }

    std::cout << header << std::endl;
    unsigned i = 1;
    for (auto& entity: entities) {
        std::cout << std::right << std::setw(indent) << i++ << ": ";

        auto desc = entity.getField<std::string>("DESC", "<missing desc>");
        std::cout << std::left << std::setw(41) << desc.substr(0, 41) << " ";

        auto stringValue = entity.getField<std::string>("VAL", "<UDF string value>");
        auto doubleValue = entity.getField<double>     ("VAL", std::numeric_limits<double>::min());
        auto intValue    = entity.getField<int>        ("VAL", std::numeric_limits<int>::min());
        if (intValue != std::numeric_limits<int>::min())
            std::cout << intValue << " ";
        else if (doubleValue != std::numeric_limits<double>::min())
            std::cout << std::setprecision(2) << std::fixed << doubleValue << " ";
        else if (stringValue != "<UDF string value>")
            std::cout << stringValue << " ";
        else
            std::cout << "N/A ";

        auto unit = entity.getField<std::string>("UNIT", "");
        if (unit != "")
            std::cout << unit << " ";

        auto stat = entity.getField<int>("STAT", epicsAlarmNone);
        if (stat != epicsAlarmNone && stat < ALARM_NSTATUS)
            std::cout << epicsAlarmConditionStrings[stat] << " ";

        auto sevr = entity.getField<int>("SEVR", epicsSevNone);
        if (sevr != epicsSevNone && sevr < ALARM_NSEV)
            std::cout << epicsAlarmSeverityStrings[sevr] << " ";

        std::cout << std::endl;
    }
}

std::string _epicsEscape(const std::string& str)
{
    std::string escaped = str;
    for (auto it = escaped.begin(); it != escaped.end(); it++) {
        if (isalnum(*it) == 0) {
            if (*it == ' ') *it = ':';
            else            *it = '_';
        }
    }
    return escaped;
}

void _printRecord(FILE* dbfile, const std::string& prefix, const Provider::Entity& entity)
{
    auto stringValue = entity.getField<std::string>("VAL", "<UDF string value>");
    auto doubleValue = entity.getField<double>     ("VAL", std::numeric_limits<double>::min());
    auto intValue    = entity.getField<int>        ("VAL", std::numeric_limits<int>::min());

    auto inp = entity.getField<std::string>("INP", "");
    auto out = entity.getField<std::string>("OUT", "");

    std::string recordType;
    if (out != "") {
        if (doubleValue != std::numeric_limits<double>::min()) recordType = "ao";
        else if (intValue != std::numeric_limits<int>::min())  recordType = "longout";
        else if (stringValue != "<UDF string value>")          recordType = "stringout";
        else {
            LOG_WARN("Record didn't specify value field, skipping");
            return;
        }
    } else if (inp != "") {
        if (doubleValue != std::numeric_limits<double>::min()) recordType = "ai";
        else if (intValue != std::numeric_limits<int>::min())  recordType = "longin";
        else if (stringValue != "<UDF string value>")          recordType = "stringin";
        else {
            LOG_WARN("Record didn't specify value field, skipping");
            return;
        }
    } else {
        LOG_WARN("Record didn't specify input or output link field, skipping");
        return;
    }

    auto name = entity.getField<std::string>("NAME", "");
    if (name == "") {
        LOG_WARN("Record didn't specify name field, skipping");
        return;
    }

    auto desc = entity.getField<std::string>("DESC", "");
    auto egu  = entity.getField<std::string>("EGU", "");
    auto prec = entity.getField<int>("PREC", std::numeric_limits<int>::min());
    auto lopr = entity.getField<double>("LOPR", std::numeric_limits<double>::min());
    auto hopr = entity.getField<double>("HOPR", std::numeric_limits<double>::min());
    auto low  = entity.getField<double>("LOW",  std::numeric_limits<double>::min());
    auto lolo = entity.getField<double>("LOLO", std::numeric_limits<double>::min());
    auto high = entity.getField<double>("HIGH", std::numeric_limits<double>::min());
    auto hihi = entity.getField<double>("HIHI", std::numeric_limits<double>::min());
    auto hyst = entity.getField<double>("HYST", std::numeric_limits<double>::min());
    auto recordName = prefix + _epicsEscape(name);

    fprintf(dbfile,     "record(%s, \"%s\") {\n", recordType.c_str(), recordName.c_str());
    fprintf(dbfile,     "  field(%s,   \"%s\")\n", (!out.empty() ? "OUT" : "INP"), (!out.empty() ? out.c_str() : inp.c_str()));
    fprintf(dbfile,     "  field(DTYP, \"ipmi\")\n");
    fprintf(dbfile,     "  field(NAME, \"%s\")\n", name.substr(0, 60).c_str());
    if (desc != "")
        fprintf(dbfile, "  field(DESC, \"%s\")\n", desc.substr(0, 40).c_str());
    if (egu != "" && stringValue == "<UDF string value>")
        fprintf(dbfile, "  field(EGU,  \"%s\")\n", egu.substr(0, 15).c_str());
    if (doubleValue != std::numeric_limits<double>::min())
        fprintf(dbfile, "  field(VAL,  \"%f\")\n", doubleValue);
    if (intValue != std::numeric_limits<int>::min())
        fprintf(dbfile, "  field(VAL,  \"%d\")\n", intValue);
    if (stringValue != "<UDF string value>")
        fprintf(dbfile, "  field(VAL,  \"%s\")\n", stringValue.substr(0, 39).c_str());
    if (prec != std::numeric_limits<int>::min())
        fprintf(dbfile, "  field(PREC, \"%d\")\n", prec);
    if (lopr != std::numeric_limits<double>::min() && doubleValue != std::numeric_limits<double>::min())
        fprintf(dbfile, "  field(LOPR, \"%f\")\n", lopr);
    if (hopr != std::numeric_limits<double>::min() && doubleValue != std::numeric_limits<double>::min())
        fprintf(dbfile, "  field(HOPR, \"%f\")\n", hopr);
    if (low != std::numeric_limits<double>::min() && doubleValue != std::numeric_limits<double>::min()) {
        fprintf(dbfile, "  field(LOW,  \"%f\")\n", low);
        fprintf(dbfile, "  field(LSV,  \"MINOR\")\n");
    }
    if (lolo != std::numeric_limits<double>::min() && doubleValue != std::numeric_limits<double>::min()) {
        fprintf(dbfile, "  field(LOLO, \"%f\")\n", lolo);
        fprintf(dbfile, "  field(LLSV, \"MAJOR\")\n");
    }
    if (high != std::numeric_limits<double>::min() && doubleValue != std::numeric_limits<double>::min()) {
        fprintf(dbfile, "  field(HIGH, \"%f\")\n", high);
        fprintf(dbfile, "  field(HSV,  \"MINOR\")\n");
    }
    if (hihi != std::numeric_limits<double>::min() && doubleValue != std::numeric_limits<double>::min()) {
        fprintf(dbfile, "  field(HIHI, \"%f\")\n", hihi);
        fprintf(dbfile, "  field(HHSV, \"MAJOR\")\n");
    }
    if (hyst != std::numeric_limits<double>::min() && doubleValue != std::numeric_limits<double>::min())
        fprintf(dbfile, "  field(HYST, \"%f\")\n", hyst);
    fprintf(dbfile,     "}\n");
}

void printDatabase(const std::string& path, const std::vector<Provider::Entity>& entities, const std::string& pv_prefix)
{
    FILE *dbfile = fopen(path.c_str(), "w+");
    if (dbfile == nullptr)
        throw std::runtime_error("failed to open output database file - " + std::string(strerror(errno)));

    for (auto& entity: entities) {
        _printRecord(dbfile, pv_prefix, entity);
    }

    fclose(dbfile);
}

}; // namespace print

#pragma once

#include "provider.h"

namespace epicsipmi {
namespace print {

void printDatabase(const std::vector<provider::EntityInfo>& entities, const std::string& path);
void printScanReportBrief(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities);
void printScanReportFull(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities);

}; // namespace print
}; // namespace epicsipmi

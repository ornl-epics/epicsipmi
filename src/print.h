/* print.h
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Oct 2018
 */

#pragma once

#include "provider.h"

namespace epicsipmi {
namespace print {

void printDatabase(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities, const std::string& path);
void printScanReportBrief(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities);
void printScanReportFull(const std::string& conn_id, const std::vector<provider::EntityInfo>& entities);

}; // namespace print
}; // namespace epicsipmi

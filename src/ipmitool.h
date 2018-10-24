/* ipmitool.h
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

#include <string>

extern "C" {
struct ipmi_intf;
struct sdr_record_common_sensor;
struct sdr_record_full_sensor;
struct sdr_record_compact_sensor;
struct sdr_record_fru_locator;
struct fru_info;
} // extern "C"

namespace epicsipmi {
namespace provider {

class IpmiToolProvider : public provider::BaseProvider
{
    private:
        ::ipmi_intf* m_intf = nullptr;

    public:
        bool connect(const std::string& hostname,
                     const std::string& username, const std::string& password,
                     const std::string& protocol, int privlevel) override;

        std::vector<EntityInfo> scan() override;

        ReturnCode getSensor(const std::string& addrspec, double& val, double& low, double& lolo, double& high, double& hihi, int16_t& prec, double& hyst) override;
        ReturnCode getFruProperties(const std::string& addrspec, EntityInfo::Properties& properties) override;

    private:
        std::vector<uint8_t> findIpmbs();
        EntityInfo extractSensorInfo(::sdr_record_full_sensor*    sdr);
        EntityInfo extractSensorInfo(::sdr_record_compact_sensor* sdr);
        EntityInfo extractSensorInfo(::sdr_record_common_sensor*  sdr);
        EntityInfo extractFruInfo(::sdr_record_fru_locator* fru);

        // Low-level IPMI functions that ipmitool doesn't provide
        uint32_t getFruAreaOffset(uint8_t device_id, struct ::fru_info &fruinfo, uint8_t area);
        bool getFruChassisProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties);
        bool getFruBoardProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties);
        bool getFruProductProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties);
};

} // namespace provider
} // namespace epicsipmi

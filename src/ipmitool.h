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
        IpmiToolProvider(const std::string& conn_id);

        bool connect(const std::string& hostname,
                     const std::string& username, const std::string& password,
                     const std::string& protocol, int privlevel) override;

        bool getValue(const std::string& addrspec, int& value) override;
        bool getValue(const std::string& addrspec, double& value) override;
        bool getValue(const std::string& addrspec, std::string& value) override;
        std::vector<EntityInfo> scan() override;

    private:
        std::vector<uint8_t> findIpmbs();
        EntityInfo extractSensorInfo(::sdr_record_full_sensor*    sdr);
        EntityInfo extractSensorInfo(::sdr_record_compact_sensor* sdr);
        EntityInfo extractSensorInfo(::sdr_record_common_sensor*  sdr);
        EntityInfo extractDeviceInfo(::sdr_record_fru_locator* fru);

        std::string getDeviceAddr(::sdr_record_fru_locator& fru, const std::string& suffix="");
        std::string getSensorAddr(::sdr_record_common_sensor& sdr, const std::string& suffix="VAL");

        // Low-level IPMI functions that ipmitool doesn't provide
        uint32_t getFruAreaOffset(uint8_t device_id, struct ::fru_info &fruinfo, uint8_t area);
        bool getFruChassisProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties);
        bool getFruBoardProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties);
        bool getFruProductProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties);
};

} // namespace provider
} // namespace epicsipmi

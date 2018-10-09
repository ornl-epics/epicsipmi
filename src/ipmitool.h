#pragma once

#include "provider.h"

#include <string>

extern "C" {
struct ipmi_intf;
struct sdr_record_common_sensor;
struct sdr_record_full_sensor;
struct sdr_record_compact_sensor;
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
                     const std::string& protocol, int privlevel);

        bool scan(std::vector<provider::EntityInfo>& entities);

        bool getValue(const std::string& addrspec, int& value);
        bool getValue(const std::string& addrspec, double& value);
        bool getValue(const std::string& addrspec, std::string& value);

    private:
        std::vector<uint8_t> findIpmbs();
        std::vector<provider::EntityInfo> scanSensors();
        provider::EntityInfo extractSensorInfo(::sdr_record_full_sensor*    sdr);
        provider::EntityInfo extractSensorInfo(::sdr_record_compact_sensor* sdr);
        provider::EntityInfo extractSensorInfo(::sdr_record_common_sensor*  sdr);
};

} // namespace provider
} // namespace epicsipmi

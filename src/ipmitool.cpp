// Include ipmitool headers
extern "C" {
#include <ipmitool/ipmi_intf.h>
#include <ipmitool/ipmi_entity.h>
#include <ipmitool/ipmi_fru.h>
#include <ipmitool/log.h>
#include <ipmitool/ipmi_sdr.h>
#include <ipmitool/ipmi_sel.h>
#include <ipmitool/ipmi_sensor.h>
int verbose = 0;
int csv_output = 0;
int time_in_utc = 0;
};

#include "ipmitool.h"

#include <array>
#include <memory>

namespace epicsipmi {
namespace provider {

bool IpmiToolProvider::connect(const std::string& hostname,
                               const std::string& username, const std::string& password,
                               const std::string& protocol, int privlevel)
{
    static bool ipmitool_init = false;
    if (!ipmitool_init) {
        // Without initializing ipmi logging, intf->sendrecv in ipmi_sdr_get_sensor_reading_ipmb()
        // never gets a value back. Go figure.
        ::log_init("epicsipmi", false, ::verbose);
        ipmitool_init = true;
    }

    m_intf = ::ipmi_intf_load(const_cast<char*>(protocol.c_str()));
    if (!m_intf) {
        //IPMI_LOG_ERROR("Cannot load interface '%s'", _proto.c_str());
        return false;
    }

    ::ipmi_intf_session_set_hostname(m_intf, const_cast<char*>(hostname.c_str()));
    ::ipmi_intf_session_set_username(m_intf, const_cast<char*>(username.c_str()));
    ::ipmi_intf_session_set_password(m_intf, const_cast<char*>(password.c_str()));

    if (privlevel > 0)
        ::ipmi_intf_session_set_privlvl(m_intf, (uint8_t)privlevel);

    // ipmitool: "See table 22-19 of the IPMIv2 spec"
    ::ipmi_intf_session_set_cipher_suite_id(m_intf, 3);
    std::array<uint8_t, IPMI_KG_BUFFER_SIZE> kgkey;
    kgkey.fill(0);
    ::ipmi_intf_session_set_kgkey(m_intf, kgkey.data());
    ::ipmi_intf_session_set_lookupbit(m_intf, 0x10);

    if ((m_intf->open == nullptr) || (m_intf->open(m_intf) == -1)) {
        //IPMI_LOG_ERROR("Failed to open connection to '%s'", _hostname.c_str());
        ::ipmi_cleanup(m_intf);
        m_intf = nullptr;
        return false;
    } // if

    //IPMI_LOG_INFO("Connected to '%s'", _hostname.c_str());
    return true;
}

bool IpmiToolProvider::scan(std::vector<EntityInfo>& entities)
{
    entities = scanSensors();
    // TODO: FRUs, SEL, etc.
    return true;
}

std::vector<EntityInfo> IpmiToolProvider::scanSensors()
{
    std::vector<EntityInfo> entities;

    if (!m_intf)
        return entities;

    uint8_t local_addr = m_intf->target_addr;
    auto addresses = findIpmbs();
    addresses.insert(addresses.begin(), local_addr);

    for (auto &addr: addresses) {
        m_intf->target_addr = addr;

        std::unique_ptr<::ipmi_sdr_iterator, decltype(::free)*> it{::ipmi_sdr_start(m_intf, 0), ::free};
        if (!it)
            continue;

        while (::sdr_get_rs* header = ::ipmi_sdr_get_next_header(m_intf, it.get())) {
            uint8_t* const rec = ::ipmi_sdr_get_record(m_intf, header, it.get());
            if (rec == nullptr)
                continue;

            switch (header->type) {
                case SDR_RECORD_TYPE_FULL_SENSOR:
                    entities.emplace_back( extractSensorInfo(reinterpret_cast<::sdr_record_full_sensor*>(rec)) );
                    break;
                case SDR_RECORD_TYPE_COMPACT_SENSOR:
                    entities.emplace_back( extractSensorInfo(reinterpret_cast<::sdr_record_compact_sensor*>(rec)) );
                    break;
                case SDR_RECORD_TYPE_MC_DEVICE_LOCATOR:
                    //slaves_.insert(reinterpret_cast< ::sdr_record_mc_locator*>(rec)->dev_slave_addr);
                    break;
                default:
                    //SuS_LOG_STREAM(finest, log_id(), "ignoring sensor type 0x" << std::hex << +header->type << ".");
                    break;
            }
            ::free(rec);
        }
    }

    m_intf->target_addr = local_addr;
    return entities;
}

std::vector<uint8_t> IpmiToolProvider::findIpmbs()
{
    std::vector<uint8_t> ipmbs;
    return ipmbs;
}

EntityInfo IpmiToolProvider::extractSensorInfo(::sdr_record_full_sensor* sdr)
{
    EntityInfo entity = extractSensorInfo(reinterpret_cast<::sdr_record_common_sensor*>(sdr));
    entity.sensor.analog = true;

    size_t idlen = std::min(sizeof(sdr->id_string), (size_t)(sdr->id_code & 0x1f));
    entity.name.assign((const char *)sdr->id_string, idlen);

    // swap for 1/x conversions, cf. section 36.5 of IPMI specification
    bool swap_hi_lo = (sdr->linearization == SDR_SENSOR_L_1_X);

    // Operating range
    if (swap_hi_lo) {
        entity.sensor.lopr = ::sdr_convert_sensor_reading(sdr, sdr->sensor_max);
        entity.sensor.hopr = ::sdr_convert_sensor_reading(sdr, sdr->sensor_min);
    } else {
        entity.sensor.lopr = ::sdr_convert_sensor_reading(sdr, sdr->sensor_min);
        entity.sensor.hopr = ::sdr_convert_sensor_reading(sdr, sdr->sensor_max);
    }

    // Limits
    if (sdr->cmn.mask.type.threshold.read.ucr) {
        if (swap_hi_lo) {
            entity.sensor.lolo = ::sdr_convert_sensor_reading(sdr, sdr->threshold.upper.critical);
        } else {
            entity.sensor.hihi = ::sdr_convert_sensor_reading(sdr, sdr->threshold.upper.critical);
        }
    }
    if (sdr->cmn.mask.type.threshold.read.lcr) {
        if (swap_hi_lo) {
            entity.sensor.hihi = ::sdr_convert_sensor_reading(sdr, sdr->threshold.lower.critical);
        } else {
            entity.sensor.lolo = ::sdr_convert_sensor_reading(sdr, sdr->threshold.lower.critical);
        }
    }
    if (sdr->cmn.mask.type.threshold.read.unc) {
        if (swap_hi_lo) {
            entity.sensor.low  = ::sdr_convert_sensor_reading(sdr, sdr->threshold.upper.non_critical);
        } else {
            entity.sensor.high = ::sdr_convert_sensor_reading(sdr, sdr->threshold.upper.non_critical);
        }
    }
    if (sdr->cmn.mask.type.threshold.read.lnc) {
        if (swap_hi_lo) {
            entity.sensor.high = ::sdr_convert_sensor_reading(sdr, sdr->threshold.lower.non_critical);
        } else {
            entity.sensor.low  = ::sdr_convert_sensor_reading(sdr, sdr->threshold.lower.non_critical);
        }
    }

    // Hysteresis is given in raw values
    if (sdr->linearization == SDR_SENSOR_L_LINEAR) {
        if ((sdr->cmn.sensor.capabilities.hysteresis == 1) || (sdr->cmn.sensor.capabilities.hysteresis == 2)) {
            if (sdr->threshold.hysteresis.positive != 0x0 && sdr->threshold.hysteresis.positive != 0xFF) {
                double hyst = ::sdr_convert_sensor_hysterisis(sdr, sdr->threshold.hysteresis.positive);
                if (hyst != 0.0)
                    entity.sensor.hyst = hyst;
            }
            if (sdr->threshold.hysteresis.negative != 0x0 && sdr->threshold.hysteresis.negative != 0xFF) {
                double hyst = ::sdr_convert_sensor_hysterisis(sdr, sdr->threshold.hysteresis.negative);
                if (hyst != 0.0)
                    entity.sensor.hyst = hyst;
            }
        }
    }

    // Units
    const char *units = ipmi_sdr_get_unit_string(sdr->cmn.unit.pct, sdr->cmn.unit.modifier, sdr->cmn.unit.type.base, sdr->cmn.unit.type.modifier);
    if (units)
        entity.sensor.units = units;

    return entity;
}

EntityInfo IpmiToolProvider::extractSensorInfo(::sdr_record_compact_sensor* sdr)
{
    EntityInfo entity = extractSensorInfo(reinterpret_cast<::sdr_record_common_sensor*>(sdr));
    entity.sensor.analog = false;

    size_t idlen = std::min(sizeof(sdr->id_string), (size_t)(sdr->id_code & 0x1f));
    entity.name.assign((const char *)sdr->id_string, idlen);

    if (sdr->cmn.unit.analog == 3) {
        if (sdr->threshold.hysteresis.positive != 0x0 && sdr->threshold.hysteresis.positive != 0xFF) {
            entity.sensor.hyst = sdr->threshold.hysteresis.positive;
        }
        if (sdr->threshold.hysteresis.negative != 0x0 && sdr->threshold.hysteresis.negative != 0xFF) {
            entity.sensor.hyst = sdr->threshold.hysteresis.negative;
        }
    }

    return entity;
}

EntityInfo IpmiToolProvider::extractSensorInfo(::sdr_record_common_sensor* sdr)
{
    EntityInfo entity;
    entity.type = EntityInfo::Type::SENSOR;

    // Build a menu
    if (sdr->event_type == 0x6F) {
        for (auto event = ::sensor_specific_event_types; event->desc != NULL; event++) {
            if (event->code == sdr->sensor.type)
                entity.sensor.options.emplace_back( std::make_pair(event->offset, event->desc) );
        }
    } else {
        for (auto event = ::generic_event_types; event->desc != NULL; event++) {
            if (event->code == sdr->event_type)
                entity.sensor.options.emplace_back( std::make_pair(event->offset, event->desc) );
        }
    }

    // Units
    if (sdr->unit.modifier > 0) {
        entity.sensor.units = ::ipmi_sdr_get_unit_string(sdr->unit.pct, sdr->unit.modifier, sdr->unit.type.base, sdr->unit.type.modifier);
    }

    return entity;
}

bool IpmiToolProvider::getValue(const std::string& addrspec, int& value)
{
    return false;
}

bool IpmiToolProvider::getValue(const std::string& addrspec, double& value)
{
    return false;
}

bool IpmiToolProvider::getValue(const std::string& addrspec, std::string& value)
{
    return false;
}

} // namespace provider
} // namespace epicsipmi

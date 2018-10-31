/* ipmitool.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Oct 2018
 */

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

// Forward declaration of ipmitool functions not exposes through .h files
int read_fru_area(struct ipmi_intf * intf, struct fru_info *fru, uint8_t id,
                  uint32_t offset, uint32_t length, uint8_t *frubuf);
};

#include "ipmitool.h"

#include <array>
#include <cassert>
#include <memory>
#include <regex>

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

std::vector<EntityInfo> IpmiToolProvider::scan()
{
    auto comp = [](const ::entity_id& lhs, const ::entity_id& rhs) {
        // must be non-reflexive, transitive, anti-symmetric, negative transitive
        if (lhs.id != rhs.id) return lhs.id < rhs.id;
        if (lhs.instance != rhs.instance) return lhs.instance < rhs.instance;
        return lhs.logical != rhs.logical;
    };

    std::vector<EntityInfo> entities;
    std::map<struct ::entity_id, size_t, decltype(comp)> frus(comp);
    std::map<size_t, struct ::entity_id> sensors;

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
                    sensors[entities.size()] = reinterpret_cast<::sdr_record_full_sensor*>(rec)->cmn.entity;
                    break;
                case SDR_RECORD_TYPE_COMPACT_SENSOR:
                    entities.emplace_back( extractSensorInfo(reinterpret_cast<::sdr_record_compact_sensor*>(rec)) );
                    sensors[entities.size()] = reinterpret_cast<::sdr_record_compact_sensor*>(rec)->cmn.entity;
                    break;
                case SDR_RECORD_TYPE_MC_DEVICE_LOCATOR:
                    //slaves_.insert(reinterpret_cast< ::sdr_record_mc_locator*>(rec)->dev_slave_addr);
                    break;
                case SDR_RECORD_TYPE_FRU_DEVICE_LOCATOR:
                    entities.emplace_back( extractFruInfo(reinterpret_cast<::sdr_record_fru_locator*>(rec)) );
                    frus[ reinterpret_cast<::sdr_record_fru_locator*>(rec)->entity ] = entities.size();
                    break;
                default:
                    printf("ignoring sensor type 0x%0X\n", header->type);
                    break;
            }
            ::free(rec);
        }
    }

    for (auto& sensor: sensors) {
        auto fru = frus.find(sensor.second);
        if (fru != frus.end()) {
            entities[sensor.first].name.insert(0, entities[fru->second].name + ":");
        } else {
            // TODO: logical unit? check Entity Association record
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
    auto entity = extractSensorInfo(reinterpret_cast<::sdr_record_common_sensor*>(sdr));

    size_t idlen = std::min(sizeof(sdr->id_string), (size_t)(sdr->id_code & 0x1f));
    entity.description.assign((const char *)sdr->id_string, idlen);
    entity.addrspec = std::to_string(sdr->cmn.keys.owner_id) + ":" + std::to_string(sdr->cmn.keys.lun) + ":" + std::to_string(sdr->cmn.keys.sensor_num);

    double value = 0.0;
    auto *sr = ::ipmi_sdr_read_sensor_value(m_intf, &sdr->cmn, SDR_RECORD_TYPE_FULL_SENSOR, 3);
    if (sr && sr->s_reading_valid && sr->s_has_analog_value)
        value = sr->s_a_val;

    entity.properties.push_back("VAL", value);

    // swap for 1/x conversions, cf. section 36.5 of IPMI specification
    bool swap_hi_lo = (sdr->linearization == SDR_SENSOR_L_1_X);

    // Operating range
    if (swap_hi_lo) {
        entity.properties.push_back("LOPR", ::sdr_convert_sensor_reading(sdr, sdr->sensor_max));
        entity.properties.push_back("HOPR", ::sdr_convert_sensor_reading(sdr, sdr->sensor_min));
    } else {
        entity.properties.push_back("LOPR", ::sdr_convert_sensor_reading(sdr, sdr->sensor_min));
        entity.properties.push_back("HOPR", ::sdr_convert_sensor_reading(sdr, sdr->sensor_max));
    }

    // Limits
    if (sdr->cmn.mask.type.threshold.read.ucr) {
        double ucr = ::sdr_convert_sensor_reading(sdr, sdr->threshold.upper.critical);
        entity.properties.push_back(swap_hi_lo ? "LOLO" : "HIHI", ucr);
    }
    if (sdr->cmn.mask.type.threshold.read.lcr) {
        double lcr = ::sdr_convert_sensor_reading(sdr, sdr->threshold.lower.critical);
        entity.properties.push_back(swap_hi_lo ? "HIHI" : "LOLO", lcr);
    }
    if (sdr->cmn.mask.type.threshold.read.unc) {
        double unc = ::sdr_convert_sensor_reading(sdr, sdr->threshold.upper.non_critical);
        entity.properties.push_back(swap_hi_lo ? "LOW" : "HIGH", unc);
    }
    if (sdr->cmn.mask.type.threshold.read.lnc) {
        double lnc = ::sdr_convert_sensor_reading(sdr, sdr->threshold.lower.non_critical);
        entity.properties.push_back(swap_hi_lo ? "HIGH" : "LOW", lnc);
    }

    // Hysteresis is given in raw values
    if (sdr->linearization == SDR_SENSOR_L_LINEAR) {
        if ((sdr->cmn.sensor.capabilities.hysteresis == 1) || (sdr->cmn.sensor.capabilities.hysteresis == 2)) {
            bool added = false;
            if (sdr->threshold.hysteresis.positive != 0x0 && sdr->threshold.hysteresis.positive != 0xFF) {
                double hyst = ::sdr_convert_sensor_hysterisis(sdr, sdr->threshold.hysteresis.positive);
                if (hyst != 0.0) {
                    entity.properties.push_back("HYST", hyst);
                    added = true;
                }
            }
            if (sdr->threshold.hysteresis.negative != 0x0 && sdr->threshold.hysteresis.negative != 0xFF && !added) {
                double hyst = ::sdr_convert_sensor_hysterisis(sdr, sdr->threshold.hysteresis.negative);
                if (hyst != 0.0) {
                    entity.properties.push_back("HYST", hyst);
                }
            }
        }
    }

    return entity;
}

EntityInfo IpmiToolProvider::extractSensorInfo(::sdr_record_compact_sensor* sdr)
{
    auto entity = extractSensorInfo(reinterpret_cast<::sdr_record_common_sensor*>(sdr));

    size_t idlen = std::min(sizeof(sdr->id_string), (size_t)(sdr->id_code & 0x1f));
    entity.description.assign((const char *)sdr->id_string, idlen);

    if (sdr->cmn.unit.analog != 3) {
        int value = 0;
        auto *sr = ::ipmi_sdr_read_sensor_value(m_intf, &sdr->cmn, SDR_RECORD_TYPE_COMPACT_SENSOR, 3);
        if (sr && sr->s_reading_valid && !sr->s_has_analog_value)
            value = sr->s_reading;

        entity.properties.push_back("VAL", value);
    } else {
        double value = 0.0;
        auto *sr = ::ipmi_sdr_read_sensor_value(m_intf, &sdr->cmn, SDR_RECORD_TYPE_COMPACT_SENSOR, 3);
        if (sr && sr->s_reading_valid && sr->s_has_analog_value)
            value = sr->s_a_val;

        entity.properties.push_back("VAL", value);

        if (sdr->threshold.hysteresis.positive != 0x0 && sdr->threshold.hysteresis.positive != 0xFF) {
            entity.properties.push_back("HYST", sdr->threshold.hysteresis.positive);
        } else if (sdr->threshold.hysteresis.negative != 0x0 && sdr->threshold.hysteresis.negative != 0xFF) {
            entity.properties.push_back("HYST", sdr->threshold.hysteresis.negative);
        }
    }

    return entity;
}

EntityInfo IpmiToolProvider::extractSensorInfo(::sdr_record_common_sensor* sdr)
{
    EntityInfo entity;
    entity.type = EntityInfo::Type::ANALOG_SENSOR;
    entity.name = "Sensor" + std::to_string((sdr->keys.lun << 8) | sdr->keys.sensor_num);

/*
    // Build a menu
    if (sdr->event_type == 0x6F) {
        for (auto event = ::sensor_specific_event_types; event->desc != NULL; event++) {
            if (event->code == sdr->sensor.type)
                entity.options.emplace_back( std::make_pair(event->offset, event->desc) );
        }
    } else {
        for (auto event = ::generic_event_types; event->desc != NULL; event++) {
            if (event->code == sdr->event_type)
                entity.options.emplace_back( std::make_pair(event->offset, event->desc) );
        }
    }
*/
    // Units
    if (sdr->unit.modifier > 0) {
        std::string unit = ::ipmi_sdr_get_unit_string(sdr->unit.pct, sdr->unit.modifier, sdr->unit.type.base, sdr->unit.type.modifier);
        entity.properties.push_back("UNIT", unit);
    }

    return entity;
}

EntityInfo IpmiToolProvider::extractFruInfo(::sdr_record_fru_locator* fru)
{
    EntityInfo entity;
    entity.type = EntityInfo::Type::FRU;
    entity.name = "Fru" + std::to_string(fru->device_id);
    entity.addrspec = std::to_string(fru->device_id);

    size_t idlen = std::min(sizeof(fru->id_string), (size_t)(fru->id_code & 0x1f));
    entity.description.assign((const char *)fru->id_string, idlen);

    getFruChassisProperties(*fru, entity.properties);
    getFruBoardProperties(*fru, entity.properties);
    getFruProductProperties(*fru, entity.properties);

    return entity;
}

uint32_t IpmiToolProvider::getFruAreaOffset(uint8_t device_id, struct ::fru_info &fruinfo, uint8_t area)
{
    struct ::ipmi_rq req;
    struct ::ipmi_rs *rsp;
    uint8_t msg_data[4] = { device_id, 0, 0, 8 };

    // Retrieve FRU information (size and offset unit)
    memset(&req, 0, sizeof(req));
    req.msg.netfn = IPMI_NETFN_STORAGE;
    req.msg.cmd = GET_FRU_INFO;
    req.msg.data = msg_data;
    req.msg.data_len = 1;

    rsp = m_intf->sendrecv(m_intf, &req);
    if (!rsp || rsp->ccode)
        return 0;

    memset(&fruinfo, 0, sizeof(fruinfo));
    fruinfo.size = (rsp->data[1] << 8) | rsp->data[0];
    fruinfo.access = (rsp->data[2] & 0x1); // 0=byte access, 1=word access

    if (fruinfo.size == 0)
        return 0;

    // Read area offsets which are at address 0x0
    memset(&req, 0, sizeof(req));
    req.msg.netfn = IPMI_NETFN_STORAGE;
    req.msg.cmd = GET_FRU_DATA;
    req.msg.data = msg_data;
    req.msg.data_len = 4;

    rsp = m_intf->sendrecv(m_intf, &req);
    if (!rsp || rsp->ccode)
        return 0;

    struct ::fru_header *header = reinterpret_cast<struct ::fru_header *>(rsp->data + 1);
    if (header->version != 1)
        return 0;

    assert(area < 5);
    return header->offsets[area] * 8;
}

bool IpmiToolProvider::getFruChassisProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties)
{
    uint8_t tmp[2] = { 0 };
    struct ::fru_info fruinfo;
    uint32_t offset = getFruAreaOffset(fru.device_id, fruinfo, 1);

    if (offset < sizeof(fru_header))
        return false;

    if (::read_fru_area(m_intf, &fruinfo, fru.device_id, offset, 2, tmp) != 0 || tmp[1] == 0)
        return false;

    size_t area_len = 8 * tmp[1];

    std::vector<uint8_t> buffer(area_len, 0);
    if (::read_fru_area(m_intf, &fruinfo, fru.device_id, offset, area_len, buffer.data()) != 0)
        return false;

    auto chassis_type = ::chassis_type_desc[ (buffer[2] > ARRAY_SIZE(::chassis_type_desc) - 1 ? 2 : buffer[2]) ];
    properties.push_back("Chassis:Type", chassis_type);

    uint32_t i = 3;
    auto chassis_part = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (chassis_part && strlen(chassis_part.get()))
        properties.push_back("Chassis:Part", chassis_part.get());

    auto chassis_serial = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (chassis_serial && strlen(chassis_serial.get()))
        properties.push_back("Chassis:Serial", chassis_serial.get());

    return true;
}

bool IpmiToolProvider::getFruBoardProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties)
{
    uint8_t tmp[2] = { 0 };
    struct ::fru_info fruinfo;
    uint32_t offset = getFruAreaOffset(fru.device_id, fruinfo, 2);

    if (offset < sizeof(fru_header))
        return false;

    if (::read_fru_area(m_intf, &fruinfo, fru.device_id, offset, 2, tmp) != 0 || tmp[1] == 0)
        return false;

    size_t area_len = 8 * tmp[1];

    std::vector<uint8_t> buffer(area_len, 0);
    if (::read_fru_area(m_intf, &fruinfo, fru.device_id, offset, area_len, buffer.data()) != 0)
        return false;

    /*
     * skip first three bytes which specify
     * fru area version, fru area length
     * and fru board language
     */
    uint32_t i = 3;

    time_t tval = 60 * ((buffer[i+2] << 16) + (buffer[i+1] << 8) + (buffer[i])) + ::secs_from_1970_1996;
    std::string board_date( ctime(&tval) );
    board_date.pop_back(); // remove \n
    properties.push_back("Board:Date", board_date);

    i += 3;
    auto board_manuf = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (board_manuf && strlen(board_manuf.get()))
        properties.push_back("Board:Manuf", board_manuf.get());

    auto board_product = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (board_product && strlen(board_product.get()))
        properties.push_back("Board:Product", board_product.get());

    auto board_serial = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (board_serial && strlen(board_serial.get()))
        properties.push_back("Board:Serial", board_serial.get());

    auto board_part = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (board_part && strlen(board_part.get()))
        properties.push_back("Board:Part", board_part.get());

    auto board_fruid = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (board_fruid && strlen(board_fruid.get()))
        properties.push_back("Board:FruID", board_fruid.get());

    return true;
}

bool IpmiToolProvider::getFruProductProperties(::sdr_record_fru_locator& fru, EntityInfo::Properties &properties)
{
    uint8_t tmp[2] = { 0 };
    struct ::fru_info fruinfo;
    uint32_t offset = getFruAreaOffset(fru.device_id, fruinfo, 3);

    if (offset < sizeof(fru_header))
        return false;

    if (::read_fru_area(m_intf, &fruinfo, fru.device_id, offset, 2, tmp) != 0 || tmp[1] == 0)
        return false;

    size_t area_len = 8 * tmp[1];

    std::vector<uint8_t> buffer(area_len, 0);
    if (::read_fru_area(m_intf, &fruinfo, fru.device_id, offset, area_len, buffer.data()) != 0)
        return false;

    /*
     * skip first three bytes which specify
     * fru area version, fru area length
     * and fru board language
     */
    uint32_t i = 3;

    auto product_manuf = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (product_manuf && strlen(product_manuf.get()))
        properties.push_back("Product:Manuf", product_manuf.get());

    auto product_name = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (product_name && strlen(product_name.get()))
        properties.push_back("Product:Name", product_name.get());

    auto product_part = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (product_part && strlen(product_part.get()))
        properties.push_back("Product:Part", product_part.get());

    auto product_ver = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (product_ver && strlen(product_ver.get()))
        properties.push_back("Product:Version", product_ver.get());

    auto product_serial = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (product_serial && strlen(product_serial.get()))
        properties.push_back("Product:Serial", product_serial.get());

    auto product_asset = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (product_asset && strlen(product_asset.get()))
        properties.push_back("Product:Asset", product_asset.get());

    auto product_fruid = std::unique_ptr<char>(::get_fru_area_str(buffer.data(), &i));
    if (product_fruid && strlen(product_fruid.get()))
        properties.push_back("Product:FruID", product_fruid.get());

    return true;
}

BaseProvider::ReturnCode IpmiToolProvider::getSensor(const std::string& addrspec, double& val, double& low, double& lolo, double& high, double& hihi, int16_t& prec, double& hyst)
{
    static std::regex re("^([0-9]+):([0-9]+):([0-9]+)$");
    std::smatch m;
    if (!std::regex_search(addrspec, m, re) || m.empty())
        return BAD_ADDRESS;

    uint8_t owner_id = std::stoul(m[1]);
    uint8_t lun = std::stoul(m[2]);
    uint8_t sensor_num = std::stoul(m[3]);

    if (!m_intf)
        return NOT_CONNECTED;

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

            if (header->type == SDR_RECORD_TYPE_FULL_SENSOR) {
                auto sdr = reinterpret_cast<::sdr_record_full_sensor*>(rec);
                if (sdr->cmn.keys.owner_id   == owner_id &&
                    sdr->cmn.keys.lun        == lun &&
                    sdr->cmn.keys.sensor_num == sensor_num) {

                    auto sensor = extractSensorInfo(sdr);
                    ::free(rec);

                    val  = sensor.properties.find("VAL")->value.dval;
                    low  = sensor.properties.find("LOW")->value.dval;
                    lolo = sensor.properties.find("LOLO")->value.dval;
                    high = sensor.properties.find("HIGH")->value.dval;
                    hihi = sensor.properties.find("HIHI")->value.dval;
                    hyst = sensor.properties.find("HYST")->value.dval;
                    return SUCCESS;
                }
            }
            ::free(rec);
        }
    }

    return NOT_FOUND;
}

BaseProvider::ReturnCode IpmiToolProvider::getFruProperties(const std::string& addrspec, EntityInfo::Properties& properties)
{
    properties.clear();

    static std::regex re("^([0-9]+)$");
    std::smatch m;
    if (!std::regex_search(addrspec, m, re) || m.empty())
        return BAD_ADDRESS;

    unsigned device_id = std::stoul(m[1]);

    if (!m_intf)
        return NOT_CONNECTED;

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

            if (header->type == SDR_RECORD_TYPE_FRU_DEVICE_LOCATOR) {
                auto fru = reinterpret_cast<::sdr_record_fru_locator*>(rec);
                if (fru->device_id == device_id) {
                    properties = extractFruInfo(fru).properties;
                    ::free(rec);
                    return SUCCESS;
                }
            }
            ::free(rec);
        }
    }

    return NOT_FOUND;
}

} // namespace provider
} // namespace epicsipmi

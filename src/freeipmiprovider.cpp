/* freeipmiprovider.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Oct 2018
 */

#include "common.h"
#include "freeipmiprovider.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include <alarm.h>

FreeIpmiProvider::FreeIpmiProvider(const std::string& conn_id, const std::string& hostname,
                                   const std::string& username, const std::string& password,
                                   const std::string& authtype, const std::string& protocol,
                                   const std::string& privlevel)
    : Provider(conn_id)
    , m_hostname(hostname)
    , m_username(username)
    , m_password(password)
    , m_protocol(protocol)
{
    if (authtype == "none" || username.empty())
        m_authType = IPMI_AUTHENTICATION_TYPE_NONE;
    else if (authtype == "plain" || authtype == "straight_password_key")
        m_authType = IPMI_AUTHENTICATION_TYPE_STRAIGHT_PASSWORD_KEY;
    else if (authtype == "md2")
        m_authType = IPMI_AUTHENTICATION_TYPE_MD2;
    else if (authtype == "md5")
        m_authType = IPMI_AUTHENTICATION_TYPE_MD5;
    else
        throw std::runtime_error("invalid authentication type (choose from none,plain,md2,md5)");

    if (privlevel == "admin")
        m_privLevel = IPMI_PRIVILEGE_LEVEL_ADMIN;
    else if (privlevel == "operator")
        m_privLevel = IPMI_PRIVILEGE_LEVEL_OPERATOR;
    else if (privlevel == "user")
        m_privLevel = IPMI_PRIVILEGE_LEVEL_USER;
    else
        throw std::runtime_error("invalid privilege level (choose from user,operator,admin)");

    m_ctx.ipmi = ipmi_ctx_create();
    if (!m_ctx.ipmi)
        throw std::runtime_error("can't create IPMI context");

    m_ctx.sdr = ipmi_sdr_ctx_create();
    if (!m_ctx.sdr)
        throw std::runtime_error("can't create IPMI SDR context");

    // TODO: parametrize
    m_sdrCachePath = "/tmp/ipmi_sdr_" + conn_id + ".cache";

    // TODO: automatic connection management
    connect();
}

FreeIpmiProvider::~FreeIpmiProvider()
{
    if (m_ctx.ipmi) {
        ipmi_ctx_close(m_ctx.ipmi);
        ipmi_ctx_destroy(m_ctx.ipmi);
    }
    if (m_ctx.sdr) {
        ipmi_sdr_ctx_destroy(m_ctx.sdr);
    }
    if (m_ctx.sensors) {
        ipmi_sensor_read_ctx_destroy(m_ctx.sensors);
    }
    if (m_ctx.fru) {
        ipmi_fru_ctx_destroy(m_ctx.fru);
    }
}

void FreeIpmiProvider::connect()
{
    const char* username_ = (m_username.empty() ? nullptr : m_username.c_str());
    const char* password_ = (m_password.empty() ? nullptr : m_password.c_str());

    int connected;
    if (m_protocol == "lan_2.0") {
        connected = ipmi_ctx_open_outofband_2_0(
                        m_ctx.ipmi, m_hostname.c_str(), username_, password_,
                        m_k_g, m_k_g_len, m_privLevel, m_cipherSuiteId,
                        m_sessionTimeout, m_retransmissionTimeout, m_workaroundFlags, m_flags);
    } else {
        connected = ipmi_ctx_open_outofband(
                        m_ctx.ipmi, m_hostname.c_str(), username_, password_,
                        m_authType, m_privLevel,
                        m_sessionTimeout, m_retransmissionTimeout, m_workaroundFlags, m_flags);
    }
    if (connected < 0)
        throw std::runtime_error("can't connect - " + std::string(ipmi_ctx_errormsg(m_ctx.ipmi)));

    openSdrCache();

    if (m_ctx.sensors)
        ipmi_sensor_read_ctx_destroy(m_ctx.sensors);
    m_ctx.sensors = ipmi_sensor_read_ctx_create(m_ctx.ipmi);
    if (!m_ctx.sensors)
        throw std::runtime_error("can't create IPMI sensor context");

    if (m_ctx.fru)
        ipmi_fru_ctx_destroy(m_ctx.fru);
    m_ctx.fru = ipmi_fru_ctx_create(m_ctx.ipmi);
    if (!m_ctx.fru)
        throw std::runtime_error("can't create IPMI FRU context");

    int sensorReadFlags = 0;
    sensorReadFlags |= IPMI_SENSOR_READ_FLAGS_BRIDGE_SENSORS;
    /* Don't error out, if this fails we can still continue */
    if (ipmi_sensor_read_ctx_set_flags(m_ctx.sensors, sensorReadFlags) < 0)
        LOG_WARN("can't set sensor read flags - %s", ipmi_sensor_read_ctx_errormsg(m_ctx.sensors));
}

void FreeIpmiProvider::openSdrCache()
{
    if (ipmi_sdr_cache_open(m_ctx.sdr, m_ctx.ipmi, m_sdrCachePath.c_str()) < 0) {
        switch (ipmi_sdr_ctx_errnum(m_ctx.sdr)) {
        case IPMI_SDR_ERR_CACHE_OUT_OF_DATE:
        case IPMI_SDR_ERR_CACHE_INVALID:
            LOG_INFO("deleting out of date or invalid SDR cache file " + m_sdrCachePath);
            (void)ipmi_sdr_cache_delete(m_ctx.sdr, m_sdrCachePath.c_str());
            // fall thru
        case IPMI_SDR_ERR_CACHE_READ_CACHE_DOES_NOT_EXIST:
            LOG_INFO("creating new SDR cache file " + m_sdrCachePath);
            (void)ipmi_sdr_cache_create(m_ctx.sdr, m_ctx.ipmi, m_sdrCachePath.c_str(), IPMI_SDR_CACHE_CREATE_FLAGS_DEFAULT, nullptr, nullptr);
            break;
        default:
            throw std::runtime_error("can't open SDR cache - " + std::string(ipmi_ctx_errormsg(m_ctx.ipmi)));
        }

        if (ipmi_sdr_cache_open(m_ctx.sdr, m_ctx.ipmi, m_sdrCachePath.c_str()) < 0)
            throw std::runtime_error("can't open SDR cache - " + std::string(ipmi_ctx_errormsg(m_ctx.ipmi)));
    }
}

std::vector<FreeIpmiProvider::Entity> FreeIpmiProvider::getSensors()
{
    std::vector<Entity> v;

    if (ipmi_sdr_cache_first(m_ctx.sdr) < 0)
        throw std::runtime_error("failed to rewind SDR cache - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));

    uint16_t recordCount;
    if (ipmi_sdr_cache_record_count(m_ctx.sdr, &recordCount) < 0)
        throw std::runtime_error("failed to get number of SDR records - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));

    auto sensorReadCtx = ipmi_sensor_read_ctx_create(m_ctx.ipmi);
    if (sensorReadCtx == nullptr)
        throw std::runtime_error("failed to create sensor read context - " + std::string(ipmi_ctx_errormsg(m_ctx.ipmi)));

    for (auto i=recordCount; i > 0; i--, ipmi_sdr_cache_next(m_ctx.sdr)) {
        uint8_t recordType;
        if (ipmi_sdr_parse_record_id_and_type(m_ctx.sdr, NULL, 0, NULL, &recordType) < 0) {
            LOG_WARN("Failed to parse SDR record type - %s, skipping", ipmi_sdr_ctx_errormsg(m_ctx.sdr));
            continue;
        }

        if (recordType == IPMI_SDR_FORMAT_FULL_SENSOR_RECORD || recordType == IPMI_SDR_FORMAT_COMPACT_SENSOR_RECORD) {
            SdrRecord record;
            record.size = ipmi_sdr_cache_record_read(m_ctx.sdr, record.data, IPMI_SDR_MAX_RECORD_LENGTH);
            if (record.size < 0) {
                LOG_DEBUG("Failed to read SDR record - %s, skipping", ipmi_sdr_ctx_errormsg(m_ctx.sdr));
                continue;
            }

            try {
                auto sensor = getSensor(record);
                v.emplace_back(std::move(sensor));
            } catch (std::runtime_error e) {
                LOG_DEBUG(std::string(e.what()) + ", skipping");
            }
        }
    }

    ipmi_sensor_read_ctx_destroy(sensorReadCtx);

    return v;
}

FreeIpmiProvider::Entity FreeIpmiProvider::getEntity(const std::string& address)
{
    auto tokens = common::split(address, ' ', 1);
    if (tokens.empty())
        throw Provider::syntax_error("Invalid address '" + address + "'");

    auto type = tokens.front();
    tokens.erase(tokens.begin());

    if (type == "SENSOR") {
        return getSensor(SensorAddress(tokens.front()));
    } else {
        throw Provider::syntax_error("Invalid address '" + address + "'");
    }
}

std::vector<FreeIpmiProvider::Entity> FreeIpmiProvider::getFrus()
{
    std::vector<Entity> v;

    if (ipmi_sdr_cache_first(m_ctx.sdr) < 0)
        throw std::runtime_error("failed to rewind SDR cache - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));

    uint16_t recordCount;
    if (ipmi_sdr_cache_record_count(m_ctx.sdr, &recordCount) < 0)
        throw std::runtime_error("failed to get number of SDR records - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));

    for (auto i=recordCount; i > 0; i--, ipmi_sdr_cache_next(m_ctx.sdr)) {
        uint8_t recordType;
        if (ipmi_sdr_parse_record_id_and_type(m_ctx.sdr, NULL, 0, NULL, &recordType) < 0) {
            LOG_WARN("Failed to parse SDR record type - %s, skipping", ipmi_sdr_ctx_errormsg(m_ctx.sdr));
            continue;
        }

        if (recordType == IPMI_SDR_FORMAT_FRU_DEVICE_LOCATOR_RECORD) {
            SdrRecord record;
            record.size = ipmi_sdr_cache_record_read(m_ctx.sdr, record.data, IPMI_SDR_MAX_RECORD_LENGTH);
            if (record.size < 0) {
                LOG_DEBUG("Failed to read SDR record - %s, skipping", ipmi_sdr_ctx_errormsg(m_ctx.sdr));
                continue;
            }

            try {
                getFru(record, v);
//                v.emplace_back(sensor);
            } catch (std::runtime_error e) {
                LOG_DEBUG(std::string(e.what()) + ", skipping");
            }
        }
    }

    return v;
}

FreeIpmiProvider::Entity FreeIpmiProvider::getSensor(const SensorAddress& address)
{
    if (ipmi_sdr_cache_first(m_ctx.sdr) < 0)
        throw Provider::process_error("failed to rewind SDR cache - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));

    uint16_t recordCount;
    if (ipmi_sdr_cache_record_count(m_ctx.sdr, &recordCount) < 0)
        throw Provider::process_error("failed to get number of SDR records - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));

    SdrRecord record;
    for (auto i=recordCount; i > 0; i--, ipmi_sdr_cache_next(m_ctx.sdr)) {
        uint8_t recordType;
        if (ipmi_sdr_parse_record_id_and_type(m_ctx.sdr, NULL, 0, NULL, &recordType) < 0)
            continue;

        if (recordType != IPMI_SDR_FORMAT_FULL_SENSOR_RECORD && recordType != IPMI_SDR_FORMAT_COMPACT_SENSOR_RECORD)
            continue;

        record.size = ipmi_sdr_cache_record_read(m_ctx.sdr, record.data, IPMI_SDR_MAX_RECORD_LENGTH);
        if (record.size < 0)
            continue;

        uint8_t ownerId;
        uint8_t ownerIdType;
        if (ipmi_sdr_parse_sensor_owner_id(m_ctx.sdr, record.data, record.size, &ownerIdType, &ownerId) < 0 || ownerId != address.ownerId)
            continue;

        uint8_t ownerLun;
        uint8_t channelNum;
        if (ipmi_sdr_parse_sensor_owner_lun(m_ctx.sdr, record.data, record.size, &ownerLun, &channelNum) < 0 || ownerLun != address.ownerLun)
            continue;

        uint8_t sensorNum;
        if (ipmi_sdr_parse_sensor_number(m_ctx.sdr, record.data, record.size, &sensorNum) < 0 || sensorNum != address.sensorNum)
            continue;

        return getSensor(record);
    }

    throw Provider::comm_error("sensor not found");
}

FreeIpmiProvider::Entity FreeIpmiProvider::getSensor(const SdrRecord& record)
{
    Entity entity;

    // Determine entity type
    uint8_t recordType;
    if (ipmi_sdr_parse_record_id_and_type(m_ctx.sdr, record.data, record.size, NULL, &recordType) < 0) {
        throw std::runtime_error("Failed to parse SDR record type - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));
    }
    if (recordType != IPMI_SDR_FORMAT_FULL_SENSOR_RECORD && recordType != IPMI_SDR_FORMAT_COMPACT_SENSOR_RECORD)
        throw std::runtime_error("SDR record not a sensor, skipping");

    // Creating link constists of record owner ID&LUN and sensor number
    entity["INP"] = getSensorAddress(record);
    entity["EGU"] = getSensorUnits(record);

    // Get sensor name and put it in description field
    uint8_t sensorNum;
    if (ipmi_sdr_parse_sensor_number(m_ctx.sdr, record.data, record.size, &sensorNum) < 0) {
        throw std::runtime_error("Failed to parse SDR record sensor number - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));
    }
    char desc[IPMI_SDR_MAX_SENSOR_NAME_LENGTH];
    int descLen = IPMI_SDR_MAX_SENSOR_NAME_LENGTH;
    if (ipmi_sdr_parse_entity_sensor_name(m_ctx.sdr, record.data, record.size, sensorNum, 0, desc, descLen) < 0) {
        throw std::runtime_error("Failed to parse SDR record sensor long name - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));
    }
    entity["DESC"] = desc;
    char name[IPMI_SDR_MAX_SENSOR_NAME_LENGTH];
    int nameLen = IPMI_SDR_MAX_SENSOR_NAME_LENGTH;
    if (ipmi_sdr_parse_sensor_name(m_ctx.sdr, record.data, record.size, sensorNum, 0, name, nameLen) < 0) {
        throw std::runtime_error("Failed to parse SDR record sensor name - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));
    }
    entity["NAME"] = name;

    auto sensorReadCtx = ipmi_sensor_read_ctx_create(m_ctx.ipmi);
    if (sensorReadCtx == nullptr)
        throw Provider::process_error("failed to create sensor read context - " + std::string(ipmi_ctx_errormsg(m_ctx.ipmi)));

    int sharedOffset = 0; // TODO: shared sensors support
    uint8_t readingRaw = 0;
    double* reading = nullptr;
    uint16_t eventMask = 0;
    if (ipmi_sensor_read(m_ctx.sensors, record.data, record.size, sharedOffset, &readingRaw, &reading, &eventMask) <= 0) {
        entity["SEVR"] = epicsSevInvalid;
        switch (ipmi_sensor_read_ctx_errnum(m_ctx.sensors)) {
            case IPMI_SENSOR_READ_ERR_SENSOR_NON_ANALOG:
            case IPMI_SENSOR_READ_ERR_SENSOR_NON_LINEAR:
                entity["STAT"] = epicsAlarmCalc;
                break;
            case IPMI_SENSOR_READ_ERR_SENSOR_READING_CANNOT_BE_OBTAINED:
            case IPMI_SENSOR_READ_ERR_NODE_BUSY:
                entity["STAT"] = epicsAlarmComm;
                break;
            case IPMI_SENSOR_READ_ERR_SENSOR_IS_SYSTEM_SOFTWARE:
            case IPMI_SENSOR_READ_ERR_SENSOR_READING_UNAVAILABLE:
            case IPMI_SENSOR_READ_ERR_SENSOR_SCANNING_DISABLED:
            case IPMI_SENSOR_READ_ERR_SENSOR_NOT_OWNED_BY_BMC:
            case IPMI_SENSOR_READ_ERR_SENSOR_CANNOT_BE_BRIDGED:
            default:
                entity["STAT"] = epicsAlarmUDF;
                break;
        }

        LOG_DEBUG("Failed to read sensor value - %s, skipping", ipmi_sensor_read_ctx_errormsg(m_ctx.sensors));
    } else {

        uint8_t readingType;
        if (ipmi_sdr_parse_event_reading_type_code(m_ctx.sdr, record.data, record.size, &readingType) < 0) {
            LOG_DEBUG("Failed to read sensor value type - %s, skipping", ipmi_sdr_ctx_errormsg(m_ctx.sdr));

        } else {

            if (reading) {
                // TODO: readingType == IPMI_EVENT_READING_TYPE_CODE_CLASS_GENERIC_DISCRETE ???
                entity["VAL"] = std::round(*reading * 100.0) / 100.0;
            } else {
                entity["VAL"] = 0.0;
                entity["SEVR"] = epicsSevInvalid;
                entity["STAT"] = epicsAlarmCalc;
            }

            if (readingType == IPMI_EVENT_READING_TYPE_CODE_CLASS_THRESHOLD) {
                // TODO: get thresholds, hint: ipmi-sensors-output-common.c:ipmi_sensors_get_thresholds()
                // and create OUT records for driving them
            }
        }
    }

    ipmi_sensor_read_ctx_destroy(sensorReadCtx);

    return entity;
}

void FreeIpmiProvider::getFru(const SdrRecord& record, std::vector<Entity>& frus)
{
    Entity fru;

    // Determine entity type
    uint8_t recordType;
    if (ipmi_sdr_parse_record_id_and_type(m_ctx.sdr, record.data, record.size, NULL, &recordType) < 0) {
        throw std::runtime_error("Failed to parse SDR record type - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));
    }
    if (recordType != IPMI_SDR_FORMAT_FRU_DEVICE_LOCATOR_RECORD)
        throw std::runtime_error("SDR record not a sensor");

    uint8_t deviceId;
    uint8_t fruDevice;
    if (ipmi_sdr_parse_fru_device_locator_parameters(m_ctx.sdr, record.data, record.size, NULL, &deviceId, NULL, NULL, &fruDevice, NULL) < 0)
        throw std::runtime_error("Failed to get SDR FRU device id - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));

    if (fruDevice == 0)
        return;

    char deviceIdStr[IPMI_SDR_MAX_DEVICE_ID_STRING_LENGTH+1] = {0};

    if (ipmi_sdr_parse_device_id_string(m_ctx.sdr, record.data, record.size, deviceIdStr, sizeof(deviceIdStr)-1) < 0)
        throw std::runtime_error("Failed to parse SDR FRU device id - " + std::string(ipmi_sdr_ctx_errormsg(m_ctx.sdr)));

    if (ipmi_fru_open_device_id(m_ctx.fru, deviceId) < 0)
        throw std::runtime_error("Failed to open FRU device - " + std::string(ipmi_fru_ctx_errormsg(m_ctx.fru)));

    if (ipmi_fru_first(m_ctx.fru) < 0)
        throw std::runtime_error("Failed to rewind FRU - " + std::string(ipmi_fru_ctx_errormsg(m_ctx.fru)));

    std::string fruName = deviceIdStr; // Or maybe "Fru" + std::to_string(deviceId)
    do {
        unsigned int areaType = 0;
        FruArea buffer;

        if (ipmi_fru_read_data_area(m_ctx.fru, &areaType, &buffer.size, buffer.data, buffer.max_size-1) < 0) {
            // Silently? skip it
            continue;
        }

        if (buffer.size == 0)
            continue;

        std::vector<Entity> entities;
        switch (areaType) {
        case IPMI_FRU_AREA_TYPE_CHASSIS_INFO_AREA:
            entities = getFruChassisInfo(deviceId, fruName, buffer);
            break;
        case IPMI_FRU_AREA_TYPE_BOARD_INFO_AREA:
            break;
        case IPMI_FRU_AREA_TYPE_PRODUCT_INFO_AREA:
            break;
        case IPMI_FRU_AREA_TYPE_MULTIRECORD_POWER_SUPPLY_INFORMATION:
            break;
        case IPMI_FRU_AREA_TYPE_MULTIRECORD_DC_OUTPUT:
            break;
        case IPMI_FRU_AREA_TYPE_MULTIRECORD_DC_LOAD:
        case IPMI_FRU_AREA_TYPE_MULTIRECORD_EXTENDED_DC_LOAD:
            break;
        case IPMI_FRU_AREA_TYPE_MULTIRECORD_MANAGEMENT_ACCESS_RECORD:
            break;
        case IPMI_FRU_AREA_TYPE_MULTIRECORD_BASE_COMPATABILITY_RECORD:
            break;
        case IPMI_FRU_AREA_TYPE_MULTIRECORD_EXTENDED_COMPATABILITY_RECORD:
            break;
        case IPMI_FRU_AREA_TYPE_MULTIRECORD_OEM:
            break;
        default:
            break;
        }

    } while (ipmi_fru_next(m_ctx.fru) == 1);;
}

std::string FreeIpmiProvider::getSensorAddress(const SdrRecord& record)
{
    SensorAddress address;
    uint8_t ownerIdType;
    uint8_t channelNum;
    if (ipmi_sdr_parse_sensor_owner_id(m_ctx.sdr, record.data, record.size, &ownerIdType, &address.ownerId) < 0) {
        LOG_DEBUG("Failed to parse SDR record owner id - %s, skipping", ipmi_sdr_ctx_errormsg(m_ctx.sdr));
        return "";
    }
    if (ipmi_sdr_parse_sensor_owner_lun(m_ctx.sdr, record.data, record.size, &address.ownerLun, &channelNum) < 0) {
        LOG_DEBUG("Failed to parse SDR record LUN number - %s, skipping", ipmi_sdr_ctx_errormsg(m_ctx.sdr));
        return "";
    }
    if (ipmi_sdr_parse_sensor_number(m_ctx.sdr, record.data, record.size, &address.sensorNum) < 0) {
        LOG_DEBUG("Failed to parse SDR record sensor number - %s, skipping", ipmi_sdr_ctx_errormsg(m_ctx.sdr));
        return "";
    }
    return "SENSOR " + address.get();
}

std::string FreeIpmiProvider::getSensorUnits(const SdrRecord& record)
{
    char units[1024] = {0};
    uint8_t percent = 0;
    uint8_t modifier = 0;
    uint8_t rate = 0;
    uint8_t baseType = 0;
    uint8_t modifierType = 0;
    if (ipmi_sdr_parse_sensor_units(m_ctx.sdr, record.data, record.size, &percent, &modifier, &rate, &baseType, &modifierType) < 0)
        return "";

    if (baseType == 0 || ipmi_sensor_units_string(percent, modifier, rate, baseType, modifierType, units, sizeof(units)-1, 1) <= 0)
        return "";

    return units;
}

std::string FreeIpmiProvider::getFruField(const ipmi_fru_field_t& field)
{
    char strbuf[IPMI_FRU_AREA_STRING_MAX + 1] = {0};
    unsigned int strbuflen = IPMI_FRU_AREA_STRING_MAX;

    if (!field.type_length_field_length)
        return "";

    if (ipmi_fru_type_length_field_to_string(m_ctx.fru,
                                             field.type_length_field,
                                             field.type_length_field_length,
                                             IPMI_FRU_LANGUAGE_CODE_ENGLISH,
                                             strbuf,
                                             &strbuflen) < 0)
        return "";

    return std::string(strbuf);
}

std::vector<FreeIpmiProvider::Entity> FreeIpmiProvider::getFruChassisInfo(uint8_t fruId, const std::string& fruName, const FruArea& fruArea)
{
    std::vector<Entity> entities;

    static const size_t IPMI_FRU_CUSTOM_FIELDS = 64;
    uint8_t type;
    ipmi_fru_field_t partNum;
    ipmi_fru_field_t serialNum;
    ipmi_fru_field_t customFields[IPMI_FRU_CUSTOM_FIELDS];

    if (ipmi_fru_chassis_info_area (m_ctx.fru, fruArea.data, fruArea.size, &type, &partNum, &serialNum, customFields, IPMI_FRU_CUSTOM_FIELDS) < 0)
        throw std::runtime_error("Failed to parse FRU chassis info - " + std::string(ipmi_fru_ctx_errormsg(m_ctx.fru)));
    if (!IPMI_FRU_CHASSIS_TYPE_VALID(type))
        type = IPMI_FRU_CHASSIS_TYPE_UNKNOWN;

    Entity chassis;
    chassis["NAME"] = fruName + " Chassis Type";
    chassis["VAL"] = ipmi_fru_chassis_types[type];
    chassis["INP"] = "FRU " + std::to_string(fruId) + " CHASSIS TYPE";
    entities.emplace_back( std::move(chassis) );

    auto str = getFruField(partNum);
    if (str != "") {
        Entity partnum;
        partnum["NAME"] = fruName + " Chassis PartNum";
        partnum["VAL"] = str;
        partnum["INP"] = "FRU " + std::to_string(fruId) + " CHASSIS PARTNUM";
        entities.emplace_back( std::move(partnum) );
    }

    str = getFruField(serialNum);
    if (str != "") {
        Entity serial;
        serial["NAME"] = fruName + " Chassis Serial";
        serial["VAL"] = str;
        serial["INP"] = "FRU " + std::to_string(fruId) + " CHASSIS SERIAL";
        entities.emplace_back( std::move(serial) );
    }

    for (size_t i = 0; i < IPMI_FRU_CUSTOM_FIELDS; i++) {
        str = getFruField(customFields[i]);
        if (str != "") {
            Entity field;
            field["NAME"] = fruName + " Chassis Field" + std::to_string(i);
            field["VAL"] = str;
            field["INP"] = "FRU " + std::to_string(fruId) + " CHASSIS FIELD" + std::to_string(i);
            entities.emplace_back( std::move(field) );
        }
    }

    return entities;
}

/*
 * ===== SensorAddress implementation =====
 */
FreeIpmiProvider::SensorAddress::SensorAddress(const std::string& address)
{
    auto tokens = common::split(address, ':');
    if (tokens.size() != 3)
        throw Provider::syntax_error("Invalid sensor address");

    try {
        ownerId   = std::stoi(tokens[0]) & 0xFF;
        ownerLun  = std::stoi(tokens[1]) & 0xFF;
        sensorNum = std::stoi(tokens[2]) & 0xFF;
    } catch (std::invalid_argument) {
        throw Provider::syntax_error("Invalid sensor address");
    }
}

FreeIpmiProvider::SensorAddress::SensorAddress(uint8_t ownerId_, uint8_t ownerLun_, uint8_t sensorNum_)
    : ownerId(ownerId_)
    , ownerLun(ownerLun_)
    , sensorNum(sensorNum_)
{}

std::string FreeIpmiProvider::SensorAddress::get()
{
    return std::to_string(ownerId) + ":" + std::to_string(ownerLun) + ":" + std::to_string(sensorNum);
}

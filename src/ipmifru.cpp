/* ipmifru.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Mar 2019
 */

#include "freeipmiprovider.h"


Provider::Entity FreeIpmiProvider::getFru(ipmi_fru_ctx_t fru, const FruAddress& address)
{
    if (ipmi_fru_open_device_id(fru, address.fruId) < 0)
        throw std::runtime_error("Failed to open FRU device - " + std::string(ipmi_fru_ctx_errormsg(fru)));

    if (ipmi_fru_first(fru) < 0) {
        ipmi_fru_close_device_id(fru);
        throw std::runtime_error("Failed to rewind FRU - " + std::string(ipmi_fru_ctx_errormsg(fru)));
    }

    std::string error;
    std::string subarea;
    do {
        unsigned int areaType = 0;
        FruArea buffer;

        if (ipmi_fru_read_data_area(fru, &areaType, &buffer.size, buffer.data, buffer.max_size-1) < 0)
            continue;
        if (buffer.size == 0)
            continue;

        try {
            switch (areaType) {
            case IPMI_FRU_AREA_TYPE_CHASSIS_INFO_AREA:
                if (address.area == "CHASSIS")
                    subarea = getFruChassisSubarea(fru, address.fruId, buffer, address.subarea);
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
        } catch (std::runtime_error e) {
            error = e.what();
            break;
        }

    } while (subarea.empty() && ipmi_fru_next(fru) == 1);

    ipmi_fru_close_device_id(fru);

    if (!error.empty())
        throw Provider::process_error(error);
    if (subarea.empty())
        throw Provider::process_error("FRU area not found");

    Entity entity;
    entity["VAL"] = subarea;
    return entity;
}

std::string FreeIpmiProvider::getFruName(ipmi_sdr_ctx_t sdr, const FreeIpmiProvider::SdrRecord& record)
{
    uint8_t entityId;
    uint8_t entityInstance;
    if (ipmi_sdr_parse_fru_entity_id_and_instance(sdr, record.data, record.size, &entityId, &entityInstance) < 0)
        throw Provider::process_error("Failed to get SDR FRU entity info - " + std::string(ipmi_sdr_ctx_errormsg(sdr)));

    if (IPMI_ENTITY_ID_VALID(entityId))
        return ipmi_entity_ids[entityId];

    if (IPMI_ENTITY_ID_IS_CHASSIS_SPECIFIC(entityId))
        return "Chassis" + std::to_string(entityInstance);

    if (IPMI_ENTITY_ID_IS_BOARD_SET_SPECIFIC(entityId))
        return "Board" + std::to_string(entityInstance);

    if (IPMI_ENTITY_ID_IS_OEM_SYSTEM_INTEGRATOR_DEFINED(entityId))
        return "Oem" + std::to_string(entityInstance);

    return "Entity" + std::to_string(entityInstance);
}

std::vector<FreeIpmiProvider::Entity> FreeIpmiProvider::getFrus(ipmi_sdr_ctx_t sdr, ipmi_fru_ctx_t fru)
{
    std::vector<Entity> entities;

    if (ipmi_sdr_cache_first(sdr) < 0)
        throw std::runtime_error("failed to rewind SDR cache - " + std::string(ipmi_sdr_ctx_errormsg(sdr)));

    uint16_t recordCount;
    if (ipmi_sdr_cache_record_count(sdr, &recordCount) < 0)
        throw std::runtime_error("failed to get number of SDR records - " + std::string(ipmi_sdr_ctx_errormsg(sdr)));

    for (auto i=recordCount; i > 0; i--, ipmi_sdr_cache_next(sdr)) {
        uint8_t recordType;
        if (ipmi_sdr_parse_record_id_and_type(sdr, NULL, 0, NULL, &recordType) < 0) {
            LOG_WARN("Failed to parse SDR record type - %s, skipping", ipmi_sdr_ctx_errormsg(sdr));
            continue;
        }

        if (recordType != IPMI_SDR_FORMAT_FRU_DEVICE_LOCATOR_RECORD)
            continue;

        SdrRecord record;
        record.size = ipmi_sdr_cache_record_read(sdr, record.data, record.max_size);
        if (record.size < 0) {
            LOG_DEBUG("Failed to read SDR record - %s, skipping", ipmi_sdr_ctx_errormsg(sdr));
            continue;
        }

        uint8_t entityId;
        uint8_t entityInstance;
        if (ipmi_sdr_parse_fru_entity_id_and_instance(sdr, record.data, record.size, &entityId, &entityInstance) < 0) {
            LOG_DEBUG("Failed to get SDR FRU entity info - %s", ipmi_sdr_ctx_errormsg(sdr));
            continue;
        }
        std::string fruName = getFruName(sdr, record);

        uint8_t fruId;
        uint8_t fruDevice;
        if (ipmi_sdr_parse_fru_device_locator_parameters(sdr, record.data, record.size, NULL, &fruId, NULL, NULL, &fruDevice, NULL) < 0) {
            LOG_DEBUG("Failed to get SDR FRU device id - %s", ipmi_sdr_ctx_errormsg(sdr));
            continue;
        }

        if (fruDevice == 0)
            continue;

        char deviceName[IPMI_SDR_MAX_DEVICE_ID_STRING_LENGTH+1] = {0};

        if (ipmi_sdr_parse_device_id_string(sdr, record.data, record.size, deviceName, sizeof(deviceName)-1) < 0) {
            LOG_DEBUG("Failed to parse SDR FRU device id - %s", ipmi_sdr_ctx_errormsg(sdr));
            continue;
        }

        try {
            auto tmp = getFruAreas(fru, fruId, deviceName);
            for (auto& e: tmp)
                entities.emplace_back( std::move(e) );
        } catch (std::runtime_error e) {
            LOG_DEBUG(std::string(e.what()) + ", skipping");
        }
    }

    return entities;
}

std::vector<Provider::Entity> FreeIpmiProvider::getFruAreas(ipmi_fru_ctx_t fru, uint8_t fruId, const std::string& deviceName)
{
    if (ipmi_fru_open_device_id(fru, fruId) < 0)
        throw std::runtime_error("Failed to open FRU device - " + std::string(ipmi_fru_ctx_errormsg(fru)));

    if (ipmi_fru_first(fru) < 0) {
        ipmi_fru_close_device_id(fru);
        throw std::runtime_error("Failed to rewind FRU - " + std::string(ipmi_fru_ctx_errormsg(fru)));
    }

    std::vector<Entity> entities;
    do {
        unsigned int areaType = 0;
        FruArea buffer;

        if (ipmi_fru_read_data_area(fru, &areaType, &buffer.size, buffer.data, buffer.max_size-1) < 0) {
            // Silently? skip it
            continue;
        }

        if (buffer.size == 0)
            continue;

        try {
            std::vector<Entity> tmp;
            switch (areaType) {
            case IPMI_FRU_AREA_TYPE_CHASSIS_INFO_AREA:
                tmp = getFruChassis(fru, fruId, deviceName, buffer);
                break;
            case IPMI_FRU_AREA_TYPE_BOARD_INFO_AREA:
                tmp = getFruBoard(fru, fruId, deviceName, buffer);
                break;
            case IPMI_FRU_AREA_TYPE_PRODUCT_INFO_AREA:
                tmp = getFruProduct(fru, fruId, deviceName, buffer);
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

            for (const auto& e: tmp)
                entities.emplace_back( std::move(e) );

        } catch (std::runtime_error e) {
            LOG_DEBUG(std::string(e.what()) + ", skipping");
        }

    } while (ipmi_fru_next(fru) == 1);

    ipmi_fru_close_device_id(fru);

    return entities;
}

std::string FreeIpmiProvider::getFruField(ipmi_fru_ctx_t fru, const ipmi_fru_field_t& field, uint8_t languageCode)
{
    char strbuf[IPMI_FRU_AREA_STRING_MAX + 1] = {0};
    unsigned int strbuflen = IPMI_FRU_AREA_STRING_MAX;

    if (!field.type_length_field_length)
        return "";

    if (ipmi_fru_type_length_field_to_string(fru,
                                             field.type_length_field,
                                             field.type_length_field_length,
                                             languageCode,
                                             strbuf,
                                             &strbuflen) < 0)
        return "";

    return std::string(strbuf);
}

std::vector<Provider::Entity> FreeIpmiProvider::getFruChassis(ipmi_fru_ctx_t fru, uint8_t fruId, const std::string& deviceName, const FruArea& fruArea)
{
    static const size_t IPMI_FRU_CUSTOM_FIELDS = 64;

    std::vector<Entity> entities;
    auto fruName = "Fru" + std::to_string(fruId);

    std::list<std::string> subareas = { "Type", "PartNum", "SerialNum" };
    for (size_t i = 0; i < IPMI_FRU_CUSTOM_FIELDS; i++) {
        subareas.emplace_back( "Field" + std::to_string(i) );
    }

    // Yes this is not the most efficient way of doing it,
    // but getFruChassis() is invoked rarely so we trade performance for cleaner code.
    for (auto& subarea: subareas) {
        try {
            auto SUBAREA = common::to_upper(subarea);
            auto value = getFruChassisSubarea(fru, fruId, fruArea, SUBAREA);
            if (!value.empty()) {
                Entity entity;
                entity["VAL"] = value;
                entity["NAME"] = fruName + " Chassis " + subarea;
                entity["DESC"] = deviceName + " Chassis " + subarea;
                entity["INP"] = "FRU " + std::to_string(fruId) + " CHASSIS " + SUBAREA;
                entities.emplace_back( std::move(entity) );
            }
        } catch (...) {
            // pass
        }
    }
    return entities;
}

std::string FreeIpmiProvider::getFruChassisSubarea(ipmi_fru_ctx_t fru, uint8_t fruId, const FruArea& area, const std::string& subarea)
{
    static const size_t IPMI_FRU_CUSTOM_FIELDS = 64;
    uint8_t type;
    ipmi_fru_field_t partNum;
    ipmi_fru_field_t serialNum;
    ipmi_fru_field_t customFields[IPMI_FRU_CUSTOM_FIELDS];

    if (ipmi_fru_chassis_info_area(fru, area.data, area.size, &type, &partNum, &serialNum, customFields, IPMI_FRU_CUSTOM_FIELDS) < 0)
        throw Provider::process_error("Failed to parse FRU chassis info - " + std::string(ipmi_fru_ctx_errormsg(fru)));
    if (!IPMI_FRU_CHASSIS_TYPE_VALID(type))
        type = IPMI_FRU_CHASSIS_TYPE_UNKNOWN;

    Entity entity;
    if (subarea == "TYPE")      return ipmi_fru_chassis_types[type];
    if (subarea == "PARTNUM")   return getFruField(fru, partNum,   IPMI_FRU_LANGUAGE_CODE_ENGLISH);
    if (subarea == "SERIALNUM") return getFruField(fru, serialNum, IPMI_FRU_LANGUAGE_CODE_ENGLISH);
    if (subarea.find("FIELD") == 0) {
        std::string tmp = subarea;
        tmp.erase(0, 5);
        size_t i = std::stol(tmp);
        if (i < IPMI_FRU_CUSTOM_FIELDS)
            return getFruField(fru, customFields[i], IPMI_FRU_LANGUAGE_CODE_ENGLISH);
    }
    throw Provider::syntax_error("Invalid FRU chassis area " + subarea);
}

std::vector<Provider::Entity> FreeIpmiProvider::getFruBoard(ipmi_fru_ctx_t fru, uint8_t fruId, const std::string& deviceName, const FruArea& fruArea)
{
    static const size_t IPMI_FRU_CUSTOM_FIELDS = 64;

    std::vector<Entity> entities;
    auto fruName = "Fru" + std::to_string(fruId);

    std::list<std::string> subareas = { "DateTime", "Manufacturer", "Product", "PartNum", "SerialNum", "FileId" };
    for (size_t i = 0; i < IPMI_FRU_CUSTOM_FIELDS; i++) {
        subareas.emplace_back( "Field" + std::to_string(i) );
    }

    // Yes this is not the most efficient way of doing it,
    // but getFruChassis() is invoked rarely so we trade performance for cleaner code.
    for (auto& subarea: subareas) {
        try {
            auto SUBAREA = common::to_upper(subarea);
            auto value = getFruBoardSubarea(fru, fruId, fruArea, SUBAREA);
            if (!value.empty()) {
                Entity entity;
                entity["VAL"] = value;
                entity["NAME"] = fruName + " Board " + subarea;
                entity["DESC"] = deviceName + " Board " + subarea;
                entity["INP"] = "FRU " + std::to_string(fruId) + " BOARD " + SUBAREA;
                entities.emplace_back( std::move(entity) );
            }
        } catch (...) {
            // pass
        }
    }
    return entities;
}

std::string FreeIpmiProvider::getFruBoardSubarea(ipmi_fru_ctx_t fru, uint8_t fruId, const FruArea& area, const std::string& subarea)
{
    static const size_t IPMI_FRU_CUSTOM_FIELDS = 64;
    static const size_t IPMI_FRU_BOARD_STR_BUFLEN = 1024;

    uint8_t langCode;
    uint32_t dateTime;
    ipmi_fru_field_t manufacturer;
    ipmi_fru_field_t product;
    ipmi_fru_field_t serialNum;
    ipmi_fru_field_t partNum;
    ipmi_fru_field_t fruFileId;
    ipmi_fru_field_t customFields[IPMI_FRU_CUSTOM_FIELDS];

    if (ipmi_fru_board_info_area(fru, area.data, area.size, &langCode, &dateTime, &manufacturer, &product, &serialNum, &partNum, &fruFileId, customFields, IPMI_FRU_CUSTOM_FIELDS) < 0)
        throw Provider::process_error("Failed to parse FRU chassis info - " + std::string(ipmi_fru_ctx_errormsg(fru)));

    if (subarea == "MANUFACTURER") return getFruField(fru, manufacturer, langCode);
    if (subarea == "PRODUCT")      return getFruField(fru, product,      langCode);
    if (subarea == "SERIALNUM")    return getFruField(fru, serialNum,    langCode);
    if (subarea == "PARTNUM")      return getFruField(fru, partNum,      langCode);
    if (subarea == "FILEID")       return getFruField(fru, fruFileId,    langCode);
    if (subarea == "DATETIME") {
        char buf[IPMI_FRU_BOARD_STR_BUFLEN + 1] = { 0 };
        int flags = IPMI_TIMESTAMP_FLAG_UTC_TO_LOCALTIME | IPMI_TIMESTAMP_FLAG_DEFAULT;
        if (dateTime != IPMI_FRU_MFG_DATE_TIME_UNSPECIFIED)
            return "unspecified";
        if (ipmi_timestamp_string(dateTime, common::getUtcOffset(), flags, "%D - %T", buf, sizeof(buf)-1) < 0)
            return "invalid";
        return buf;
    }
    if (subarea.find("FIELD") == 0) {
        std::string tmp = subarea;
        tmp.erase(0, 5);
        size_t i = std::stol(tmp);
        if (i < IPMI_FRU_CUSTOM_FIELDS)
            return getFruField(fru, customFields[i], langCode);
    }
    throw Provider::syntax_error("Invalid FRU board area " + subarea);
}

std::vector<Provider::Entity> FreeIpmiProvider::getFruProduct(ipmi_fru_ctx_t fru, uint8_t fruId, const std::string& deviceName, const FruArea& fruArea)
{
    static const size_t IPMI_FRU_CUSTOM_FIELDS = 64;

    std::vector<Entity> entities;
    auto fruName = "Fru" + std::to_string(fruId);

    std::list<std::string> subareas = { "Manufacturer", "Product", "Model", "Version", "AssetTag", "SerialNum", "FileId" };
    for (size_t i = 0; i < IPMI_FRU_CUSTOM_FIELDS; i++) {
        subareas.emplace_back( "Field" + std::to_string(i) );
    }

    // Yes this is not the most efficient way of doing it,
    // but getFruChassis() is invoked rarely so we trade performance for cleaner code.
    for (auto& subarea: subareas) {
        try {
            auto SUBAREA = common::to_upper(subarea);
            auto value = getFruProductSubarea(fru, fruId, fruArea, SUBAREA);
            if (!value.empty()) {
                Entity entity;
                entity["VAL"] = value;
                entity["NAME"] = fruName + " Board " + subarea;
                entity["DESC"] = deviceName + " Board " + subarea;
                entity["INP"] = "FRU " + std::to_string(fruId) + " BOARD " + SUBAREA;
                entities.emplace_back( std::move(entity) );
            }
        } catch (...) {
            // pass
        }

    }
    return entities;
}

std::string FreeIpmiProvider::getFruProductSubarea(ipmi_fru_ctx_t fru, uint8_t fruId, const FruArea& area, const std::string& subarea)
{
    static const size_t IPMI_FRU_CUSTOM_FIELDS = 64;

    uint8_t langCode;
    ipmi_fru_field_t manufacturer;
    ipmi_fru_field_t product;
    ipmi_fru_field_t model;
    ipmi_fru_field_t version;
    ipmi_fru_field_t serialNum;
    ipmi_fru_field_t assetTag;
    ipmi_fru_field_t fruFileId;
    ipmi_fru_field_t customFields[IPMI_FRU_CUSTOM_FIELDS];

    if (ipmi_fru_product_info_area(fru, area.data, area.size, &langCode, &manufacturer, &product, &model, &version, &serialNum, &assetTag, &fruFileId, customFields, IPMI_FRU_CUSTOM_FIELDS) < 0)
        throw Provider::process_error("Failed to parse FRU chassis info - " + std::string(ipmi_fru_ctx_errormsg(fru)));

    if (subarea == "MANUFACTURER") return getFruField(fru, manufacturer, langCode);
    if (subarea == "PRODUCT")      return getFruField(fru, product,      langCode);
    if (subarea == "MODEL")        return getFruField(fru, model,        langCode);
    if (subarea == "VERSION")      return getFruField(fru, version,      langCode);
    if (subarea == "SERIALNUM")    return getFruField(fru, serialNum,    langCode);
    if (subarea == "ASSETTAG")     return getFruField(fru, assetTag,     langCode);
    if (subarea == "FILEID")       return getFruField(fru, fruFileId,    langCode);
    if (subarea.find("FIELD") == 0) {
        std::string tmp = subarea;
        tmp.erase(0, 5);
        size_t i = std::stol(tmp);
        if (i < IPMI_FRU_CUSTOM_FIELDS)
            return getFruField(fru, customFields[i], langCode);
    }
    throw Provider::syntax_error("Invalid FRU product area " + subarea);
}

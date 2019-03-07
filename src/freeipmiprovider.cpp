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

#include <cstdio>
#include <stdexcept>

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
    common::ScopedLock lock(m_apiMutex);
    return getSensors(m_ctx.sdr, m_ctx.sensors);
}

FreeIpmiProvider::Entity FreeIpmiProvider::getEntity(const std::string& address)
{
    common::ScopedLock lock(m_apiMutex);

    // First token in address is the entity type, like 'SENSOR', 'FRU' etc.
    // Rest is type specific
    auto tokens = common::split(address, ' ', 1);
    if (tokens.size() != 2)
        throw Provider::syntax_error("Invalid address '" + address + "'");

    auto type = std::move(tokens.at(0));
    auto rest = std::move(tokens.at(1));

    if (type == "SENSOR") {
        return getSensor(m_ctx.sdr, m_ctx.sensors, SensorAddress(rest));
    } else if (type == "FRU") {
        return getFru(m_ctx.fru, FruAddress(rest));
    } else {
        throw Provider::syntax_error("Invalid address '" + address + "'");
    }
}

std::vector<FreeIpmiProvider::Entity> FreeIpmiProvider::getFrus()
{
    common::ScopedLock lock(m_apiMutex);
    return getFrus(m_ctx.sdr, m_ctx.fru);
}

/*
 * ===== SensorAddress implementation =====
 *
 * EPICS record link specification for SENSOR entities
 * @ipmi <conn> SENSOR <owner>:<number>:<instance>
 * Example:
 * @ipmi IPMI1 SENSOR 22:12:1
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

/*
 * ===== FruAddress implementation =====
 *
 * EPICS record link specification for FRU entities
 * @ipmi <conn> FRU <id> <area> [subarea]
 * Example:
 * @ipmi IPMI1 FRU 10 CHASSIS SERIALNUM
 */
FreeIpmiProvider::FruAddress::FruAddress(const std::string& address)
{
    auto tokens = common::split(address, ' ', 3);
    if (tokens.size() < 2)
        throw Provider::syntax_error("Invalid FRU address");

    try {
        fruId   = std::stoi(tokens[0]) & 0xFF;
        area    = tokens[1];
        subarea = (tokens.size() == 3 ? tokens[2] : "");
    } catch (std::invalid_argument) {
        throw Provider::syntax_error("Invalid FRU address");
    }
}

FreeIpmiProvider::FruAddress::FruAddress(uint8_t fruId_, const std::string& area_, const std::string& subarea_)
    : fruId(fruId_)
    , area(area_)
    , subarea(subarea_)
{}

std::string FreeIpmiProvider::FruAddress::get()
{
    std::string addr = std::to_string(fruId) + " " + area;
    if (!subarea.empty())
        addr += " " + subarea;
    return addr;
}

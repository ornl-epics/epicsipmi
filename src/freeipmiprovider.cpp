/* freeipmiprovider.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Oct 2018
 */

#include "freeipmiprovider.h"

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

    // TODO: parametrize
    m_sdrCachePath = "/tmp/ipmi_sdr_" + conn_id + ".cache";

    // TODO: automatic connection management
    connect();
}

FreeIpmiProvider::~FreeIpmiProvider()
{
    if (stopThread() == false)
        LOG_WARN("Processing thread did not stop");

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

    if (m_ctx.sdr) {
        ipmi_sdr_ctx_destroy(m_ctx.sdr);
    }

    if (m_ctx.ipmi) {
        ipmi_ctx_close(m_ctx.ipmi);
        ipmi_ctx_destroy(m_ctx.ipmi);
    }
    m_ctx.ipmi = ipmi_ctx_create();
    if (!m_ctx.ipmi)
        throw std::runtime_error("can't create IPMI context");

    m_ctx.sdr = ipmi_sdr_ctx_create();
    if (!m_ctx.sdr)
        throw std::runtime_error("can't create IPMI SDR context");

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

    m_connected = true;
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
    if (!m_connected)
        connect();
    return getSensors(m_ctx.sdr, m_ctx.sensors);
}

FreeIpmiProvider::Entity FreeIpmiProvider::getEntity(const std::string& address)
{
    common::ScopedLock lock(m_apiMutex);
    if (!m_connected)
        connect();

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
        return getFru(m_ctx.ipmi, m_ctx.sdr, m_ctx.fru, FruAddress(rest));
    } else if (type == "PICMG_LED") {
        return getPicmgLed(m_ctx.ipmi, PicmgLedAddress(rest));
    } else {
        throw Provider::syntax_error("Invalid address '" + address + "'");
    }
}

std::vector<FreeIpmiProvider::Entity> FreeIpmiProvider::getFrus()
{
    common::ScopedLock lock(m_apiMutex);
    if (!m_connected)
        connect();
    return getFrus(m_ctx.ipmi, m_ctx.sdr, m_ctx.fru);
}

bool FreeIpmiProvider::setBridgeConditional(ipmi_ctx_t ipmi, uint8_t channel, uint8_t slaveAddress)
{
    uint8_t channel_;
    uint8_t slaveAddress_;
    if (ipmi_ctx_get_target(ipmi, &channel_, &slaveAddress_) < 0)
        throw Provider::process_error("Failed to get IPMI target address - " + std::string(ipmi_ctx_errormsg(ipmi)));

    if (channel_ == channel && slaveAddress_ == slaveAddress)
        return false;

    if (ipmi_ctx_set_target(ipmi, &channel, &slaveAddress) < 0)
        throw Provider::process_error("Failed to set IPMI target address - " + std::string(ipmi_ctx_errormsg(ipmi)));

    return true;
}

void FreeIpmiProvider::resetBridge(ipmi_ctx_t ipmi)
{
    if (ipmi_ctx_set_target(ipmi, NULL, NULL) < 0)
        throw Provider::process_error("Failed to set IPMI target address - " + std::string(ipmi_ctx_errormsg(ipmi)));
}

std::vector<FreeIpmiProvider::Entity> FreeIpmiProvider::getPicmgLeds()
{
    common::ScopedLock lock(m_apiMutex);
    if (!m_connected)
        connect();
    return getPicmgLeds(m_ctx.ipmi, m_ctx.sdr);
}

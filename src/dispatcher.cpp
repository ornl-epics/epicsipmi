/* dispatcher.cpp
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
#include "print.h"
#include "dispatcher.h"

#include <map>
#include <string>

// EPICS records that we support
#include <aiRecord.h>
#include <stringinRecord.h>

namespace dispatcher {

static std::map<std::string, std::shared_ptr<FreeIpmiProvider>> g_connections; //!< Global map of connections.
static epicsMutex g_mutex; //!< Global mutex to protect g_connections.

class ScopedLock {
    private:
        epicsMutex& m_mutex;
    public:
        explicit ScopedLock(epicsMutex &mutex) : m_mutex(mutex) { m_mutex.lock(); }
        ~ScopedLock() { m_mutex.unlock(); }
};

static std::pair<std::string, std::string> _parseLink(const std::string& link)
{
    auto tokens = common::split(link, ' ', 2);
    if (tokens.size() < 3 || tokens[0] != "@ipmi")
        return std::make_pair(std::string(""), std::string(""));

    return std::make_pair(tokens[1], tokens[2]);
}

static std::string _createLink(const std::string& conn_id, const std::string& addr)
{
    return "@ipmi " + conn_id + " " + addr;
}

static std::shared_ptr<FreeIpmiProvider> _getConnection(const std::string& conn_id)
{
    ScopedLock lock(g_mutex);
    auto it = g_connections.find(conn_id);
    if (it != g_connections.end())
        return it->second;
    return nullptr;
}

bool connect(const std::string& conn_id, const std::string& hostname,
             const std::string& username, const std::string& password,
             const std::string& authtype, const std::string& protocol,
             const std::string& privlevel)
{
    ScopedLock lock(g_mutex);

    if (g_connections.find(conn_id) != g_connections.end())
        return false;

    std::shared_ptr<FreeIpmiProvider> conn;
    try {
        conn.reset(new FreeIpmiProvider(conn_id, hostname, username, password, authtype, protocol, privlevel));
    } catch (std::bad_alloc& e) {
        LOG_ERROR("can't allocate FreeIPMI provider\n");
        return false;
    } catch (std::runtime_error& e) {
        if (username.empty())
            LOG_ERROR("can't connect to %s - %s", hostname.c_str(), e.what());
        else
            LOG_ERROR("can't connect to %s as user %s - %s", hostname.c_str(), username.c_str(), e.what());
        return false;
    }

    g_connections[conn_id] = conn;
    return true;
}

void scan(const std::string& conn_id, const std::vector<EntityType>& types)
{
    g_mutex.lock();
    auto it = g_connections.find(conn_id);
    bool found = (it != g_connections.end());
    g_mutex.unlock();

    if (!found) {
        LOG_ERROR("no such connection " + conn_id);
        return;
    }

    auto conn = it->second;
    for (auto& type: types) {
        try {
            std::vector<Provider::Entity> entities;
            std::string header;
            if (type == EntityType::SENSOR) {
                entities = conn->getSensors();
                header = "Sensors:";
            } else if (type == EntityType::FRU) {
                entities = conn->getFrus();
                header = "FRUs:";
            }
            print::printScanReport(header, entities);
        } catch (std::runtime_error& e) {
            LOG_ERROR(e.what());
        }
    }
}

void printDb(const std::string& conn_id, const std::string& path, const std::string& pv_prefix)
{
    g_mutex.lock();
    auto it = g_connections.find(conn_id);
    bool found = (it != g_connections.end());
    g_mutex.unlock();

    if (!found) {
        LOG_ERROR("no such connection " + conn_id);
        return;
    }

    auto conn = it->second;
    auto sensors = conn->getSensors();
    for (auto& sensor: sensors) {
        try {
            auto inp = std::get<std::string>(sensor["INP"]);
            sensor["INP"] = _createLink(conn_id, inp);
        } catch (std::bad_variant_access) {
            LOG_WARN("Sensor doesn't have INP field, skipping");
        }
    }
    print::printDatabase(path, sensors, pv_prefix);
}

bool checkLink(const std::string& address)
{
    auto conn = _getConnection( _parseLink(address).first );
    return (!!conn);
}

bool scheduleGet(const std::string& address, const std::function<void()>& cb, Provider::Entity& entity)
{
    auto addr = _parseLink(address);
    auto conn = _getConnection(addr.first);
    if (!conn)
        return false;

    return conn->schedule( Provider::Task(std::move(addr.second), cb, entity) );
}

}; // namespace dispatcher

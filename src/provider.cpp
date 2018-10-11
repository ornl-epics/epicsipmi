/* provider.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Oct 2018
 */

#include "provider.h"
#ifdef HAVE_IPMITOOL
#include "ipmitool.h"
#endif // HAVE_IPMITOOL

#include <map>

#include <epicsMutex.h>

namespace epicsipmi {
namespace provider {

// ***************************************************
// ***** Forward declaration of static functions *****
// ***************************************************

/**
 * @brief Returns a pointer to existing connection.
 * @param connection_id unique connection id
 * @return Smart pointer or empty.
 */
static std::shared_ptr<BaseProvider> getConnection(const std::string& conn_id);

// ***************************************************
// ***** Private definitions                     *****
// ***************************************************

class ScopedLock {
    private:
        epicsMutex &m_mutex;
    public:
        explicit ScopedLock(epicsMutex &mutex) : m_mutex(mutex) { m_mutex.lock(); }
        ~ScopedLock() { m_mutex.unlock(); }
};

// ***************************************************
// ***** Private variables                       *****
// ***************************************************

/**
 * Global mutex to protect g_connections.
 */
static epicsMutex g_mutex;

/**
 * Global map of connections.
 */
static std::map<std::string, std::shared_ptr<BaseProvider>> g_connections;

// ***************************************************
// ***** Functions implementations               *****
// ***************************************************

bool connect(const std::string& conn_id, const std::string& hostname,
             const std::string& username, const std::string& password,
             const std::string& protocol, int privlevel)
{
    ScopedLock lock(g_mutex);

    auto conn = getConnection(conn_id);
    if (conn)
        return false;

    do {
#ifdef HAVE_IPMITOOL
        try {
            conn.reset(new IpmiToolProvider(conn_id));
        } catch (std::bad_alloc& e) {
            // TODO: LOG
        }
        break;
#endif // HAVE_IPMITOOL
    } while (0);


    if (!conn) {
        return false;
    }

    if (!conn->connect(hostname, username, password, protocol, privlevel)) {
        // TODO: LOG
        return false;
    }

    g_connections[conn_id] = conn;
    return true;
}

std::shared_ptr<BaseProvider> getConnection(const std::string& conn_id)
{
    std::shared_ptr<BaseProvider> conn;
    ScopedLock lock(g_mutex);

#ifdef HAVE_IPMITOOL
    auto ipmitoolconn = g_connections.find(conn_id);
    if (ipmitoolconn != g_connections.end()) {
        conn = ipmitoolconn->second;
    }
#endif // HAVE_IPMITOOL

    return conn;
}

std::vector<EntityInfo> scan(const std::string& conn_id)
{
    auto conn = getConnection(conn_id);
    if (!conn)
        return std::vector<EntityInfo>();

    return conn->scan();
}

// ***************************************************
// ***** BaseProvider class functions            *****
// ***************************************************

BaseProvider::BaseProvider(const std::string& conn_id)
: m_connid(conn_id)
{}

std::string BaseProvider::getDeviceName(uint8_t device_id, const std::string& suffix)
{
    std::string name = m_connid + ":" + "Dev" + std::to_string(device_id);
    if (!suffix.empty())
        name += ":" + suffix;
    return name;
}

std::string BaseProvider::getSensorName(uint8_t owner_id, uint8_t lun, uint8_t sensor_num, const std::string& suffix)
{
    uint16_t sensor_id = ((lun & 0x3) << 8) | sensor_num;
    std::string name = m_connid + ":";
    name += "Dev" + std::to_string(owner_id) + ":";
    name += "Sen" + std::to_string(sensor_id);
    if (!suffix.empty())
        name += ":" + suffix;
    return name;
}

// ***************************************************
// ***** Entity class functions                  *****
// ***************************************************

void EntityInfo::Properties::push_back(const std::string& name, int value, const std::string& addrspec)
{
    Property property;
    property.name = name;
    property.addrspec = addrspec;
    property.valtype = Property::ValueType::IVAL;
    property.ival = value;
    property.sval = std::to_string(value);
    std::vector<Property>::push_back(property);
}

void EntityInfo::Properties::push_back(const std::string& name, double value, const std::string& addrspec)
{
    Property property;
    property.name = name;
    property.addrspec = addrspec;
    property.valtype = Property::ValueType::DVAL;
    property.dval = value;
    property.sval = std::to_string(value);
    std::vector<Property>::push_back(property);
}

void EntityInfo::Properties::push_back(const std::string& name, const std::string& value, const std::string& addrspec)
{
    Property property;
    property.name = name;
    property.addrspec = addrspec;
    property.valtype = Property::ValueType::SVAL;
    property.sval = value;
    std::vector<Property>::push_back(property);
}

std::vector<EntityInfo::Property>::const_iterator EntityInfo::Properties::find(const std::string& name) const
{
    auto it = begin();
    for ( ; it != end(); it++) {
        if (it->name == name)
            break;
    }
    return it;
}

} // namespace provider
} // namespace epicsipmi

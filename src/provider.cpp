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
            conn.reset(new IpmiToolProvider);
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

// ***************************************************
// ***** Entity class functions                  *****
// ***************************************************

void EntityInfo::Properties::push_back(const std::string& name, int value)
{
    Property property;
    property.name = name;
    property.value = value;
    std::vector<Property>::push_back(property);
}

void EntityInfo::Properties::push_back(const std::string& name, double value)
{
    Property property;
    property.name = name;
    property.value = value;
    std::vector<Property>::push_back(property);
}

void EntityInfo::Properties::push_back(const std::string& name, const std::string& value)
{
    Property property;
    property.name = name;
    property.value = value;
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

// ***************************************************
// ***** Entity::Property::Value class functions *****
// ***************************************************

EntityInfo::Property::Value& EntityInfo::Property::Value::operator=(int v) {
    type = Type::IVAL;
    ival = v;
    sval = std::to_string(v);
    return *this;
}

EntityInfo::Property::Value& EntityInfo::Property::Value::operator=(double v) {
    type = Type::DVAL;
    dval = v;
    sval = std::to_string(v);
    return *this;
}

EntityInfo::Property::Value& EntityInfo::Property::Value::operator=(const std::string& v) {
    type = Type::SVAL;
    sval = v;
    return *this;
}

EntityInfo::Property::Value& EntityInfo::Property::Value::operator=(const EntityInfo::Property::Value& v) {
    type = v.type;
    ival = v.ival;
    dval = v.dval;
    sval = v.sval;
    return *this;
}

} // namespace provider
} // namespace epicsipmi

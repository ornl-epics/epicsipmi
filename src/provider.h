/* provider.h
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Oct 2018
 */

#pragma once

#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace epicsipmi {
namespace provider {

/**
 * @class EntityInfo
 * @brief Holds common IPMI entity information.
 */
struct EntityInfo {
    enum class Type {
        NONE = 0,
        ANALOG_SENSOR,
        FRU,
    } type = Type::NONE;

    std::string name{""};           //!< Unique entity name based on id
    std::string description{""};    //!< Entity description as defined by IPMI system
    std::string addrspec{""};       //!< Address used to communicate with this entity

    struct Property {
        struct Value {
            enum class Type {
                IVAL,
                DVAL,
                SVAL,
            } type = Type::IVAL;
            int ival = 0;           //!< Integer value
            double dval = 0.0;      //!< Floating point value
            std::string sval = "";  //!< String value, must always be populated even when converting from ival or dval
            Value& operator=(int v);
            Value& operator=(double v);
            Value& operator=(const std::string& v);
            Value& operator=(const Value& v);
        };

        std::string name{""};       //!< Property name (like VAL, HIGH, HIHI, Serial, etc.)
        Value value;
        bool writable{false};
    };
    class Properties : public std::vector<Property> {
        public:
            void push_back(const std::string& name, int value);
            void push_back(const std::string& name, double value);
            void push_back(const std::string& name, const std::string& value);
            std::vector<Property>::const_iterator find(const std::string& name) const;
    } properties;
//    std::vector<Property> properties;
};

typedef std::function<void(bool, EntityInfo::Property::Value&)> Callback;

/**
 * @class BaseProvider
 * @brief An abstract provider class with pure-virtual handles.
 *
 * One provider object is instantiated for each IPMI connection.
 * IPMI provider implementations must derive from this class.
 */
class BaseProvider {
    public:
        enum ReturnCode {
            SUCCESS,
            NOT_FOUND,
            NOT_CONNECTED,
            BAD_ADDRESS,
        };

        /**
         * @brief Establishes connection with IPMI sub-system.
         * @param hostname to connect to
         * @param username credentials to use for connection
         * @param password credentials to use for connection
         * @param protocol to be used
         * @param privlevel privilege level to use for all queries
         * @return true on connection success, false otherwise
         */
        virtual bool connect(const std::string& hostname,
                             const std::string& username, const std::string& password,
                             const std::string& protocol, int privlevel) = 0;

        /**
         * @brief Return a list of all entities currently available.
         * @return A list of entities
         */
        virtual std::vector<EntityInfo> scan() = 0;

        virtual ReturnCode getSensor(const std::string& addrspec, double& val, double& low, double& lolo, double& high, double& hihi, int16_t& prec, double& hyst) { return NOT_FOUND; };

        virtual ReturnCode getFruProperties(const std::string& addrspec, EntityInfo::Properties& properties) { return NOT_FOUND; };
};

/**
 * @brief Establishes connection with IPMI sub-system.
 * @param connection_id unique connection id
 * @param hostname to connect to
 * @param username credentials to use for connection
 * @param password credentials to use for connection
 * @param protocol to be used
 * @param privlevel privilege level to use for all queries
 * @return true on connection success, false otherwise
 *
 * For now only IpmiTool implementation is supported by this function.
 */
bool connect(const std::string& connection_id, const std::string& hostname,
             const std::string& username, const std::string& password,
             const std::string& protocol, int privlevel);

/**
 * @brief Find existing connection by its id.
 * @param conn_id Connection id as specified at connect time.
 * @return Smart pointed to existing connection or invalid pointer if not found.
 */
std::shared_ptr<BaseProvider> getConnection(const std::string& conn_id);

} // namespace provider
} // namespace epicsipmi


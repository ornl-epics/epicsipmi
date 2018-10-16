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

#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsThread.h>

namespace epicsipmi {
namespace provider {

/**
 * @class EntityInfo
 * @brief Holds common IPMI entity information.
 */
struct EntityInfo {
    enum class Type {
        NONE = 0,
        SENSOR,
        DEVICE,
    } type = Type::NONE;

    std::string name{""};           //!< Unique entity name based on id
    std::string description{""};    //!< Entity description as defined by IPMI system

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
        std::string addrspec{""};   //!< IPMI address to access the value, may be empty if not addressable
        Value value;
    };
    class Properties : public std::vector<Property> {
        public:
            void push_back(const std::string& name, int value, const std::string& addrspec="");
            void push_back(const std::string& name, double value, const std::string& addrspec="");
            void push_back(const std::string& name, const std::string& value, const std::string& addrspec="");
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
class BaseProvider : private epicsThreadRunable {
    private:
        epicsMutex m_mutex;
        epicsThread m_thread;
        epicsEvent m_event;
        std::list<std::pair<std::string,Callback>> m_tasks;
        bool m_running = true;
    protected:
        std::string m_connid;
    public:
        /**
         * C'tor
         */
        BaseProvider(const std::string& conn_id);

        /**
         * @brief Destructor stops the thread
         */
        ~BaseProvider();

        /**
         * @brief Thread main function
         */
        void run();

        /**
         * @brief Stop the thread.
         */
        void stop();

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

        /**
         * @brief Schedules retrieving new value
         * @param addrspec address to connect
         * @param cb function to be called uppon completion (or failure)
         * @return true on success, false when no entity found or wrong type
         */
        bool scheduleTask(const std::string& addrspec, const Callback& cb);

        /**
         * @brief Generates EPICS compatible for a given device.
         * @note Uniqueness is not guaranteed.
         * @param device_id Device ID that is supposed to be unique for a given connection.
         *
         * EPICS names are strings up to 63 characters long. They're usually
         * composed of entity ids separated by : character. This function
         * returns a name that can be used as EPICS PV id.
         */
        std::string getDeviceName(uint8_t device_id, const std::string& suffix="");

        /**
         * @brief Generates EPICS compatible for a given sensor.
         * @note Uniqueness is not guaranteed.
         * @param owner_id Owner device ID.
         *
         * EPICS names are strings up to 63 characters long. They're usually
         * composed of entity ids separated by : character. This function
         * returns a name that can be used as EPICS PV id.
         */
        std::string getSensorName(uint8_t owner_id, uint8_t lun, uint8_t sensor_num, const std::string& suffix="");

        /**
         * @brief Retrieve a new value, take as much time as needed.
         * @param addrspec IPMI entity address
         * @param cb callback function to be called upon completion.
         *
         * This function is called from the working thread. Derived classes
         * must implement the details of parsing addrspec and getting a new
         * value from IPMI system.
         */
        virtual bool getValue(const std::string& addrspec, EntityInfo::Property::Value& value) = 0;
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
 * @brief Return a list of all entities currently available from a given connection.
 * @param connection_id
 * @return A list of entities
 */
std::vector<EntityInfo> scan(const std::string& connection_id);

/**
 * @brief Schedule reading a new value.
 * @param addrspec Address of the value to read.
 * @param cb Function to be called uppon completion.
 */
bool scheduleTask(const std::string& addrspec, const Callback& cb);

} // namespace provider
} // namespace epicsipmi


#pragma once

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace epicsipmi {
    /**
     * The provider namespace implements functionality to connect to IPMI systems.
     */
    namespace provider {

        /**
         * @class EntityInfo
         * @brief Holds an IPMI entity information.
         */
        struct EntityInfo {
            enum class Type {
                NONE = 0,
                SENSOR,
            } type = Type::NONE;

            std::string addrspec = "";
            std::string name = "";
            std::string description = "";
            struct {
                bool analog = true;
                std::string units = "";
                std::vector<std::pair<uint8_t,std::string>> options;
                unsigned precision = 0;
                double lopr = std::numeric_limits<double>::min();
                double hopr = std::numeric_limits<double>::max();
                double low  = std::numeric_limits<double>::min();
                double lolo = std::numeric_limits<double>::min();
                double high = std::numeric_limits<double>::max();
                double hihi = std::numeric_limits<double>::max();
                double hyst = 0;
            } sensor;

        };

        /**
         * @class BaseProvider
         * @brief An abstract provider class with pure-virtual handles.
         *
         * One provider object is instantiated for each IPMI connection.
         * IPMI provider implementations must derive from this class.
         */
        class BaseProvider {
            public:
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
                 * @brief Scans for all supported IPMI entities
                 * @param entities found
                 * @return true on success, false on error
                 */
                virtual bool scan(std::vector<EntityInfo>& entities) = 0;

                /**
                 * @brief Queries for integer value from a given IPMI address
                 * @param addrspec address to connect
                 * @param value returned value on success
                 * @return true on success, false when no entity found or wrong type
                 */
                virtual bool getValue(const std::string& addrspec, int& value) { return false; }

                /**
                 * @brief Queries for floating value from a given IPMI address
                 * @param addrspec address to connect
                 * @param value returned value on success
                 * @return true on success, false when no entity found or wrong type
                 */
                virtual bool getValue(const std::string& addrspec, double& value) { return false; }

                /**
                 * @brief Queries for string value from a given IPMI address
                 * @param addrspec address to connect
                 * @param value returned value on success
                 * @return true on success, false when no entity found or wrong type
                 */
                virtual bool getValue(const std::string& addrspec, std::string& value) { return false; }
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
         * @brief Returns a pointer to existing connection.
         * @param connection_id unique connection id
         * @return Smart pointer or empty.
         */
        std::shared_ptr<BaseProvider> getConnection(const std::string& connection_id);

    } // namespace provider
} // namespace epicsipmi


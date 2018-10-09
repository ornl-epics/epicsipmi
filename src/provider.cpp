#include "provider.h"
#ifdef HAVE_IPMITOOL
#include "ipmitool.h"
#endif // HAVE_IPMITOOL

#include <map>

#include <epicsMutex.h>

namespace epicsipmi {

    namespace provider {

        class ScopedLock {
            private:
                epicsMutex &m_mutex;
            public:
                explicit ScopedLock(epicsMutex &mutex) : m_mutex(mutex) { m_mutex.lock(); }
                ~ScopedLock() { m_mutex.unlock(); }
        };

        /**
         * Global mutex to protect g_connections.
         */
        static epicsMutex g_mutex;

        /**
         * Global map of connections.
         */
        static std::map<std::string, std::shared_ptr<BaseProvider>> g_connections;

        bool connect(const std::string& conn_id, const std::string& hostname,
                     const std::string& username, const std::string& password,
                     const std::string& protocol, int privlevel)
        {
            ScopedLock lock(g_mutex);

            std::shared_ptr<BaseProvider> conn = getConnection(conn_id);
            if (conn)
                return false;

            do {
#ifdef HAVE_IPMITOOL
                try {
                    conn.reset(new IpmiToolProvider());
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

    } // namespace provider
} // namespace epicsipmi

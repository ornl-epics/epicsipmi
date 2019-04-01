/* provider.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Feb 2019
 */

#include <common.h>
#include <provider.h>

#include <alarm.h>
#include <epicsThread.h>

#include <limits>

extern "C" {
    static void providerThread(void* ctx)
    {
        reinterpret_cast<Provider*>(ctx)->tasksThread();
    }
};

Provider::Provider(const std::string& conn_id)
{
    epicsThreadCreate(conn_id.c_str(), epicsThreadPriorityLow, epicsThreadStackMedium, (EPICSTHREADFUNC)&providerThread, this);
}

Provider::~Provider()
{
    m_tasks.processing = false;
    m_tasks.event.signal();
    epicsThreadSleep(0.1);
}

bool Provider::schedule(const Task&& task)
{
    m_tasks.mutex.lock();
    m_tasks.queue.emplace_back(task);
    m_tasks.event.signal();
    m_tasks.mutex.unlock();
    return true;
}

void Provider::tasksThread()
{
    while (m_tasks.processing) {
        m_tasks.event.wait();

        m_tasks.mutex.lock();
        if (m_tasks.queue.empty()) {
            m_tasks.mutex.unlock();
            continue;
        }

        Task task = std::move(m_tasks.queue.front());
        m_tasks.queue.pop_front();
        m_tasks.mutex.unlock();

        try {
            auto entity = getEntity(task.address);
            for (auto& kv: entity) {
                task.entity[kv.first] = std::move(kv.second);
            }
        } catch (std::runtime_error& e) {
            task.entity["SEVR"] = (int)epicsAlarmComm;
            task.entity["STAT"] = (int)epicsSevInvalid;
            LOG_ERROR(e.what());
        } catch (...) {
            task.entity["SEVR"] = (int)epicsAlarmComm;
            task.entity["STAT"] = (int)epicsSevInvalid;
            LOG_ERROR("Unhandled exception getting IPMI entity");
        }
        task.callback();
    }
}

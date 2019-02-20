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
    m_tasksProcessed = false;
    m_event.signal();
    epicsThreadSleep(0.1);
}

bool Provider::schedule(const Task&& task)
{
    m_mutex.lock();
    m_tasks.emplace_back(task);
    m_event.signal();
    m_mutex.unlock();
    return true;
}

void Provider::tasksThread()
{
    while (m_tasksProcessed) {
        m_event.wait();

        m_mutex.lock();
        if (m_tasks.empty()) {
            m_mutex.unlock();
            continue;
        }

        Task task = std::move(m_tasks.front());
        m_tasks.pop_front();
        m_mutex.unlock();

        try {
            task.entity = getEntity(task.address);
        } catch (...) {
            task.entity["SEVR"] = epicsAlarmComm;
            task.entity["STAT"] = epicsSevInvalid;
        }
        task.callback();
    }
}

/* epicsdevice.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date Oct 2018
 */

#include <aiRecord.h>
#include <alarm.h>
#include <callback.h>
#include <cantProceed.h>
#include <devSup.h>
#include <epicsExport.h>
#include <epicsTime.h>
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <initHooks.h>
#include <recGbl.h>
//#include <mbbiRecord.h>
//#include <mbbiDirectRecord.h>
#include <stringinRecord.h>

#include <functional>
#include <map>
#include <stdlib.h>
#include <regex>

#include "provider.h"

namespace epicsipmi {
namespace device {

struct RecordContext {
    CALLBACK callback;
    std::string conn_id;
    std::string type;
    std::string addrspec;
    std::string field;
    const void* rsp = nullptr;
};

class CallbackManager : private epicsThreadRunable {
    private:
        struct Task {
            aiRecord *ai = nullptr;
            stringinRecord *stringin = nullptr;
            Task(stringinRecord* rec) { stringin = rec; };
            Task(aiRecord* rec) { ai = rec; };
        };

        epicsThread m_thread;
        epicsMutex m_mutex;
        epicsEvent m_event;
        bool m_running = true;
        std::list<Task> m_tasks;

    public:
        CallbackManager()
        : m_thread(*this, "epicsipmi callback", epicsThreadGetStackSize(epicsThreadStackSmall), epicsThreadPriorityLow)
        {
            m_thread.start();
        }

        ~CallbackManager()
        {
            m_running = false;
            m_event.signal();
        }

        template<typename T>
        void schedule(T *rec)
        {
            Task task(rec);
            m_mutex.lock();
            m_tasks.emplace_back(std::move(task));
            m_mutex.unlock();
            m_event.signal();
        }

        void executeAi(aiRecord *rec)
        {
            if (rec->pact != 1)
                return;

            auto* dpvt = reinterpret_cast<RecordContext*>(rec->dpvt);
            auto* rsp = reinterpret_cast<aiRecord*>(const_cast<void*>(dpvt->rsp));
            if (rsp == nullptr)
                return;

            do {
                // Set default STAT and SEVR
                rsp->stat = epicsAlarmRead;
                rsp->sevr = epicsSevInvalid;

                auto conn = provider::getConnection(dpvt->conn_id);
                if (!conn) {
                    rsp->stat = epicsAlarmComm;
                    break;
                }

                if (dpvt->type == "SENA") {
                    provider::EntityInfo::Properties properties;
                    auto ret = conn->getSensor(dpvt->addrspec, rsp->val, rsp->low, rsp->lolo, rsp->high, rsp->hihi, rsp->prec, rsp->hyst);
                    if (ret == provider::BaseProvider::SUCCESS) {
                        rsp->stat = epicsAlarmNone;
                        rsp->stat = epicsSevNone;
                        break;
                    }
                }
                // otherwise use default STAT and SEVR
            } while (false);

            callbackRequestProcessCallback(&dpvt->callback, rec->prio, rec);
        }

        void executeStringin(stringinRecord *rec)
        {
            if (rec->pact != 1)
                return;

            auto* dpvt = reinterpret_cast<RecordContext*>(rec->dpvt);
            auto* rsp = reinterpret_cast<stringinRecord*>(const_cast<void*>(dpvt->rsp));
            if (rsp == nullptr)
                return;

            do {
                // Set default stat and sevr
                rsp->stat = epicsAlarmRead;
                rsp->sevr = epicsSevInvalid;

                auto conn = provider::getConnection(dpvt->conn_id);
                if (!conn) {
                    rsp->stat = epicsAlarmComm;
                    break;
                }

                if (dpvt->type == "FRU") {
                    provider::EntityInfo::Properties properties;
                    auto ret = conn->getFruProperties(dpvt->addrspec, properties);
                    auto property = properties.find(dpvt->field);
                    if (ret == provider::BaseProvider::NOT_CONNECTED || property == properties.end()) {
                        rsp->stat = epicsAlarmComm;
                        break;
                    }
                    if (ret == provider::BaseProvider::SUCCESS) {
                        rsp->stat = epicsAlarmNone;
                        rsp->stat = epicsSevNone;
                        strncpy(rsp->val, property->value.sval.c_str(), sizeof(rsp->val));
                        break;
                    }
                    // otherwise use default stat and sevr
                }
            } while (false);

            callbackRequestProcessCallback(&dpvt->callback, rec->prio, rec);
        }

        void run() override
        {
            while (m_running) {
                m_event.wait();
                m_mutex.lock();
                if (m_tasks.empty()) {
                    m_mutex.unlock();
                    continue;
                } else {
                    auto task = m_tasks.front();
                    m_tasks.pop_front();
                    m_mutex.unlock();

                    // Single thread, no locking required
                    if (task.ai != nullptr) {
                        executeAi(task.ai);
                    } else if (task.stringin != nullptr) {
                        executeStringin(task.stringin);
                    }
                }
            }
            // thread exiting
        }
};
static std::map<std::string, CallbackManager> callbacks;

/**
 * @brief Parse EPICS record link specification
 * @param link Input link string
 * @param conn_id Parsed connection id
 * @param type IPMI entity type
 * @param addrspec IPMI system address specification
 * @param field Optional field name
 * @return true when parse, false on any error
 *
 * epicsipmi link consists of 3 tokens separated by space character.
 * First token is the connection identifier as used when establishing
 * connection. Second token is the type of IPMI entity. Third one is
 * IPMI specific address specification text used to address the entity.
 * The 3 token text is surrounded by @ipmi(...) identifier. For example:
 * \\@ipmi(IPMI1 FRU 50)
 */
static bool parseLink(const std::string& link, std::string& conn_id, std::string& type, std::string& addrspec, std::string& field)
{
    static std::regex re("^@ipmi\\( *([^ ]+) +([^ ]+) +([^ ]+) *([^ ]*).*\\)$");
    std::smatch m;
    if (!std::regex_search(link, m, re) || m.empty()) {
        return false;
    }

    conn_id = m[1];
    type = m[2];
    addrspec = m[3];
    if (m.length(4) > 0)
        field = m[4];
    return true;
}

void copyRecord(const aiRecord* src, aiRecord* dst)
{
    dst->val = src->val;
    dst->low = src->low;
    dst->lolo = src->lolo;
    dst->high = src->high;
    dst->hihi = src->hihi;
    dst->prec = src->prec;
    dst->hyst = src->hyst;
    strncpy(dst->egu, src->egu, sizeof(dst->egu));
}

void copyRecord(const stringinRecord* src, stringinRecord* dst)
{
    strncpy(dst->val,  src->val,  sizeof(dst->val));
    strncpy(dst->oval, src->oval, sizeof(dst->oval));
}

template<typename T>
static long initGeneric(T* rec)
{
    RecordContext* ctx = reinterpret_cast<RecordContext*>(callocMustSucceed(1, sizeof(RecordContext), "recordInit"));
    ctx->rsp = nullptr;

    if (!parseLink(rec->inp.text, ctx->conn_id, ctx->type, ctx->addrspec, ctx->field)) {
        fprintf(stderr, "ERROR: Invalid epicsipmi address");
        ::free(ctx);
        return -1;
    }

    rec->dpvt = ctx;
    return 0;
}

template<typename T>
static long processGeneric(T* rec)
{
    auto* dpvt = reinterpret_cast<RecordContext*>(rec->dpvt);

    if (rec->pact == 0) {
        // This is the first pass of processing - schedule to get value
        if (dpvt->rsp == nullptr)
            dpvt->rsp = callocMustSucceed(1, sizeof(T), "epicsipmi::record_response");
        auto* rsp = reinterpret_cast<T*>(const_cast<void*>(dpvt->rsp));
        copyRecord(rec, rsp);
        callbacks[dpvt->conn_id].schedule(rec);
        rec->pact = 1;
        return 0;
    }

    // This is the second pass, we got the value now update the record
    rec->pact = 0;
    epicsTimeGetCurrent(&rec->time);
    auto* rsp = reinterpret_cast<T*>(const_cast<void*>(dpvt->rsp));
    if (rsp == nullptr) {
        recGblSetSevr(rec, epicsAlarmRead, epicsSevInvalid);
        return -1;
    }

    recGblSetSevr(rec, rsp->stat, rsp->sevr);
    copyRecord(rsp, rec);
    ::free(rsp);
    dpvt->rsp = nullptr;
    return 0;
}

static long processAi(aiRecord* rec) {
    long ret = processGeneric<aiRecord>(rec);
    return (ret == 0 ? 2 : ret);
}

static long processStringin(stringinRecord* rec) {
    std::string oval(rec->val, sizeof(rec->val));
    long ret = processGeneric<stringinRecord>(rec);
    if (ret == 0)
        strncmp(rec->oval, oval.c_str(), sizeof(rec->oval));
    return ret;
}

extern "C" {

struct {
   long            number;
   DEVSUPFUN       report;
   DEVSUPFUN       init;
   DEVSUPFUN       init_record;
   DEVSUPFUN       get_ioint_info;
   DEVSUPFUN       read_ai;
   DEVSUPFUN       special_linconv;
} devEpicsIpmiAi = {
   6, // number
   NULL, // report
   NULL, // init
   (DEVSUPFUN)initGeneric<aiRecord>,
   NULL, // get_ioint_info
   (DEVSUPFUN)processAi,
   NULL  // special_linconv
};
epicsExportAddress(dset, devEpicsIpmiAi);

struct {
   long            number;
   DEVSUPFUN       report;
   DEVSUPFUN       init;
   DEVSUPFUN       init_record;
   DEVSUPFUN       get_ioint_info;
   DEVSUPFUN       read_ai;
   DEVSUPFUN       special_linconv;
} devEpicsIpmiStringin = {
   6, // number
   NULL, // report
   NULL, // init
   (DEVSUPFUN)initGeneric<stringinRecord>,
   NULL, // get_ioint_info
   (DEVSUPFUN)processStringin,
   NULL  // special_linconv
};
epicsExportAddress(dset, devEpicsIpmiStringin);

}; // extern "C"

} // namespace device
} // namespace epicsipmi

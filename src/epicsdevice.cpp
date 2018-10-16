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
#include <epicsString.h>
#include <initHooks.h>
#include <recGbl.h>
//#include <mbbiRecord.h>
//#include <mbbiDirectRecord.h>

#include <functional>
#include <map>
#include <stdlib.h>
#include <regex>

#include "provider.h"

namespace epicsipmi {
namespace device {

struct RecordContext {
    CALLBACK callback;
    const char *addrspec;
};

static std::map<std::string, provider::EntityInfo::Property::Value> g_cachedValues;
static epicsMutex g_cachedValuesMutex;

void processCallbackAi(aiRecord *rec, bool valid, provider::EntityInfo::Property::Value& value)
{
    auto* dpvt = reinterpret_cast<RecordContext*>(rec->dpvt);
    if (valid) {
        g_cachedValuesMutex.lock();
        g_cachedValues[dpvt->addrspec] = value;
        g_cachedValuesMutex.unlock();
    }
    if (rec->pact == 1) {
        callbackRequestProcessCallback(&dpvt->callback, rec->prio, rec);
    }
}

bool getCachedValue(const std::string& addrspec, double& value)
{
    bool ret = false;
    g_cachedValuesMutex.lock();
    auto it = g_cachedValues.find(addrspec);
    if (it != g_cachedValues.end()) {
        value = it->second.dval;
        g_cachedValues.erase(it);
        ret = true;
    }
    g_cachedValuesMutex.unlock();
    return ret;
}

static long processAi(aiRecord* rec) {
    bool ret;
    double value;
    auto* dpvt = reinterpret_cast<RecordContext*>(rec->dpvt);

    if (rec->pact == 0) {
        auto cb = std::bind(&processCallbackAi, rec, std::placeholders::_1, std::placeholders::_2);
        ret = provider::scheduleTask(dpvt->addrspec, cb);
        if (ret) {
            rec->pact = 1;
            return 0;
        }
    } else {
        ret = getCachedValue(dpvt->addrspec, value);
        rec->pact = 0;
    }

    epicsTimeGetCurrent(&rec->time);
    if (ret) {
        recGblSetSevr(rec, epicsAlarmNone, epicsSevNone);
        rec->val = value;
        return 2;
    } else {
        recGblSetSevr(rec, epicsAlarmRead, epicsSevInvalid);
        return -1;
    }
}

static long initAi(aiRecord* rec) {
    RecordContext* ctx = reinterpret_cast<RecordContext*>(callocMustSucceed(1, sizeof(RecordContext), "initAi"));

    static std::regex re("^@ipmi\\((.*)\\)$");
    std::smatch m;
    std::string link(rec->inp.text);
    if (!std::regex_search(link, m, re) || m.empty()) {
        free(ctx);
        return -1;
    }

    ctx->addrspec = epicsStrDup(std::string(m[1]).c_str());
    rec->dpvt = ctx;
    return 0;
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
   initAi, // init_record
   NULL, // get_ioint_info
   processAi, // read_ai
   NULL  // special_linconv
};
epicsExportAddress(dset, devEpicsIpmiAi);

}; // extern "C"

} // namespace device
} // namespace epicsipmi

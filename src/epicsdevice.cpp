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
#include <recGbl.h>
#include <stringinRecord.h>

#include <limits>

#include "common.h"
#include "dispatcher.h"

struct IpmiRecord {
    CALLBACK callback;
    Provider::Entity entity;
};

template<typename T>
long initInpRecord(T* rec)
{
    if (dispatcher::checkLink(rec->inp.text) == false) {
        if (rec->tpro == 1) {
            LOG_ERROR("invalid record link or no connection");
        }
        return -1;
    }
    rec->dpvt = callocMustSucceed(1, sizeof(IpmiRecord), "ipmi::initGeneric");
    return 0;
}

static long processAiRecord(aiRecord* rec)
{
    IpmiRecord* ctx = reinterpret_cast<IpmiRecord*>(rec->dpvt);

    if (rec->pact == 0) {
        rec->pact = 1;

        std::function<void()> cb = std::bind(callbackRequestProcessCallback, &ctx->callback, rec->prio, rec);
        if (dispatcher::scheduleGet(rec->inp.value.instio.string, cb, ctx->entity) == false) {
            // Keep PACT=1 to prevent further processing
            recGblSetSevr(rec, epicsAlarmUDF, epicsSevInvalid);
            return -1;
        }

        return 0;
    }

    // This is the second pass, we got new value now update the record
    rec->pact = 0;

    auto sevr = ctx->entity.getField<int>("SEVR", epicsAlarmNone);
    auto stat = ctx->entity.getField<int>("STAT", epicsSevNone);
    rec->val = ctx->entity.getField<double>("VAL", rec->val);
    rec->rval = rec->val;

    (void)recGblSetSevr(rec, stat, sevr);

    if (rec->egu[0] == 0)
        common::copy(ctx->entity.getField<std::string>("EGU", ""), rec->egu, sizeof(rec->egu));
    if (rec->desc[0] == 0)
        common::copy(ctx->entity.getField<std::string>("DESC", ""), rec->desc, sizeof(rec->desc));

    return 0;
}

static long processStringinRecord(stringinRecord* rec)
{
    IpmiRecord* ctx = reinterpret_cast<IpmiRecord*>(rec->dpvt);

    if (rec->pact == 0) {
        rec->pact = 1;

        std::function<void()> cb = std::bind(callbackRequestProcessCallback, &ctx->callback, rec->prio, rec);
        if (dispatcher::scheduleGet(rec->inp.value.instio.string, cb, ctx->entity) == false) {
            // Keep PACT=1 to prevent further processing
            recGblSetSevr(rec, epicsAlarmUDF, epicsSevInvalid);
            return -1;
        }

        return 0;
    }

    // This is the second pass, we got new value now update the record
    rec->pact = 0;

    common::copy(ctx->entity.getField<std::string>("VAL", rec->val), rec->val, sizeof(rec->val));
    rec->sevr = ctx->entity.getField<int>("SEVR", rec->sevr);
    rec->stat = ctx->entity.getField<int>("STAT", rec->stat);

    if (rec->desc[0] == 0)
        common::copy(ctx->entity.getField<std::string>("DESC", ""), rec->desc, sizeof(rec->desc));

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
   NULL,                                // report
   NULL,                                // once-per-IOC initialization
   (DEVSUPFUN)initInpRecord<aiRecord>,  // once-per-record initialization
   NULL,                                // get_ioint_info
   (DEVSUPFUN)processAiRecord,
   NULL                                 // special_linconv
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
   (DEVSUPFUN)initInpRecord<stringinRecord>,
   NULL, // get_ioint_info
   (DEVSUPFUN)processStringinRecord,
   NULL  // special_linconv
};
epicsExportAddress(dset, devEpicsIpmiStringin);

}; // extern "C"

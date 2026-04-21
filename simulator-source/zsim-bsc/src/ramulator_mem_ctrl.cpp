/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _WITH_RAMULATOR_  // was compiled with ramulator

#include "ramulator_mem_ctrl.h"

#include <map>
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "zsim.h"
#include "RamulatorWrapper.h"
#include "StatType.h"
#include "Request.h"
#include <cmath>

class RamulatorAccEvent : public TimingEvent {
private:
    Ramulator* dram;
    bool write;

public:
    Address addr;
    uint32_t coreid;
    
    uint64_t sCycle;    // predicted issue cycle (zsim enqueue cycle)

    uint64_t issueCycle; // actual issue cycle (core freq domain)
    uint64_t dramIssueCycle;  // actual issue cycle (dram freq domain)
    uint32_t boundLatency; // latency estimated by the bound phase estimator

    RamulatorAccEvent(Ramulator* _dram, bool _write, Address _addr, int32_t domain, uint32_t _coreid)
        : TimingEvent(0, 0, domain), dram(_dram), write(_write), addr(_addr), coreid(_coreid) {}

    bool isWrite() const { return write; }

    void simulate(uint64_t startCycle) {
        sCycle = startCycle;

        dram->enqueue(this, startCycle);
    }
};

Ramulator::Ramulator(const g_string& name, uint32_t domain, unsigned cpuFreq, const std::string& configPath,
    g_vector<IBoundMemLatencyEstimator*> estimators, unsigned numCpus, unsigned cacheLineSize, bool pimMode,
    const string& application, bool recordMemoryTrace, bool networkOverhead, const std::vector<uint32_t>& trackedCores)
    : name(name), domain(domain), estimators(estimators), pim_mode(pimMode),
      statList(nullptr),
      read_cb_func(std::bind(&Ramulator::onReadComplete, this, std::placeholders::_1)),
      write_cb_func(std::bind(&Ramulator::onWriteComplete, this, std::placeholders::_1))
    {
    curCycle = 0;
    dramPs = 0, cpuPs = 0;

    const char* configPathCStr = configPath.c_str();
    std::string outputDir = zinfo->outputDir;
    std::string applicationName = outputDir + "/" + application;
    const char* app_name = applicationName.c_str();
    statList = new Stats_ramulator::StatList();
    {
        Stats_ramulator::StatListScope guard(statList);
        wrapper = new ramulator::RamulatorWrapper(configPathCStr, numCpus, cacheLineSize, pimMode, recordMemoryTrace,
            app_name, networkOverhead, trackedCores);
    }

    // setup device synch tick params
    const double tCK = wrapper->get_tCK();
    const double memFreq = (1 / (tCK / 1000000)) / 1000;
    const double cpuFreqHz = pim_mode ? memFreq : cpuFreq;
    dramPsPerClk = static_cast<uint64_t>(tCK * 1000);
    cpuPsPerClk = static_cast<uint64_t>(1000000. / cpuFreqHz);
    assert(cpuPsPerClk < dramPsPerClk);

    TickEvent<Ramulator>* tickEv = new TickEvent<Ramulator>(this, domain);
    tickEv->queue(0); // start the sim at time 0

    statList->output(outputDir + "/" + application + "_" + this->name.c_str() + ".ramulator.stats");
    // Custom addition BEGIN: configure per-core tracker
    coreTracker.configure(trackedCores, numCpus);
    // Custom addition END: configure per-core tracker
}

Ramulator::~Ramulator() {
    Stats_ramulator::StatListScope statsGuard(statList);
    delete wrapper;
    if (statList) {
        delete statList;
        statList = nullptr;
    }
}

void Ramulator::initStats(AggregateStat* parentStat) {
    AggregateStat* memStats = new AggregateStat();
    memStats->init(name.c_str(), "Memory controller stats");
    profReads.init("rd", "Read requests");
    memStats->append(&profReads);
    profWrites.init("wr", "Write requests");
    memStats->append(&profWrites);
    profTotalRdLat.init("rdlat", "Total latency experienced by read requests");
    memStats->append(&profTotalRdLat);
    profTotalRdLatBound.init("rdlatbound", "Total bound latency experienced by read requests");
    memStats->append(&profTotalRdLatBound);
    profTotalWrLat.init("wrlat", "Total latency experienced by write requests");
    memStats->append(&profTotalWrLat);
    profTotalSkewLat.init("est_skew_lat", "Total latency experienced by requests in wait queue");
    memStats->append(&profTotalSkewLat);
    reissuedAccesses.init("reissuedAccesses", "Number of accesses that were reissued due to full queue");
    memStats->append(&reissuedAccesses);
    coreTracker.registerStats(memStats, "trackCores");
    profTotalAbsError.init("totalAbsError", "Total absolute error between bound and weave latencies");
    memStats->append(&profTotalAbsError);
    profTotalAbsErrorCounter.init("totalAbsErrorCounter", "Total number of samples for absolute error of bound latency");
    memStats->append(&profTotalAbsErrorCounter);
    parentStat->append(memStats);
}

uint64_t Ramulator::access(MemReq& req) {
    futex_lock(&updateLock);
    if (!isBoundPhase) {
        scoped_mutex sm(reset_mutex);
        isBoundPhase = true;
        for (auto& est : estimators) {
            est->reset();
        }
    }
    futex_unlock(&updateLock);

    switch (req.type) {
        case PUTS:
        case PUTX:
            *req.state = I;
            break;
        case GETS:
            *req.state = req.is(MemReq::NOEXCL) ? S : E;
            break;
        case GETX:
            *req.state = M;
            break;
        default:
            panic("!?");
    }

    uint64_t respCycle = req.cycle;

    if (req.type != PUTS && zinfo->eventRecorders[req.srcId]) {
        bool isWrite = (req.type == PUTX);
        Address addr = req.lineAddr << lineBits;
        RamulatorAccEvent* accEv =
            new (zinfo->eventRecorders[req.srcId]) RamulatorAccEvent(this, isWrite, addr, domain, req.srcId);
        accEv->setMinStartCycle(req.cycle - 1);
        accEv->boundLatency = estimators[req.srcId]->getMemLatency();
        TimingRecord tr = { addr, req.cycle - 1, respCycle, req.type, accEv, accEv };
        zinfo->eventRecorders[req.srcId]->pushRecord(tr);
        respCycle += estimators[req.srcId]->getMemLatency();
    }

    return respCycle;
}

ramulator::Request Ramulator::getRequestFromAccEvent(RamulatorAccEvent* accEv) {
    auto addr = static_cast<long>(accEv->addr);
    auto coreId = accEv->coreid;
    auto req_type = accEv->isWrite() ? ramulator::Request::Type::WRITE : ramulator::Request::Type::READ;
    auto callback = accEv->isWrite() ? write_cb_func : read_cb_func;
    return ramulator::Request(addr, req_type, callback, coreId);
}

uint32_t Ramulator::tick(uint64_t /*cycle*/) {
    Stats_ramulator::StatListScope statsGuard(statList);
    // Only the master process should drive the external DRAM model
    if (procIdx != 0) return 1;
    pushInFlights();

    cpuPs += cpuPsPerClk;
    curCycle++;

    if (cpuPs > dramPs) {
        wrapper->tick();
        dramPs += dramPsPerClk;
        dramCycle++;
    }

    if (cpuPs == dramPs) {
        cpuPs = 0;
        dramPs = 0;
    }

    return 1;
}

void Ramulator::finish() {
    Stats_ramulator::StatListScope statsGuard(statList);
    wrapper->finish();
    if (statList) {
        statList->printall();
        delete statList;
        statList = nullptr;
    }
}

void Ramulator::enqueue(RamulatorAccEvent* accEv, uint64_t /*cycle*/) {
    Stats_ramulator::StatListScope statsGuard(statList);
    if (isBoundPhase) {
        isBoundPhase = false;
    }

    auto backendReq = getRequestFromAccEvent(accEv);

    if (!wrapper->send(backendReq)) {
        reissuedAccesses.inc();
        notQueuedRequests.emplace_back(accEv);
    } else {
        accEv->issueCycle = curCycle;
        accEv->dramIssueCycle = dramCycle;
        coreTracker.recordIssue(accEv->coreid, accEv->issueCycle, accEv->sCycle, accEv->isWrite());

        inflightRequests.insert({ backendReq._addr, accEv });
    }

    accEv->hold();
}

void Ramulator::onReadComplete(ramulator::Request& req) {
    bool isWrite = req.type == ramulator::Request::Type::WRITE;

    auto range = inflightRequests.equal_range(static_cast<uint64_t>(req._addr));
    auto it = range.first;
    for (; it != range.second; ++it) {
        RamulatorAccEvent* cand = it->second;
        if (static_cast<int>(cand->coreid) == req.coreid && cand->isWrite() == isWrite) {
            break;
        }
    }
    if (it == inflightRequests.end()) {
        panic("unexpected request");
    }
    assert((it != inflightRequests.end()));

    RamulatorAccEvent* accEv = it->second;
    uint32_t lat = curCycle + 1 - accEv->sCycle;
    uint32_t service_lat = curCycle + 1 - accEv->issueCycle;

    if (accEv->isWrite()) {
        profWrites.inc();
        profTotalWrLat.inc(lat);
    } else {
        profReads.inc();
        profTotalRdLat.inc(lat);
        profTotalRdLatBound.inc(accEv->boundLatency);

        // adding error of bound and weave (error percent)
        uint32_t absErr = 100*((lat>accEv->boundLatency)?(lat-accEv->boundLatency) : (accEv->boundLatency-lat));
        absErr /= lat;
        // printf("Ramulator: core %d, addr 0x%lx, weave lat %u, bound lat %u, abs error %u%%\n",
        //     accEv->coreid, accEv->addr, lat, accEv->boundLatency, absErr);
        profTotalAbsError.inc(absErr);
        profTotalAbsErrorCounter.inc();

        // update the estimator model (only for read because most dram simulator call the write callback as soon as it
        // reaches the queue)
        estimators[req.coreid]->updateModel(lat);
    }

    coreTracker.recordComplete(accEv->coreid, lat, service_lat, accEv->isWrite());
    accEv->release();
    accEv->done(curCycle + 1);
    inflightRequests.erase(it);
}

void Ramulator::onWriteComplete(ramulator::Request& req) {
    // Same as read for now
    onReadComplete(req);
}

void Ramulator::pushInFlights() {
    Stats_ramulator::StatListScope statsGuard(statList);

    int numberOfTries = 0;
    for (auto it = notQueuedRequests.begin(); it != notQueuedRequests.end();) {
        auto* accEv = *it;
        if (accEv->sCycle > curCycle) {
            ++it;
            continue;
        }

        if(numberOfTries==8) {
            break;
        }
        numberOfTries++;

        ramulator::Request backendReq = getRequestFromAccEvent(accEv);
        if (wrapper->send(backendReq)) {
            if (!accEv->isWrite()) {
                // only for reads
                profTotalSkewLat.inc(curCycle - accEv->sCycle);
            }

            accEv->dramIssueCycle = dramCycle;
            accEv->issueCycle = curCycle;

            coreTracker.recordIssue(accEv->coreid, accEv->issueCycle, accEv->sCycle, accEv->isWrite());
            inflightRequests.insert({ accEv->addr, accEv });

            it = notQueuedRequests.erase(it);
        } else {
            ++it;
        }
    }
}

#endif

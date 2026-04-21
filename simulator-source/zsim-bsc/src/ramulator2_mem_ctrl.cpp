#ifdef _WITH_RAMULATOR2_

#include <algorithm>

#include "ramulator2_mem_ctrl.h"
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "zsim.h"

// std::string toString(const Ramulator::Request& req);
class Ramulator2AccEvent : public TimingEvent {
private:
    Ramulator2* dram;
    bool write;
    bool prefetch;
    Address addr;
    uint32_t coreid;

public:
    uint64_t sCycle;
    uint64_t dramCycle;
    uint32_t boundLatency; // latency estimated by the bound phase estimator
    uint64_t enqueueCycle;
    uint64_t issueCycle;
    Ramulator2AccEvent(Ramulator2* _dram, bool _write, bool _prefetch, Address _addr, int32_t domain, uint32_t _coreid)
        : TimingEvent(0, 0, domain), dram(_dram), write(_write), prefetch(_prefetch), addr(_addr), coreid(_coreid),
          enqueueCycle(0), issueCycle(0) {}

    bool isWrite() const { return write; }
    bool isPrefetch() const { return prefetch; }

    Address getAddr() const { return addr; }

    uint32_t getCoreID() const { return coreid; }
    uint64_t getEnqueueCycle() const { return enqueueCycle; }
    uint64_t getIssueCycle() const { return issueCycle; }
    void setIssueCycle(uint64_t cycle) { issueCycle = cycle; }

    void simulate(uint64_t startCycle) {
        sCycle = startCycle;
        enqueueCycle = startCycle;
        issueCycle = startCycle;
        dram->enqueue(this, startCycle);
    }
};

Ramulator2::Ramulator2(const g_string& name, uint32_t domain, unsigned cpuFreq, const std::string& configPath,
    g_vector<IBoundMemLatencyEstimator*> estimators, unsigned numCpus, bool /*recordMemoryTrace*/,
    const std::vector<uint32_t>& trackedCores)
    : name(name), domain(domain), estimators(estimators) {
    // setup times
    curCycle = 0;
    dramCycle = 0;
    dramPs = 0, cpuPs = 0;

    // setup simulator wrapper
    wrapper = new Ramulator::Ramulator2Wrapper(configPath.c_str());
    // wrapper = Ramulator::getRamulator2Wrapper(configPath.c_str());

    // wrapper->enable_debug(trace_file.c_str());
    // Use completion queue (no cross-DSO std::function)
    // wrapper->enable_completion_queue();

    // setup device synch tick params
    const double tCK = wrapper->get_tCK();
    dramPsPerClk = static_cast<uint64_t>(tCK * 1000);
    cpuPsPerClk = static_cast<uint64_t>(1000000. / cpuFreq);
    assert(cpuPsPerClk < dramPsPerClk);

    TickEvent<Ramulator2>* tickEv = new TickEvent<Ramulator2>(this, domain);
    tickEv->queue(0); // start the sim at time 0
    coreTracker.configure(trackedCores, numCpus);
}

Ramulator2::~Ramulator2() { delete wrapper; }

void Ramulator2::initStats(AggregateStat* parentStat) {
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
    profTotalAbsError.init("totalAbsError", "Total absolute error between bound and weave latencies");
    memStats->append(&profTotalAbsError);
    profTotalAbsErrorCounter.init("totalAbsErrorCounter", "Total number of samples for absolute error of bound latency");
    memStats->append(&profTotalAbsErrorCounter);
    coreTracker.registerStats(memStats, "trackCores");
    parentStat->append(memStats);
}

uint64_t Ramulator2::access(MemReq& req) {
    futex_lock(&updateLock);
    if (!isBoundPhase) {
        isBoundPhase = true;
        for (auto& est: estimators) {
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
        bool isPrefetch = req.is(MemReq::PREFETCH) || req.is(MemReq::PREFETCH_HINT);
        auto accEv = new (zinfo->eventRecorders[req.srcId]) Ramulator2AccEvent(this, isWrite, isPrefetch, addr, domain, req.srcId);
        accEv->setMinStartCycle(req.cycle - 1);
        accEv->boundLatency = estimators[req.srcId]->getMemLatency();
        TimingRecord tr = { addr, req.cycle - 1, respCycle, req.type, accEv, accEv };
        zinfo->eventRecorders[req.srcId]->pushRecord(tr);

        // if (req.is(MemReq::PREFETCH_NOREC)) {
        //     memEv->queue(req.cycle - 1);
        // } else {
        //     TimingRecord tr = { addr, req.cycle - 1, respCycle, req.type, memEv, memEv };
        //     zinfo->eventRecorders[req.srcId]->pushRecord(tr);
        // }

        respCycle += estimators[req.srcId]->getMemLatency();
    }

    return respCycle;
}

Ramulator::Request Ramulator2::getRequestFromAccEvent(Ramulator2AccEvent* accEv) {
    auto callback = [this](Ramulator::Request& req) {
        if (req.type_id == Ramulator::Request::Type::Read) {
            this->onReadComplete(req);
        } else {
            this->onWriteComplete(req);
        }
    };

    int64_t addr = static_cast<int64_t>(static_cast<uint64_t>(accEv->getAddr()) & 0x7fffffffffffffffULL);
    Ramulator::Request backendReq(addr, accEv->isWrite() ? 1 : 0, accEv->getCoreID(), callback);
    backendReq.scratchpad[0] = accEv->isPrefetch() ? 1 : 0;
    return backendReq;
}

// void Ramulator2::handle_completion(uint64_t addr, int type_id, int source_id) {
//     // Find matching event for this completion; multiple in-flight may share addr
//     auto range = inflightRequests.equal_range(static_cast<uint64_t>(addr));
//     auto it = range.first;
//     for (; it != range.second; ++it) {
//         Ramulator2AccEvent* cand = it->second;
//         if (static_cast<int>(cand->getCoreID()) == source_id) {
//             break;
//         }
//     }
//     if (it == range.second) {
//         // No matching inflight request found; drop silently
//         return;
//     }

//     Ramulator2AccEvent* ev = it->second;
//     uint32_t lat = curCycle + 1 - ev->sCycle;

//     bool is_write = (type_id == 1); // 0=Read, 1=Write in Ramulator2
//     if (is_write) {
//         profWrites.inc();
//         profTotalWrLat.inc(lat);
//     } else {
//         profReads.inc();
//         profTotalRdLat.inc(lat);
//         estimator->updateModel(lat);
//     }

//     accEv->release();
//     accEv->done(curCycle + 1);
//     inflightRequests.erase(it);
// }

uint32_t Ramulator2::tick(uint64_t /*cycle*/) {
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

void Ramulator2::finish() {
    wrapper->finish();
    // Stats_ramulator::statlist.printall();
}

void Ramulator2::enqueue(Ramulator2AccEvent* accEv, uint64_t /*cycle*/) {
    accEv->hold();

    if (isBoundPhase) {
        isBoundPhase = false;
    }

    auto backendReq = getRequestFromAccEvent(accEv);

    if (!wrapper->send(backendReq)) {
        notQueuedRequests.emplace_back(accEv);
        reissuedAccesses.inc();
    } else {
        accEv->setIssueCycle(curCycle);
        coreTracker.recordIssue(accEv->getCoreID(), accEv->getIssueCycle(), accEv->getEnqueueCycle(), accEv->isWrite());
        // ev->sCycle = curCycle;
        accEv->dramCycle = dramCycle;
        if (!accEv->isWrite()) {
            // only track reads in ramulator2
            inflightRequests.insert({ backendReq.addr, accEv });
        } else {
            // warning: we can't keep track of profTotalWrLat (ramulator2: no callback for writes)
            profWrites.inc();
            uint64_t lat = curCycle + 1 - accEv->sCycle;
            uint64_t serviceLat = curCycle + 1 - accEv->getIssueCycle();
            coreTracker.recordComplete(accEv->getCoreID(), lat, serviceLat, true);
            accEv->release();
            accEv->done(curCycle + 1);
        }
    }

}

void Ramulator2::onReadComplete(Ramulator::Request& backendReq) {
    // Find matching event for this completion; multiple in-flight may share addr
    auto range = inflightRequests.equal_range(static_cast<uint64_t>(backendReq.addr));
    auto it = range.first;
    for (; it != range.second; ++it) {
        Ramulator2AccEvent* cand = it->second;
        if (static_cast<int>(cand->getCoreID()) == backendReq.source_id) {
            break;
        }
    }
    if (it == inflightRequests.end()) {
        panic("unexpected request");
    }
    assert((it != inflightRequests.end()));

    Ramulator2AccEvent* accEv = it->second;
    uint32_t lat = curCycle + 1 - accEv->sCycle;
    uint64_t serviceLat = curCycle + 1 - accEv->getIssueCycle();

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

    // update the estimator model
    estimators[backendReq.source_id]->updateModel(lat);

    coreTracker.recordComplete(accEv->getCoreID(), lat, serviceLat, false);
    accEv->release();
    accEv->done(curCycle + 1);
    inflightRequests.erase(it);
}

void Ramulator2::onWriteComplete(Ramulator::Request& /*backendReq*/) {
    // Warning: ramulator2 does not call write callbacks at all
    }

void Ramulator2::pushInFlights() {

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

        Ramulator::Request backendReq = getRequestFromAccEvent(accEv);
        if (wrapper->send(backendReq)) {

            accEv->setIssueCycle(curCycle);
            coreTracker.recordIssue(accEv->getCoreID(), accEv->getIssueCycle(), accEv->getEnqueueCycle(), accEv->isWrite());
            accEv->dramCycle = dramCycle;
            
            if (!accEv->isWrite()) {
                profTotalSkewLat.inc(curCycle - accEv->sCycle);
                inflightRequests.insert({ backendReq.addr, accEv });
            } else {
                // warning: we can't keep track of profTotalWrLat (ramulator2: no callback for writes)
                profWrites.inc();
                uint64_t lat = curCycle + 1 - accEv->sCycle;
                uint64_t serviceLat = curCycle + 1 - accEv->getIssueCycle();
                coreTracker.recordComplete(accEv->getCoreID(), lat, serviceLat, true);
                accEv->release();
                accEv->done(curCycle + 1);
            }

            it = notQueuedRequests.erase(it);
        } else {
            ++it;
        }
    }
}

#endif

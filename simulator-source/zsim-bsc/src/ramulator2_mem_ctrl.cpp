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

Ramulator2::Ramulator2(std::string config_path, g_vector<IBoundMemLatencyEstimator*> _estimators, unsigned num_cpus,
    uint32_t _domain, unsigned _cpuFreq, bool _record_memory_trace, const g_string& _name, const std::vector<uint32_t>& trackedCores)
    : name(_name), domain(_domain), estimators(_estimators) {
    // setup times
    curCycle = 0;
    dramCycle = 0;
    dramPs = 0, cpuPs = 0;

    // setup simulator wrapper
    wrapper = new Ramulator::Ramulator2Wrapper(config_path.c_str());
    // wrapper = Ramulator::getRamulator2Wrapper(config_path.c_str());

    // wrapper->enable_debug(trace_file.c_str());
    // Use completion queue (no cross-DSO std::function)
    // wrapper->enable_completion_queue();

    // setup device synch tick params
    tCK = wrapper->get_tCK();
    cpuFreq = _cpuFreq;
    memFreq = (1 / (tCK / 1000000)) / 1000;
    dramPsPerClk = static_cast<uint64_t>(tCK * 1000);
    cpuPsPerClk = static_cast<uint64_t>(1000000. / cpuFreq);
    assert(cpuPsPerClk < dramPsPerClk);

    TickEvent<Ramulator2>* tickEv = new TickEvent<Ramulator2>(this, domain);
    tickEv->queue(0); // start the sim at time 0
    coreTracker.configure(trackedCores, num_cpus);
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
        auto memEv = new (zinfo->eventRecorders[req.srcId]) Ramulator2AccEvent(this, isWrite, isPrefetch, addr, domain, req.srcId);
        memEv->setMinStartCycle(req.cycle - 1);
        memEv->boundLatency = estimators[req.srcId]->getMemLatency();
        TimingRecord tr = { addr, req.cycle - 1, respCycle, req.type, memEv, memEv };
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

Ramulator::Request Ramulator2::getRequestFromAccEvent(Ramulator2AccEvent* ev) {
    auto callback = [this](Ramulator::Request& req) {
        if (req.type_id == Ramulator::Request::Type::Read) {
            this->DRAM_read_return_cb(req);
        } else {
            this->DRAM_write_return_cb(req);
        }
    };

    int64_t ram_addr = static_cast<int64_t>(static_cast<uint64_t>(ev->getAddr()) & 0x7fffffffffffffffULL);
    Ramulator::Request request(ram_addr, ev->isWrite() ? 1 : 0, ev->getCoreID(), callback);
    request.scratchpad[0] = ev->isPrefetch() ? 1 : 0;
    return request;
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

//     ev->release();
//     ev->done(curCycle + 1);
//     inflightRequests.erase(it);
// }

uint32_t Ramulator2::tick(uint64_t cycle) {
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

void Ramulator2::enqueue(Ramulator2AccEvent* ev, uint64_t cycle) {
    ev->hold();

    if (isBoundPhase) {
        isBoundPhase = false;
    }

    auto req = getRequestFromAccEvent(ev);

    if (!wrapper->send(req)) {
        notQueuedRequests.emplace_back(ev);
        reissuedAccesses.inc();
    } else {
        ev->setIssueCycle(curCycle);
        coreTracker.recordIssue(ev->getCoreID(), ev->getIssueCycle(), ev->getEnqueueCycle(), ev->isWrite());
        // ev->sCycle = curCycle;
        ev->dramCycle = dramCycle;
        if (!ev->isWrite()) {
            // only track reads in ramulator2
            inflightRequests.insert({ req.addr, ev });
        } else {
            // warning: we can't keep track of profTotalWrLat (ramulator2: no callback for writes)
            profWrites.inc();
            uint64_t lat = curCycle + 1 - ev->sCycle;
            uint64_t serviceLat = curCycle + 1 - ev->getIssueCycle();
            coreTracker.recordComplete(ev->getCoreID(), lat, serviceLat, true);
            ev->release();
            ev->done(curCycle + 1);
        }
    }

}

void Ramulator2::DRAM_read_return_cb(Ramulator::Request& req) {
    // Find matching event for this completion; multiple in-flight may share addr
    auto range = inflightRequests.equal_range(static_cast<uint64_t>(req.addr));
    auto it = range.first;
    for (; it != range.second; ++it) {
        Ramulator2AccEvent* cand = it->second;
        if (static_cast<int>(cand->getCoreID()) == req.source_id) {
            break;
        }
    }
    if (it == inflightRequests.end()) {
        panic("unexpected request");
    }
    assert((it != inflightRequests.end()));

    Ramulator2AccEvent* ev = it->second;
    uint32_t lat = curCycle + 1 - ev->sCycle;
    uint64_t serviceLat = curCycle + 1 - ev->getIssueCycle();

    profReads.inc();
    profTotalRdLat.inc(lat);
    profTotalRdLatBound.inc(ev->boundLatency);

    // adding error of bound and weave (error percent)
    uint32_t absErr = 100*((lat>ev->boundLatency)?(lat-ev->boundLatency) : (ev->boundLatency-lat));
    absErr /= lat;
    // printf("Ramulator: core %d, addr 0x%lx, weave lat %u, bound lat %u, abs error %u%%\n",
    //     ev->coreid, ev->addr, lat, ev->boundLatency, absErr);
    profTotalAbsError.inc(absErr);
    profTotalAbsErrorCounter.inc();

    // update the estimator model
    estimators[req.source_id]->updateModel(lat);

    coreTracker.recordComplete(ev->getCoreID(), lat, serviceLat, false);
    ev->release();
    ev->done(curCycle + 1);
    inflightRequests.erase(it);
}

void Ramulator2::DRAM_write_return_cb(Ramulator::Request& req) {
    // Warning: ramulator2 does not call write callbacks at all
    }

void Ramulator2::pushInFlights() {

    int numberOfTries = 0;
    for (auto it = notQueuedRequests.begin(); it != notQueuedRequests.end();) {
        auto* mem_ev = *it;

        if (mem_ev->sCycle > curCycle) {
            ++it;
            continue;
        }

        if(numberOfTries==8) {
            break;
        }
        numberOfTries++;

        Ramulator::Request req = getRequestFromAccEvent(mem_ev);
        if (wrapper->send(req)) {

            mem_ev->setIssueCycle(curCycle);
            coreTracker.recordIssue(mem_ev->getCoreID(), mem_ev->getIssueCycle(), mem_ev->getEnqueueCycle(), mem_ev->isWrite());
            // mem_ev->sCycle = curCycle;
            mem_ev->dramCycle = dramCycle;
            
            if (!mem_ev->isWrite()) {
                profTotalSkewLat.inc(curCycle - mem_ev->sCycle);
                inflightRequests.insert({ req.addr, mem_ev });
            } else {
                // warning: we can't keep track of profTotalWrLat (ramulator2: no callback for writes)
                profWrites.inc();
                uint64_t lat = curCycle + 1 - mem_ev->sCycle;
                uint64_t serviceLat = curCycle + 1 - mem_ev->getIssueCycle();
                coreTracker.recordComplete(mem_ev->getCoreID(), lat, serviceLat, true);
                mem_ev->release();
                mem_ev->done(curCycle + 1);
            }

            it = notQueuedRequests.erase(it);
        } else {
            ++it;
        }
    }
}

#endif

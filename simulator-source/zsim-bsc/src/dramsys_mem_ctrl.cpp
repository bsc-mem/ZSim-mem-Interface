#ifdef _WITH_DRAMSYS_

#include "dramsys_wrapper.h"
#include "dramsys_mem_ctrl.h"

#include <algorithm>
#include <inttypes.h>

#include "event_recorder.h"
#include "log.h"
#include "tick_event.h"
#include "timing_event.h"
#include "zsim.h"

class DRAMSysAccEvent : public TimingEvent {
  private:
    DRAMSysMemory* dram_;
    bool write_;

  public:
    Address addr;
    uint32_t coreid;
    uint64_t reqTag;

    uint64_t sCycle;
    uint64_t issueCycle;
    uint64_t dramIssueCycle;
    uint32_t boundLatency;

    DRAMSysAccEvent(DRAMSysMemory* dram, bool write, Address addr, int32_t domain, uint32_t coreId)
        : TimingEvent(0, 0, domain),
          dram_(dram),
          write_(write),
          addr(addr),
          coreid(coreId),
          reqTag(0),
          sCycle(0),
          issueCycle(0),
          dramIssueCycle(0),
          boundLatency(0) {}

    bool isWrite() const { return write_; }

    void simulate(uint64_t startCycle) {
        sCycle = startCycle;
        dram_->enqueue(this, startCycle);
    }
};

DRAMSysRuntime* DRAMSysMemory::sharedRuntime_ = nullptr;
lock_t DRAMSysMemory::sharedRuntimeLock_ = 0;
uint32_t DRAMSysMemory::sharedRuntimeUsers_ = 0;
g_vector<DRAMSysMemory*> DRAMSysMemory::controllers_;

DRAMSysMemory::DRAMSysMemory(const g_string& name,
                             uint32_t domain,
                             uint32_t ctrlId,
                             uint32_t numControllers,
                             unsigned cpuFreqMHz,
                             const std::string& configPath,
                             g_vector<IBoundMemLatencyEstimator*> estimators,
                             const std::vector<uint32_t>& trackedCores)
    : name_(name),
      domain_(domain),
      ctrlId_(ctrlId),
      estimators_(estimators),
      wrapper_(nullptr),
      nextReqTag_(1),
      curCycle_(0),
      isBoundPhase_(false),
      updateLock_(0) {
    wrapper_ = std::make_unique<DRAMSysWrapper>((std::string("dramsys_wrapper_") + name_.c_str()).c_str(), configPath);

    const double tCK = wrapper_->getTckNs();
    const uint64_t dramPsPerClk = static_cast<uint64_t>(tCK * 1000);
    const uint64_t cpuPsPerClk = static_cast<uint64_t>(1000000. / cpuFreqMHz);
    assert(cpuPsPerClk > 0);
    assert(dramPsPerClk > 0);
    assert(cpuPsPerClk <= dramPsPerClk);

    registerController(this, numControllers, cpuPsPerClk, dramPsPerClk);

    TickEvent<DRAMSysMemory>* tickEv = new TickEvent<DRAMSysMemory>(this, domain_);
    tickEv->queue(0);

    coreTracker_.configure(trackedCores, zinfo->numCores);
}

DRAMSysMemory::~DRAMSysMemory() {
    for (auto& entry : inflightRequests_) {
        DRAMSysAccEvent* accEv = entry.second;
        if (accEv) {
            accEv->release();
            accEv->done(curCycle_);
        }
    }
    inflightRequests_.clear();

    for (auto* accEv : notQueuedRequests_) {
        if (accEv) {
            accEv->release();
            accEv->done(curCycle_);
        }
    }
    notQueuedRequests_.clear();

    unregisterController(this);
}

void DRAMSysMemory::initStats(AggregateStat* parentStat) {
    AggregateStat* memStats = new AggregateStat();
    memStats->init(name_.c_str(), "Memory controller stats");
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
    reissuedAccesses.init("reissuedAccesses", "Number of accesses reissued due to backend backpressure");
    memStats->append(&reissuedAccesses);
    profTotalAbsError.init("totalAbsError", "Total absolute error between bound and weave latencies");
    memStats->append(&profTotalAbsError);
    profTotalAbsErrorCounter.init("totalAbsErrorCounter", "Total number of abs-error samples");
    memStats->append(&profTotalAbsErrorCounter);
    coreTracker_.registerStats(memStats, "trackCores");
    parentStat->append(memStats);
}

uint64_t DRAMSysMemory::access(MemReq& req) {
    futex_lock(&updateLock_);
    if (!isBoundPhase_) {
        scoped_mutex sm(resetMutex_);
        isBoundPhase_ = true;
        for (auto& est : estimators_) {
            est->reset();
        }
    }
    futex_unlock(&updateLock_);

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
            panic("Unexpected request type in DRAMSysMemory::access");
    }

    uint64_t respCycle = req.cycle;

    if (req.type != PUTS && zinfo->eventRecorders[req.srcId]) {
        const bool isWrite = (req.type == PUTX);
        const Address addr = req.lineAddr << lineBits;
        DRAMSysAccEvent* accEv = new (zinfo->eventRecorders[req.srcId]) DRAMSysAccEvent(this, isWrite, addr, domain_, req.srcId);
        accEv->setMinStartCycle(req.cycle - 1);
        accEv->boundLatency = estimators_[req.srcId]->getMemLatency();
        TimingRecord tr = {addr, req.cycle - 1, respCycle, req.type, accEv, accEv};
        zinfo->eventRecorders[req.srcId]->pushRecord(tr);
        respCycle += estimators_[req.srcId]->getMemLatency();
    }

    return respCycle;
}

uint32_t DRAMSysMemory::tick(uint64_t cycle) {
    if (procIdx != 0) {
        return 1;
    }

    pushInFlights();
    drainCompletions();

    curCycle_++;

    const bool leader = sharedRuntime_->onControllerTick(ctrlId_, cycle);
    if (leader) {
        advanceSystemOneCycle();
        sharedRuntime_->finishDramTick(ctrlId_);
    }

    return 1;
}

void DRAMSysMemory::enqueue(DRAMSysAccEvent* accEv, uint64_t cycle) {
    if (isBoundPhase_) {
        isBoundPhase_ = false;
    }

    const DRAMSysWrapper::Request req = {
        static_cast<uint64_t>(accEv->addr),
        accEv->isWrite(),
        accEv->coreid,
        nextReqTag_++
    };

    if (!wrapper_->trySend(req)) {
        reissuedAccesses.inc();
        notQueuedRequests_.emplace_back(accEv);
    } else {
        accEv->reqTag = req.reqTag;
        accEv->issueCycle = curCycle_;
        accEv->dramIssueCycle = sharedRuntime_->currentDramCycle();
        coreTracker_.recordIssue(accEv->coreid, accEv->issueCycle, accEv->sCycle, accEv->isWrite());
        inflightRequests_.insert({accEv->reqTag, accEv});
    }

    accEv->hold();
    (void)cycle;
}

void DRAMSysMemory::pushInFlights() {
    int numberOfTries = 0;
    for (auto it = notQueuedRequests_.begin(); it != notQueuedRequests_.end();) {
        DRAMSysAccEvent* accEv = *it;
        if (accEv->sCycle > curCycle_) {
            ++it;
            continue;
        }
        if (numberOfTries == 8) {
            break;
        }
        numberOfTries++;

        const DRAMSysWrapper::Request req = {
            static_cast<uint64_t>(accEv->addr),
            accEv->isWrite(),
            accEv->coreid,
            nextReqTag_++
        };

        if (wrapper_->trySend(req)) {
            if (!accEv->isWrite()) {
                profTotalSkewLat.inc(curCycle_ - accEv->sCycle);
            }
            accEv->reqTag = req.reqTag;
            accEv->issueCycle = curCycle_;
            accEv->dramIssueCycle = sharedRuntime_->currentDramCycle();
            coreTracker_.recordIssue(accEv->coreid, accEv->issueCycle, accEv->sCycle, accEv->isWrite());
            inflightRequests_.insert({accEv->reqTag, accEv});
            it = notQueuedRequests_.erase(it);
        } else {
            ++it;
        }
    }
}

void DRAMSysMemory::drainCompletions() {
    DRAMSysWrapper::Completion completion{};
    while (wrapper_->popCompletion(completion)) {
        auto range = inflightRequests_.equal_range(completion.reqTag);
        auto it = range.first;
        if (range.first == range.second) {
            panic("DRAMSysMemory completion for unknown reqTag=%lu", completion.reqTag);
        }

        DRAMSysAccEvent* accEv = it->second;
        const uint32_t lat = curCycle_ + 1 - accEv->sCycle;
        const uint32_t serviceLat = curCycle_ + 1 - accEv->issueCycle;

        if (accEv->isWrite()) {
            profWrites.inc();
            profTotalWrLat.inc(lat);
        } else {
            profReads.inc();
            profTotalRdLat.inc(lat);
            profTotalRdLatBound.inc(accEv->boundLatency);
            uint32_t absErr = 100 * ((lat > accEv->boundLatency) ? (lat - accEv->boundLatency) : (accEv->boundLatency - lat));
            absErr /= lat;
            profTotalAbsError.inc(absErr);
            profTotalAbsErrorCounter.inc();
            estimators_[accEv->coreid]->updateModel(lat);
        }

        coreTracker_.recordComplete(accEv->coreid, lat, serviceLat, accEv->isWrite());
        accEv->release();
        accEv->done(curCycle_ + 1);
        inflightRequests_.erase(it);
    }
}

void DRAMSysMemory::registerController(DRAMSysMemory* ctrl,
                                       uint32_t numControllers,
                                       uint64_t cpuPsPerClk,
                                       uint64_t dramPsPerClk) {
    futex_lock(&sharedRuntimeLock_);
    if (!sharedRuntime_) {
        sharedRuntime_ = new DRAMSysRuntime(numControllers, cpuPsPerClk, dramPsPerClk, 0, 0);
    } else if (sharedRuntime_->numControllers() != numControllers) {
        futex_unlock(&sharedRuntimeLock_);
        panic("DRAMSysMemory controller count mismatch (existing=%u new=%u)",
              sharedRuntime_->numControllers(), numControllers);
    } else if (sharedRuntime_->cpuPsPerClk() != cpuPsPerClk ||
               sharedRuntime_->dramPsPerClk() != dramPsPerClk) {
        futex_unlock(&sharedRuntimeLock_);
        panic("DRAMSysMemory timing mismatch: runtime(cpuPs=%" PRIu64 " dramPs=%" PRIu64 ") new(cpuPs=%" PRIu64 " dramPs=%" PRIu64 ")",
              sharedRuntime_->cpuPsPerClk(), sharedRuntime_->dramPsPerClk(),
              cpuPsPerClk, dramPsPerClk);
    }
    controllers_.push_back(ctrl);
    sharedRuntimeUsers_++;
    futex_unlock(&sharedRuntimeLock_);
}

void DRAMSysMemory::unregisterController(DRAMSysMemory* ctrl) {
    futex_lock(&sharedRuntimeLock_);
    auto it = std::find(controllers_.begin(), controllers_.end(), ctrl);
    if (it != controllers_.end()) {
        controllers_.erase(it);
    }
    if (sharedRuntimeUsers_ > 0) {
        sharedRuntimeUsers_--;
    }
    if (sharedRuntimeUsers_ == 0) {
        delete sharedRuntime_;
        sharedRuntime_ = nullptr;
    }
    futex_unlock(&sharedRuntimeLock_);
}

void DRAMSysMemory::advanceSystemOneCycle() {
    futex_lock(&sharedRuntimeLock_);
    if (!controllers_.empty()) {
        // SystemC uses a global simulation context; one sc_start() advances all instantiated modules.
        controllers_.front()->wrapper_->advanceOneCycle();
    }
    futex_unlock(&sharedRuntimeLock_);
}

#endif  // _WITH_DRAMSYS_

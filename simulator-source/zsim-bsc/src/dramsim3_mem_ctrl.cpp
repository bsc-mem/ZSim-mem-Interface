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


#ifdef _WITH_DRAMSIM3_ //was compiled with dramsim3

#include <map>
#include <string>
#include <fstream>
#include <algorithm>
#include <sstream>

#include "zsim.h"
#include "dramsim3_mem_ctrl.h"
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "galloc.h"
#include "dramsim3.h"

class DRAMsim3AccEvent : public TimingEvent
{
  private:
    DRAMsim3Memory *dram;
    bool write;

  public:
    uint64_t sCycle;
    Address addr;
    uint32_t coreid;

    uint64_t dramIssueCycle;
    uint64_t issueCycle;
    uint32_t boundLatency; // latency estimated by the bound phase estimator


    DRAMsim3AccEvent(DRAMsim3Memory *_dram, bool _write, Address _addr, int32_t domain, uint32_t _coreid) : TimingEvent(0, 0, domain), dram(_dram), write(_write), addr(_addr), coreid(_coreid) {}

    bool isWrite() const
    {
        return write;
    }

    // push dramsim3 queue
    void simulate(uint64_t startCycle)
    {
        sCycle = startCycle;

        dram->enqueue(this, startCycle);
    }
};

DRAMsim3Memory::DRAMsim3Memory(const g_string& name, uint32_t domain, int cpuFreqMHz,
    const std::string& configPath, const std::string& outputDir, g_vector<IBoundMemLatencyEstimator*> estimators,
    bool dumpTrace /*= false */, const std::vector<uint32_t>& trackedCores /* = {} */ )
    : name(name), domain(domain), estimators(estimators), dump_trace(dumpTrace) {

    curCycle = 0;
    dramCycle = 0;
    dramPs = 0;
    cpuPs = 0;
    lstFinished = 0;

    // NOTE: this will alloc DRAM on the heap and not the glob_heap, make sure only one process ever handles this
    callBackFn = std::bind(&DRAMsim3Memory::onReadComplete, this, std::placeholders::_1);

    // For some reason you cannot "new" here because zsim seems to override this "new"
    // so we have to use the helper function to init the pointer
    // wrapper = new dramsim3::MemorySystem(configPath.c_str(), outputDir.c_str(), callBackFn, callBackFn);
    wrapper = dramsim3::GetMemorySystem(configPath, outputDir, callBackFn, callBackFn);
    double tCK = wrapper->GetTCK();

    dramPsPerClk = static_cast<uint64_t>(tCK*1000);
    cpuPsPerClk = static_cast<uint64_t>(1000000. / cpuFreqMHz);
    assert(cpuPsPerClk < dramPsPerClk);
    TickEvent<DRAMsim3Memory> *tickEv = new TickEvent<DRAMsim3Memory>(this, domain);
    tickEv->queue(0); // start the sim at time 0

    if (dump_trace) {
        dump_trace_file.open(outputDir + "/" + name.c_str() + ".trace");
        if (!dump_trace_file.is_open()) {
            panic("can't open trace dump file");
        }
    }


    // dump_est_file.open(outputDir + "/" + name.c_str() + "_est.txt");
    // if (!dump_est_file.is_open()) {
    //     panic("can't open est dump file");
    // }
    // Custom addition BEGIN: configure per-core tracker
    coreTracker.configure(trackedCores, zinfo->numCores);
    // Custom addition END: configure per-core tracker
}

DRAMsim3Memory::~DRAMsim3Memory() {
    // Release any held timing events so their allocators can reclaim memory.
    for (auto& entry : inflightRequests) {
        DRAMsim3AccEvent* accEv = entry.second;
        if (accEv) {
            accEv->release();
            accEv->done(curCycle);
        }
    }
    inflightRequests.clear();

    for (auto* accEv : notQueuedRequests) {
        if (accEv) {
            accEv->release();
            accEv->done(curCycle);
        }
    }
    notQueuedRequests.clear();

    if (dump_trace_file.is_open()) {
        dump_trace_file.close();
    }

    delete wrapper;
    wrapper = nullptr;
}

void DRAMsim3Memory::initStats(AggregateStat *parentStat)
{
    AggregateStat* memStats = new AggregateStat();
    memStats->init(name.c_str(), "Memory controller stats");
    profReads.init("rd", "Read requests"); memStats->append(&profReads);
    profWrites.init("wr", "Write requests"); memStats->append(&profWrites);
    profIssuedReads.init("issuedRd", "Issued read accesses"); memStats->append(&profIssuedReads);
    profIssuedWrites.init("issuedWr", "Issued write accesses"); memStats->append(&profIssuedWrites);
    profTotalRdLat.init("rdlat", "Total latency experienced by read requests"); memStats->append(&profTotalRdLat);
    profTotalRdLatBound.init("rdlatbound", "Total bound latency experienced by read requests"); memStats->append(&profTotalRdLatBound);

    profTotalWrLat.init("wrlat", "Total latency experienced by write requests"); memStats->append(&profTotalWrLat);
    profTotalSkewLat.init("est_skew_lat", "Total latency experienced by requests in wait queue"); memStats->append(&profTotalSkewLat);
    reissuedAccesses.init("reissuedAccesses", "Number of accesses that were reissued due to full queue"); memStats->append(&reissuedAccesses);
    queueNotFull.init("queueNotFull", "Number of cycles where queue is not full"); memStats->append(&queueNotFull);
    coreTracker.registerStats(memStats, "trackCores");
    profTotalAbsError.init("totalAbsError", "Total absolute error between bound and weave latencies");
    memStats->append(&profTotalAbsError);
    profTotalAbsErrorCounter.init("totalAbsErrorCounter", "Total number of samples for absolute error of bound latency");
    memStats->append(&profTotalAbsErrorCounter);
    parentStat->append(memStats);
}

uint64_t DRAMsim3Memory::access(MemReq &req)
{
    futex_lock(&updateLock);

    if (!is_bound_phase) {
        scoped_mutex sm(reset_mutex);
        is_bound_phase = true;
        for (auto& est: estimators) {
            est->reset();
        }
    }
    futex_unlock(&updateLock);

    // NOTE so you basicall cannot access draoCore->*
    // in this function (or this phase I assume) otherwise
    // you break some weird memory and pin will try to kill you, like, what?
    if (req.type == GETS || req.type == GETX) {
        profIssuedReads.atomicInc();
    } else if (req.type == PUTX) {
        profIssuedWrites.atomicInc();
    }

    switch (req.type)
    {
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

    EventRecorder* evRec = zinfo->eventRecorders[req.srcId];
    bool trackAccess = (req.type != PUTS /*discard clean writebacks*/) && evRec;
    if (trackAccess) {
        Address addr = req.lineAddr << lineBits;
        bool isWrite = (req.type == PUTX);
        // TODO: check the new
        DRAMsim3AccEvent* accEv = new (evRec) DRAMsim3AccEvent(this, isWrite, addr, domain, req.srcId);
        accEv->setMinStartCycle(req.cycle - 1);
        accEv->boundLatency = estimators[req.srcId]->getMemLatency();
        TimingRecord tr = {addr, req.cycle - 1, respCycle, req.type, accEv, accEv};
        evRec->pushRecord(tr);

        // if (req.is(MemReq::PREFETCH_NOREC)) {
        //     // Prefetches should not enqueue timing records on the requesting core.
        //     // Queue the event directly so DRAMSim3 still sees the transaction.
        //     memEv->queue(req.cycle - 1);
        // } else {
        //     TimingRecord tr = {addr, req.cycle - 1, respCycle, req.type, memEv, memEv};
        //     evRec->pushRecord(tr);
        // }

        respCycle += estimators[req.srcId]->getMemLatency();
    }

    return respCycle;
}

// used by TickEvent
uint32_t DRAMsim3Memory::tick(uint64_t /*cycle*/) {
    // Only the master process should drive the external DRAM model
    if (procIdx != 0) return 1;
    pushInFlights();

    cpuPs += cpuPsPerClk;
    curCycle++;

    if (cpuPs > dramPs) {
        wrapper->ClockTick();
        dramPs += dramPsPerClk;
        dramCycle++;
    }
    if (cpuPs == dramPs) {  // reset to prevent overflow
        cpuPs = 0;
        dramPs = 0;
    }
    return 1;
}

void DRAMsim3Memory::enqueue(DRAMsim3AccEvent* accEv, uint64_t /*cycle*/) {
    if (is_bound_phase) {
        is_bound_phase = false;
    }

    if (wrapper->WillAcceptTransaction(accEv->addr, accEv->isWrite())) {
        // push to dramsim3 queue 
        wrapper->AddTransaction(accEv->addr, accEv->isWrite());

        accEv->dramIssueCycle = dramCycle;
        accEv->issueCycle = curCycle;

        // update stats
        coreTracker.recordIssue(accEv->coreid, accEv->issueCycle, accEv->sCycle, accEv->isWrite());
        if (dump_trace) dumpTransaction(accEv, std::to_string(accEv->coreid));

        queueNotFull.inc(curCycle - lstFinished);
        inflightRequests.insert(std::pair<Address, DRAMsim3AccEvent*>(accEv->addr, accEv));
    } else {
        reissuedAccesses.inc();
        notQueuedRequests.emplace_back(accEv);
    }

    accEv->hold();
}

void DRAMsim3Memory::onReadComplete(uint64_t addr) {
    // sanity: request should now be present among inflight
    auto it = inflightRequests.find(addr);
    assert((it != inflightRequests.end()));

    DRAMsim3AccEvent* accEv = it->second;

    lstFinished = curCycle;
    const uint64_t lat = curCycle + 1 - accEv->sCycle;
    if (accEv->isWrite()) {
        profWrites.inc();
        profTotalWrLat.inc(lat);
    } else {
        profReads.inc();
        profTotalRdLat.inc(lat);
        profTotalRdLatBound.inc(accEv->boundLatency);

        int32_t absErr = 100*((lat>accEv->boundLatency)?(lat-accEv->boundLatency) : (accEv->boundLatency-lat));

        // std::abs(static_cast<int32_t>(lat) - static_cast<int32_t>(ev->boundLatency));
        absErr /= lat;
        // printf("Ramulator: core %d, addr 0x%lx, weave lat %u, bound lat %u, abs error %u%%\n",
            // accEv->coreid, accEv->addr, lat, accEv->boundLatency, absErr);
        profTotalAbsError.inc(absErr);
        profTotalAbsErrorCounter.inc();
        
        // update the estimator model (only for read because most dram simulator call the write callback as soon as it
        // reaches the queue)


        estimators[accEv->coreid]->updateModel(lat);
    }

    coreTracker.recordComplete(accEv->coreid, lat, curCycle + 1 - accEv->issueCycle, accEv->isWrite());
    accEv->release();
    accEv->done(curCycle + 1);
    inflightRequests.erase(it);

}

void DRAMsim3Memory::onWriteComplete(uint64_t addr) {
    // Same as read for now
    onReadComplete(addr);
}

void DRAMsim3Memory::pushInFlights() {

    int numberOfTries = 0;
    for (auto it = notQueuedRequests.begin(); it != notQueuedRequests.end();) {
        auto* accEv = *it;
        if(numberOfTries==8) {
            break;
        }
        numberOfTries++;

        if (accEv->sCycle > curCycle || !wrapper->WillAcceptTransaction(accEv->addr, accEv->isWrite())) {
            ++it;
            continue;
        }

        if (!accEv->isWrite()) {
            // only for reads
            profTotalSkewLat.inc(curCycle - accEv->sCycle);
        }

        accEv->issueCycle = curCycle;
        accEv->dramIssueCycle = dramCycle;

        wrapper->AddTransaction(accEv->addr, accEv->isWrite());
        coreTracker.recordIssue(accEv->coreid, accEv->issueCycle, accEv->sCycle, accEv->isWrite());
        inflightRequests.insert({ accEv->addr, accEv });
        if (dump_trace) dumpTransaction(accEv, std::to_string(accEv->coreid));

        it = notQueuedRequests.erase(it);
    }
}

void DRAMsim3Memory::printStats() {
    wrapper->PrintStats();
}

void DRAMsim3Memory::dumpTransaction(DRAMsim3AccEvent* accEv, std::string tag /*= "" */) {
    std::ostringstream oss;
    oss << "0x" << std::hex << accEv->addr << (accEv->isWrite() ? " WRITE " : " READ ") << std::dec << accEv->dramIssueCycle;
    if (!tag.empty()) oss << " #[TAG]: " << tag;
    oss << std::endl;
    dump_trace_file << oss.str();
}

#endif

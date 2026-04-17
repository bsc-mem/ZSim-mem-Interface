#ifndef RAMULATOR2_MEM_CTRL_H_
#define RAMULATOR2_MEM_CTRL_H_

// #define _WITH_RAMULATOR2_
#ifdef _WITH_RAMULATOR2_
#include <string>
#include <vector>
#include "g_std/g_string.h"
#include "g_std/g_multimap.h"
#include "g_std/g_vector.h"
#include "memory_hierarchy.h"
#include "pad.h"
#include "stats.h"
#include "mem_core_tracker.h"

#include "bound_mem_latency.h"
#include "ramulator2_wrapper.h"
#include "base/request.h"

class Ramulator2AccEvent;
class Ramulator2 : public MemObject { // one Ramulator controller
private:
    g_string name;
    uint32_t domain;
    g_vector<IBoundMemLatencyEstimator*> estimators;

    uint32_t cpuFreq;
    double tCK;
    double memFreq;
    unsigned freqRatio;
    unsigned long long tickCounter = 0;
    Ramulator::Ramulator2Wrapper* wrapper;

    bool isBoundPhase = false;
    g_multimap<int64_t, Ramulator2AccEvent*> inflightRequests;

    uint64_t curCycle; // processor cycle, used in callbacks
    uint64_t dramCycle;


    // R/W stats
    PAD();
    Counter profReads;
    Counter profWrites;
    Counter profTotalAbsError;
    Counter profTotalAbsErrorCounter;
    Counter profTotalRdLatBound;
    Counter profTotalRdLat;
    Counter profTotalWrLat;
    Counter profTotalSkewLat;
    Counter reissuedAccesses;
    PAD();

public:
    Ramulator2(std::string config_path, g_vector<IBoundMemLatencyEstimator*> _estimators, unsigned num_cpus,
        uint32_t _domain, unsigned _cpuFreq, bool _record_memory_trace, const g_string& name, const std::vector<uint32_t>& trackedCores = {});
    ~Ramulator2();

    void finish();

    const char* getName() { return name.c_str(); }
    void initStats(AggregateStat* parentStat);

    // Record accesses
    uint64_t access(MemReq& req);

    // Event-driven simulation (phase 2)
    uint32_t tick(uint64_t cycle);
    void enqueue(Ramulator2AccEvent* ev, uint64_t cycle);

private:
    uint64_t dramPsPerClk, cpuPsPerClk;
    uint64_t dramPs, cpuPs;
    lock_t updateLock;

    // Completion handler without depending on Ramulator2 Request type
    void handle_completion(uint64_t addr, int type_id, int source_id);
    void DRAM_read_return_cb(Ramulator::Request& req);
    void DRAM_write_return_cb(Ramulator::Request& req);

    g_vector<Ramulator2AccEvent*> notQueuedRequests;

    Ramulator::Request getRequestFromAccEvent(Ramulator2AccEvent* ev);
    void pushInFlights();
    MemCoreTracker coreTracker;
};
#endif // _WITH_RAMULATOR2_

#endif // RAMULATOR2_MEM_CTRL_H_

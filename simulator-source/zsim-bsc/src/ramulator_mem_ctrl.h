#ifndef RAMULATOR_MEM_CTRL_H_
#define RAMULATOR_MEM_CTRL_H_

#ifdef _WITH_RAMULATOR_
#include <string>
#include <vector>
#include <functional>

#include "g_std/g_string.h"
#include "g_std/g_vector.h"
#include "g_std/g_multimap.h"
#include "memory_hierarchy.h"
#include "mutex.h"
#include "pad.h"
#include "stats.h"
#include "bound_mem_latency.h"
#include "mem_core_tracker.h"

namespace ramulator {
  class Request;
  class RamulatorWrapper;
};

namespace Stats_ramulator {
  class StatList;
}

class RamulatorAccEvent;
class Ramulator : public MemObject { //one Ramulator controller
  private:
    g_string name;
    uint32_t domain;
    g_vector<IBoundMemLatencyEstimator*> estimators;

    uint32_t cpuFreq;
    double tCK;
    double memFreq;
    unsigned freqRatio;
    bool pim_mode;
    g_string application_name;
    ramulator::RamulatorWrapper* wrapper;
    Stats_ramulator::StatList* statList;

    g_multimap<uint64_t, RamulatorAccEvent*> inflightRequests;

    uint64_t curCycle;  // processor cycle, for ticking sync
    uint64_t dramCycle; // dram cycle, for ticking sync

    bool isBoundPhase = false; // update estimator status

    mutex reset_mutex;

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
    Ramulator(std::string config_file, unsigned num_cpus, unsigned cache_line_size, uint32_t _minLatency, uint32_t _domain, const g_string& _name, bool pim_mode,  const std::string& application, unsigned _cpuFreq, bool _record_memory_trace, bool networkOverhead, const std::vector<uint32_t>& trackedCores = {});
    Ramulator(std::string config_file, g_vector<IBoundMemLatencyEstimator*> _estimators, unsigned num_cpus, unsigned cache_line_size, uint32_t _domain, const g_string& _name, bool pim_mode,  const std::string& application, unsigned _cpuFreq, bool _record_memory_trace, bool networkOverhead, const std::vector<uint32_t>& trackedCores = {});
    ~Ramulator();
    void finish();

    const char* getName() {return name.c_str();}
    void initStats(AggregateStat* parentStat);

    // Record accesses
    uint64_t access(MemReq& req);

    // Event-driven simulation (phase 2)
    uint32_t tick(uint64_t cycle);
    void enqueue(RamulatorAccEvent* ev, uint64_t cycle);

  private:
    uint64_t dramPsPerClk, cpuPsPerClk;
    uint64_t dramPs, cpuPs;
    std::function<void(ramulator::Request&)> read_cb_func;
	  std::function<void(ramulator::Request&)> write_cb_func;

    lock_t updateLock;

    void DRAM_read_return_cb(ramulator::Request&);
    void DRAM_write_return_cb(ramulator::Request&);

    g_vector<RamulatorAccEvent*> notQueuedRequests;


    ramulator::Request getRequestFromAccEvent(RamulatorAccEvent* ev);
    void pushInFlights();
    MemCoreTracker coreTracker;
};

#endif // _WITH_RAMULATOR_

#endif  // RAMULATOR_MEM_CTRL_H_

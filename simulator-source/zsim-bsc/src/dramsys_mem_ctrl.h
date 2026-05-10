#ifndef DRAMSYS_MEM_CTRL_H_
#define DRAMSYS_MEM_CTRL_H_

#ifdef _WITH_DRAMSYS_

#include <memory>
#include <string>
#include <vector>

#include "bound_mem_latency.h"
#include "dramsys_runtime.h"
#include "g_std/g_multimap.h"
#include "g_std/g_string.h"
#include "g_std/g_vector.h"
#include "mem_core_tracker.h"
#include "memory_hierarchy.h"
#include "mutex.h"
#include "pad.h"
#include "stats.h"

class DRAMSysAccEvent;
class DRAMSysWrapper;

/*
 * DRAMSysMemory
 * -------------
 * zsim-facing memory controller implementation for DRAMSys backend.
 *
 * Design:
 * - Keeps the same MemObject API used by other backends (access/enqueue/tick).
 * - Uses DRAMSysWrapper for request/response protocol adaptation.
 * - Uses DRAMSysRuntime barrier so backend clock advances exactly once per
 *   DRAM cycle after all controllers are ready.
 */
class DRAMSysMemory : public MemObject {
  public:
    DRAMSysMemory(const g_string& name,
                  uint32_t domain,
                  uint32_t ctrlId,
                  uint32_t numControllers,
                  unsigned cpuFreqMHz,
                  const std::string& configPath,
                  g_vector<IBoundMemLatencyEstimator*> estimators,
                  const std::vector<uint32_t>& trackedCores = {});
    ~DRAMSysMemory();

    const char* getName() { return name_.c_str(); }
    void initStats(AggregateStat* parentStat);
    uint64_t access(MemReq& req);
    uint32_t tick(uint64_t cycle);
    void enqueue(DRAMSysAccEvent* accEv, uint64_t cycle);

  private:
    static void registerController(DRAMSysMemory* ctrl,
                                   uint32_t numControllers,
                                   uint64_t cpuPsPerClk,
                                   uint64_t dramPsPerClk);
    static void unregisterController(DRAMSysMemory* ctrl);
    static void advanceSystemOneCycle();

    void pushInFlights();
    void drainCompletions();

    g_string name_;
    uint32_t domain_;
    uint32_t ctrlId_;

    g_vector<IBoundMemLatencyEstimator*> estimators_;

    std::unique_ptr<DRAMSysWrapper> wrapper_;

    // Request tracking state.
    uint64_t nextReqTag_;
    g_multimap<uint64_t, DRAMSysAccEvent*> inflightRequests_;
    g_vector<DRAMSysAccEvent*> notQueuedRequests_;

    // Local cycle tracking for latency accounting.
    uint64_t curCycle_;

    bool isBoundPhase_;
    mutex resetMutex_;
    lock_t updateLock_;

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

    MemCoreTracker coreTracker_;

    // Shared runtime and controller registry for barrier-driven ticking.
    static DRAMSysRuntime* sharedRuntime_;
    static lock_t sharedRuntimeLock_;
    static uint32_t sharedRuntimeUsers_;
    static g_vector<DRAMSysMemory*> controllers_;
};

#endif  // _WITH_DRAMSYS_

#endif  // DRAMSYS_MEM_CTRL_H_

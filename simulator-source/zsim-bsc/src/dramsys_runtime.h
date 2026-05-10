#ifndef DRAMSYS_RUNTIME_H_
#define DRAMSYS_RUNTIME_H_

#ifdef _WITH_DRAMSYS_

#include <stdint.h>

#include "g_std/g_vector.h"
#include "g_std/stl_galloc.h"
#include "locks.h"

/*
 * DRAMSysRuntime
 * --------------
 * Coordinates a cycle-level barrier for all zsim-side DRAMSys controllers.
 *
 * Why this class exists:
 * - Multiple memory controllers call enqueue/push logic independently.
 * - DRAMSys backend clock must advance exactly once per simulated cycle.
 * - Advancing on the first caller creates order bias (late callers miss a cycle).
 *
 * This runtime enforces a two-phase protocol:
 *   1) Controllers arrive at a shared CPU-cycle barrier via onControllerTick().
 *   2) The last controller updates shared cpu/dram time and, if needed, becomes
 *      the DRAM-tick leader for that cycle.
 *   3) Leader advances backend clock once, then calls finishDramTick().
 *
 * Guarantees:
 * - At most one leader per cycle.
 * - Duplicate onControllerTick() from the same controller/cycle is ignored.
 * - Calls for future cycles are rejected (panic), preventing desync.
 */
class DRAMSysRuntime : public GlobAlloc {
  public:
    explicit DRAMSysRuntime(uint32_t numControllers = 0,
                            uint64_t cpuPsPerClk = 0,
                            uint64_t dramPsPerClk = 0,
                            uint64_t startCpuCycle = 0,
                            uint64_t startDramCycle = 0);

    /*
     * Reinitialize runtime state.
     * Safe to call during setup/reset, before controllers start ticking.
     */
    void reset(uint32_t numControllers,
               uint64_t cpuPsPerClk,
               uint64_t dramPsPerClk,
               uint64_t startCpuCycle = 0,
               uint64_t startDramCycle = 0);

    /*
     * Mark one controller as ready for the specified zsim CPU cycle.
     *
     * Returns true only for the controller elected as the CPU-cycle leader
     * when a DRAM tick is due. That leader must call finishDramTick().
     */
    bool onControllerTick(uint32_t ctrlId, uint64_t cpuCycle);

    /*
     * Complete one DRAM tick after leader has advanced SystemC once.
     * Only the leader returned by onControllerTick() may call this.
     */
    void finishDramTick(uint32_t ctrlId);

    uint64_t currentCpuCycle() const;
    uint64_t currentDramCycle() const;
    uint32_t numControllers() const { return numControllers_; }
    uint64_t cpuPsPerClk() const { return cpuPsPerClk_; }
    uint64_t dramPsPerClk() const { return dramPsPerClk_; }

  private:
    static constexpr uint32_t kInvalidCtrlId = (uint32_t)-1;

    uint32_t numControllers_;
    uint64_t cpuPsPerClk_;
    uint64_t dramPsPerClk_;

    // Shared global time state.
    uint64_t currentCpuCycle_;
    uint64_t currentDramCycle_;
    uint64_t cpuPs_;
    uint64_t dramPs_;

    // CPU-cycle barrier state (all controllers must arrive before a shared update).
    uint64_t cpuEpoch_;
    g_vector<uint64_t> cpuReadyEpoch_;
    uint32_t cpuReadyCount_;

    // True between leader election and finishDramTick() when a DRAM tick is due.
    bool dramTickPending_;
    uint32_t dramTickLeader_;

    mutable lock_t stateLock_;
};

#endif  // _WITH_DRAMSYS_

#endif  // DRAMSYS_RUNTIME_H_

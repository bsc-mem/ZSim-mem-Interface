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

#ifndef DRAMSIM3_MEM_CTRL_H_
#define DRAMSIM3_MEM_CTRL_H_

#ifdef _WITH_DRAMSIM3_

#include <functional>
#include <string>
#include <fstream>
#include <vector>

#include "mem_core_tracker.h"

#include "g_std/g_string.h"
#include "g_std/g_unordered_map.h"
#include "g_std/g_multimap.h"
#include "g_std/g_vector.h"
#include "memory_hierarchy.h"
#include "mutex.h"
#include "pad.h"
#include "stats.h"

#include "dramsim3.h"
#include "bound_mem_latency.h"

class DRAMsim3AccEvent;

class DRAMsim3Memory : public MemObject
{
  private:
    g_string name;
    uint32_t domain;
    g_vector<IBoundMemLatencyEstimator*> estimators;
    dramsim3::MemorySystem* dramCore;
    std::ofstream dump_trace_file;
    std::ofstream dump_est_file;

    g_multimap<uint64_t, DRAMsim3AccEvent *> inflightRequests;
    g_vector<DRAMsim3AccEvent *> notQueuedRequests; // FIXME: use multiple queue based on ctrl prevents iterating 

    uint64_t curCycle; //processor cycle, used in callbacks
    uint64_t dramCycle;

    bool is_bound_phase = false;

    mutex reset_mutex;
    bool dump_trace;

    // R/W stats
    PAD();
    Counter profReads;
    Counter profWrites;
    Counter profIssuedReads;
    Counter profIssuedWrites;
    Counter profTotalAbsError;
    Counter profTotalAbsErrorCounter;
    Counter profTotalRdLat;
    Counter profTotalRdLatBound; 
    Counter profTotalWrLat;
    Counter profTotalOsLat;
    Counter profTotalSkewLat;
    Counter reissuedAccesses;
    Counter queueNotFull;
    PAD();

  public:
    DRAMsim3Memory(std::string &ConfigName, std::string &OutputDir,
                   int cpuFreqMHz, uint32_t _domain, const g_string &_name, const std::vector<uint32_t>& trackedCores = {});

    DRAMsim3Memory(std::string& ConfigName, std::string& OutputDir, g_vector<IBoundMemLatencyEstimator*> _estimators,
        int cpuFreqMHz, uint32_t _domain, const g_string& _name, const bool _dump_trace = false, const std::vector<uint32_t>& trackedCores = {});
    ~DRAMsim3Memory();

    const char *getName() { return name.c_str(); }

    void initStats(AggregateStat *parentStat);

    // Record accesses
    uint64_t access(MemReq &req);

    // Event-driven simulation (phase 2)
    uint32_t tick(uint64_t cycle);
    void enqueue(DRAMsim3AccEvent *ev, uint64_t cycle);

    void printStats();

private:
    void DRAM_read_return_cb(uint64_t addr);
    void DRAM_write_return_cb(uint64_t addr);
    std::function<void(uint64_t)> callBackFn;
    uint64_t dramPsPerClk, cpuPsPerClk;
    uint64_t dramPs, cpuPs;
    uint64_t lstFinished;
    int tCL;

    lock_t updateLock;

    void pushInFlights();
    void dumpTransaction(DRAMsim3AccEvent *ev, std::string tag = "");
    MemCoreTracker coreTracker;
};

#endif // _WITH_DRAMSIM3_
#endif // DRAMSIM3_MEM_CTRL_H_

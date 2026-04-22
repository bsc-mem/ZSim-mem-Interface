#ifdef _WITH_DRAMSYS_

#include "dramsys_runtime.h"

#include <inttypes.h>

#include "log.h"

DRAMSysRuntime::DRAMSysRuntime(uint32_t numControllers,
                               uint64_t cpuPsPerClk,
                               uint64_t dramPsPerClk,
                               uint64_t startCpuCycle,
                               uint64_t startDramCycle)
    : numControllers_(0),
      cpuPsPerClk_(0),
      dramPsPerClk_(0),
      currentCpuCycle_(0),
      currentDramCycle_(0),
      cpuPs_(0),
      dramPs_(0),
      cpuEpoch_(1),
      cpuReadyCount_(0),
      dramTickPending_(false),
      dramTickLeader_(kInvalidCtrlId),
      stateLock_(0) {
    futex_init(&stateLock_);
    reset(numControllers, cpuPsPerClk, dramPsPerClk, startCpuCycle, startDramCycle);
}

void DRAMSysRuntime::reset(uint32_t numControllers,
                           uint64_t cpuPsPerClk,
                           uint64_t dramPsPerClk,
                           uint64_t startCpuCycle,
                           uint64_t startDramCycle) {
    if (numControllers == 0) {
        panic("DRAMSysRuntime::reset called with numControllers == 0");
    }
    if (cpuPsPerClk == 0 || dramPsPerClk == 0) {
        panic("DRAMSysRuntime::reset requires non-zero cpuPsPerClk/dramPsPerClk");
    }
    if (cpuPsPerClk > dramPsPerClk) {
        panic("DRAMSysRuntime::reset invalid periods (cpuPsPerClk=%" PRIu64 " > dramPsPerClk=%" PRIu64 ")",
              cpuPsPerClk, dramPsPerClk);
    }

    futex_lock(&stateLock_);

    numControllers_ = numControllers;
    cpuPsPerClk_ = cpuPsPerClk;
    dramPsPerClk_ = dramPsPerClk;
    currentCpuCycle_ = startCpuCycle;
    currentDramCycle_ = startDramCycle;
    cpuPs_ = 0;
    dramPs_ = 0;

    cpuEpoch_ = 1;
    cpuReadyCount_ = 0;
    dramTickPending_ = false;
    dramTickLeader_ = kInvalidCtrlId;

    cpuReadyEpoch_.clear();
    cpuReadyEpoch_.resize(numControllers_, 0);

    futex_unlock(&stateLock_);
}

bool DRAMSysRuntime::onControllerTick(uint32_t ctrlId, uint64_t cpuCycle) {
    while (true) {
        futex_lock(&stateLock_);

        if (ctrlId >= numControllers_) {
            futex_unlock(&stateLock_);
            panic("DRAMSysRuntime::onControllerTick invalid ctrlId=%u (numControllers=%u)", ctrlId, numControllers_);
        }

        if (cpuCycle < currentCpuCycle_) {
            // Stale call (already completed); ignore.
            futex_unlock(&stateLock_);
            return false;
        }

        if (cpuCycle > currentCpuCycle_) {
            futex_unlock(&stateLock_);
            panic("DRAMSysRuntime::onControllerTick future cpuCycle=%" PRIu64 " while currentCpuCycle=%" PRIu64,
                  cpuCycle, currentCpuCycle_);
        }

        if (dramTickPending_) {
            // A leader from the previous CPU barrier is still committing the shared DRAM tick.
            // Wait until that commit is done so this controller does not race into the next barrier.
            futex_unlock(&stateLock_);
            _mm_pause();
            continue;
        }

        // Same controller marked this CPU cycle twice; ignore idempotently.
        if (cpuReadyEpoch_[ctrlId] == cpuEpoch_) {
            futex_unlock(&stateLock_);
            return false;
        }

        cpuReadyEpoch_[ctrlId] = cpuEpoch_;
        cpuReadyCount_++;

        // CPU-cycle barrier reached: leader performs shared time update decision.
        if (cpuReadyCount_ == numControllers_) {
            cpuPs_ += cpuPsPerClk_;

            const bool dramTickDue = (cpuPs_ > dramPs_);
            if (dramTickDue) {
                dramTickPending_ = true;
                dramTickLeader_ = ctrlId;
            } else if (cpuPs_ == dramPs_) {
                cpuPs_ = 0;
                dramPs_ = 0;
            }

            currentCpuCycle_++;
            cpuEpoch_++;
            cpuReadyCount_ = 0;

            const bool isLeader = dramTickDue;
            futex_unlock(&stateLock_);
            return isLeader;
        }

        futex_unlock(&stateLock_);
        return false;
    }
}

void DRAMSysRuntime::finishDramTick(uint32_t ctrlId) {
    futex_lock(&stateLock_);

    if (!dramTickPending_) {
        futex_unlock(&stateLock_);
        panic("DRAMSysRuntime::finishDramTick called with no DRAM tick pending");
    }

    if (ctrlId != dramTickLeader_) {
        futex_unlock(&stateLock_);
        panic("DRAMSysRuntime::finishDramTick ctrlId=%u is not leader=%u", ctrlId, dramTickLeader_);
    }

    dramPs_ += dramPsPerClk_;
    currentDramCycle_++;
    dramTickPending_ = false;
    dramTickLeader_ = kInvalidCtrlId;

    if (cpuPs_ == dramPs_) {
        cpuPs_ = 0;
        dramPs_ = 0;
    }

    futex_unlock(&stateLock_);
}

uint64_t DRAMSysRuntime::currentCpuCycle() const {
    futex_lock(&stateLock_);
    uint64_t cycle = currentCpuCycle_;
    futex_unlock(&stateLock_);
    return cycle;
}

uint64_t DRAMSysRuntime::currentDramCycle() const {
    futex_lock(&stateLock_);
    uint64_t cycle = currentDramCycle_;
    futex_unlock(&stateLock_);
    return cycle;
}

#endif  // _WITH_DRAMSYS_

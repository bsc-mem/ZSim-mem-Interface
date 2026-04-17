/** Custom addition: shared per-core memory tracking helpers */
#ifndef MEM_CORE_TRACKER_H_
#define MEM_CORE_TRACKER_H_

#include <stdint.h>
#include <string>
#include <vector>

#include "g_std/g_vector.h"
#include "g_std/g_unordered_map.h"
#include "stats.h"
#include "galloc.h"
#include "log.h"

class MemCoreTracker {
  private:
    struct Entry {
        uint32_t coreId;
        bool seenIssue;
        uint64_t lastIssueCycle;
        uint64_t totalReadLatency;
        uint64_t totalMemReadLatency;
        uint64_t readCount;
        uint64_t avgReadLatencyValue;
        uint64_t avgMemReadLatencyValue;
        uint64_t totalWriteLatency;
        uint64_t writeCount;
        uint64_t avgWriteLatencyValue;
        uint64_t totalQueueTime;
        uint64_t queueCount;
        uint64_t avgQueueTimeValue;
        uint64_t totalReadQueueTime;
        uint64_t readQueueCount;
        uint64_t avgReadQueueTimeValue;
        uint64_t totalWriteQueueTime;
        uint64_t writeQueueCount;
        uint64_t avgWriteQueueTimeValue;
        uint64_t maxQueueTimeValue;
        uint64_t totalInterval;
        uint64_t intervalCount;
        uint64_t avgIntervalValue;

        Entry()
            : coreId(0), seenIssue(false), lastIssueCycle(0), totalReadLatency(0), totalMemReadLatency(0), readCount(0), avgReadLatencyValue(0),
              totalWriteLatency(0), writeCount(0), avgWriteLatencyValue(0), totalQueueTime(0), queueCount(0), avgQueueTimeValue(0),
              totalReadQueueTime(0), readQueueCount(0), avgReadQueueTimeValue(0), totalWriteQueueTime(0), writeQueueCount(0),
              avgWriteQueueTimeValue(0), maxQueueTimeValue(0), totalInterval(0), intervalCount(0), avgIntervalValue(0) {}
    };

    bool enabled_;
    g_vector<Entry> entries_;
    g_unordered_map<uint32_t, size_t> index_;

    Entry* getEntry(uint32_t coreId) {
        if (!enabled_) return nullptr;
        auto it = index_.find(coreId);
        if (it == index_.end()) return nullptr;
        return &entries_[it->second];
    }

  public:
    MemCoreTracker() : enabled_(false) {}

    void configure(const std::vector<uint32_t>& cores, uint32_t maxCores) {
        entries_.clear();
        index_.clear();
        enabled_ = false;
        for (uint32_t coreId : cores) {
            if (coreId >= maxCores) {
                warn("trackCores entry %u outside available cores (%u)", coreId, maxCores);
                continue;
            }
            if (index_.find(coreId) != index_.end()) {
                warn("Duplicate trackCores entry %u ignored", coreId);
                continue;
            }
            Entry entry;
            entry.coreId = coreId;
            index_[coreId] = entries_.size();
            entries_.push_back(entry);
        }
        enabled_ = entries_.size() > 0;
    }

    bool enabled() const { return enabled_; }

    void registerStats(AggregateStat* parentStat, const char* scopeName) {
        if (!enabled_ || !parentStat) return;
        AggregateStat* trackedAgg = new AggregateStat();
        trackedAgg->init(scopeName, "Per-core memory interface stats (cycles)");
        parentStat->append(trackedAgg);

        for (Entry& entry : entries_) {
            std::string coreName = "core" + std::to_string(entry.coreId);
            AggregateStat* coreStat = new AggregateStat();
            coreStat->init(gm_strdup(coreName.c_str()), "Tracked core stats");
            trackedAgg->append(coreStat);

            std::string rdCountName = coreName + ".read_count";
            ProxyStat* rdCount = new ProxyStat();
            rdCount->init(gm_strdup(rdCountName.c_str()), "Completed read requests", &entry.readCount);
            coreStat->append(rdCount);

            std::string rdLatName = coreName + ".avg_read_latency";
            ProxyStat* avgRdLat = new ProxyStat();
            avgRdLat->init(gm_strdup(rdLatName.c_str()), "Average read latency (cycles)", &entry.avgReadLatencyValue);
            coreStat->append(avgRdLat);

            std::string rdLatMemName = coreName + ".avg_read_latency_only_mem";
            ProxyStat* avgRdMemLat = new ProxyStat();
            avgRdMemLat->init(gm_strdup(rdLatMemName.c_str()), "Average read latency only from memory (cycles)", &entry.avgMemReadLatencyValue);
            coreStat->append(avgRdMemLat);


            std::string wrLatName = coreName + ".avg_write_latency";
            ProxyStat* avgWrLat = new ProxyStat();
            avgWrLat->init(gm_strdup(wrLatName.c_str()), "Average write latency (cycles)", &entry.avgWriteLatencyValue);
            coreStat->append(avgWrLat);

            std::string wrCountName = coreName + ".write_count";
            ProxyStat* wrCount = new ProxyStat();
            wrCount->init(gm_strdup(wrCountName.c_str()), "Completed write requests", &entry.writeCount);
            coreStat->append(wrCount);

            std::string queueName = coreName + ".avg_queue_time";
            ProxyStat* avgQueue = new ProxyStat();
            avgQueue->init(gm_strdup(queueName.c_str()), "Average enqueue-to-issue delay (cycles)", &entry.avgQueueTimeValue);
            coreStat->append(avgQueue);

            std::string queueRdName = coreName + ".avg_queue_time_reads";
            ProxyStat* avgQueueRd = new ProxyStat();
            avgQueueRd->init(gm_strdup(queueRdName.c_str()), "Average enqueue-to-issue delay for reads (cycles)", &entry.avgReadQueueTimeValue);
            coreStat->append(avgQueueRd);

            std::string queueWrName = coreName + ".avg_queue_time_writes";
            ProxyStat* avgQueueWr = new ProxyStat();
            avgQueueWr->init(gm_strdup(queueWrName.c_str()), "Average enqueue-to-issue delay for writes (cycles)", &entry.avgWriteQueueTimeValue);
            coreStat->append(avgQueueWr);

            std::string maxQueueName = coreName + ".max_queue_time";
            ProxyStat* maxQueue = new ProxyStat();
            maxQueue->init(gm_strdup(maxQueueName.c_str()), "Max enqueue-to-issue delay (cycles)", &entry.maxQueueTimeValue);
            coreStat->append(maxQueue);

            std::string intervalName = coreName + ".avg_request_interval";
            ProxyStat* avgInterval = new ProxyStat();
            avgInterval->init(gm_strdup(intervalName.c_str()), "Average interval between issued requests (cycles)", &entry.avgIntervalValue);
            coreStat->append(avgInterval);
        }
    }

    void recordIssue(uint32_t coreId, uint64_t issueCycle, uint64_t enqueueCycle, bool isWrite) {
        Entry* entry = getEntry(coreId);
        if (!entry) return;
        if (issueCycle > enqueueCycle) {
            uint64_t queueCycles = issueCycle - enqueueCycle;
            entry->totalQueueTime += queueCycles;
            entry->queueCount++;
            entry->avgQueueTimeValue = entry->queueCount ? entry->totalQueueTime / entry->queueCount : 0;
            if (queueCycles > entry->maxQueueTimeValue) {
                entry->maxQueueTimeValue = queueCycles;
            }
            if (isWrite) {
                entry->totalWriteQueueTime += queueCycles;
                entry->writeQueueCount++;
                entry->avgWriteQueueTimeValue = entry->writeQueueCount ? entry->totalWriteQueueTime / entry->writeQueueCount : 0;
            } else {
                entry->totalReadQueueTime += queueCycles;
                entry->readQueueCount++;
                entry->avgReadQueueTimeValue = entry->readQueueCount ? entry->totalReadQueueTime / entry->readQueueCount : 0;
            }
        }
        if (entry->seenIssue) {
            if (issueCycle >= entry->lastIssueCycle) {
                entry->totalInterval += issueCycle - entry->lastIssueCycle;
                entry->intervalCount++;
                entry->avgIntervalValue = entry->intervalCount ? entry->totalInterval / entry->intervalCount : 0;
            }
        } else {
            entry->seenIssue = true;
        }
        entry->lastIssueCycle = issueCycle;
    }

    void recordComplete(uint32_t coreId, uint64_t latency, uint64_t mem_only_latency, bool isWrite) {
        Entry* entry = getEntry(coreId);
        if (!entry) return;
        if (isWrite) {
            entry->totalWriteLatency += latency;
            entry->writeCount++;
            entry->avgWriteLatencyValue = entry->writeCount ? entry->totalWriteLatency / entry->writeCount : 0;
        } else {
            entry->totalReadLatency += latency;
            entry->totalMemReadLatency += mem_only_latency;
            entry->readCount++;
            entry->avgReadLatencyValue = entry->readCount ? entry->totalReadLatency / entry->readCount : 0;
            entry->avgMemReadLatencyValue = entry->readCount ? entry->totalMemReadLatency / entry->readCount : 0;
        }
    }
};

#endif  // MEM_CORE_TRACKER_H_

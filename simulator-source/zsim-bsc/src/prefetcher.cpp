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

#include "prefetcher.h"
#include "bithacks.h"
#include "ooo_core.h"
#include "zsim.h"
#include "timing_event.h"

//#define DBG(args...) info(args)
#define DBG(args...)

namespace {
constexpr uint32_t kLinesPerEntry = StreamPrefetcher::kLinesPerEntry;
constexpr uint32_t kPageShift = StreamPrefetcher::kPageShift;
constexpr uint32_t kPageMask = StreamPrefetcher::kPageMask;
}

void StreamPrefetcher::setParents(uint32_t _childId, const g_vector<MemObject*>& _parents, Network* network) {
    childId = _childId;
    if (_parents.empty()) panic("Must have at least one parent");
    // if (network) panic("Network not handled");
    parents = _parents;
}

void StreamPrefetcher::setChildren(const g_vector<BaseCache*>& children, Network* network) {
    if (children.size() != 1) panic("Must have one children");
    // if (network) panic("Network not handled");
    child = children[0];
}

MemObject* StreamPrefetcher::selectParent(Address lineAddr) const {
    assert(parents.size());
    if (parents.size() == 1) return parents[0];

    // Match the hash used in MESIBottomCC::getParentId so we pick the same bank
    // layout as a cache would when bypassing the prefetcher.
    uint32_t res = 0;
    uint64_t tmp = lineAddr;
    for (uint32_t i = 0; i < 4; i++) {
        res ^= static_cast<uint32_t>(tmp & 0xffff);
        tmp >>= 16;
    }
    return parents[res % parents.size()];
}

void StreamPrefetcher::initStats(AggregateStat* parentStat) {
    AggregateStat* s = new AggregateStat();
    s->init(name.c_str(), "Prefetcher stats");
    profAccesses.init("acc", "Accesses"); s->append(&profAccesses);
    profPrefetches.init("pf", "Issued prefetches"); s->append(&profPrefetches);
    profDoublePrefetches.init("dpf", "Issued double prefetches"); s->append(&profDoublePrefetches);
    profPageHits.init("pghit", "Page/entry hit"); s->append(&profPageHits);
    profHits.init("hit", "Prefetch buffer hits, short and full"); s->append(&profHits);
    profShortHits.init("shortHit", "Prefetch buffer short hits"); s->append(&profShortHits);
    profStrideSwitches.init("strideSwitches", "Predicted stride switches"); s->append(&profStrideSwitches);
    profLowConfAccs.init("lcAccs", "Low-confidence accesses with no prefetches"); s->append(&profLowConfAccs);
    profPrefetchRecorderAttached.init("pfRecAttach", "Prefetches that attached timing events to the recorder"); s->append(&profPrefetchRecorderAttached);
    profPrefetchRecorderBypassed.init("pfRecBypass", "Prefetches that skipped or failed recorder attachment"); s->append(&profPrefetchRecorderBypassed);
    profPrefetchLeadCycles.init("pfLeadCycles", "Cumulative cycles between prefetch issue and demand hit"); s->append(&profPrefetchLeadCycles);
    profPrefetchLeadSamples.init("pfLeadSamples", "Demand accesses that matched an outstanding prefetch"); s->append(&profPrefetchLeadSamples);
    profPageRunLengthTotal.init("pageRunLenTotal", "Total length of completed same-page access runs"); s->append(&profPageRunLengthTotal);
    profPageRunCount.init("pageRunCount", "Number of completed same-page access runs"); s->append(&profPageRunCount);

    auto accuracyFn = [this]() -> uint64_t {
        uint64_t issued = profPrefetches.get();
        if (!issued) return 0;
        return (profHits.get() * 10000) / issued;  // percent * 100 precision
    };
    LambdaStat<decltype(accuracyFn)>* accuracyStat = new LambdaStat<decltype(accuracyFn)>(accuracyFn);
    accuracyStat->init("accuracy_pct_x100", "Prefetch accuracy (useful / issued, percent * 100)");
    s->append(accuracyStat);

    auto coverageFn = [this]() -> uint64_t {
        uint64_t accesses = profAccesses.get();
        if (!accesses) return 0;
        return (profHits.get() * 10000) / accesses;  // percent * 100 precision
    };
    LambdaStat<decltype(coverageFn)>* coverageStat = new LambdaStat<decltype(coverageFn)>(coverageFn);
    coverageStat->init("coverage_pct_x100", "Prefetch coverage (prefetch hits / demand accesses, percent * 100)");
    s->append(coverageStat);

    auto leadFn = [this]() -> uint64_t {
        uint64_t samples = profPrefetchLeadSamples.get();
        if (!samples) return 0;
        return profPrefetchLeadCycles.get() / samples;
    };
    LambdaStat<decltype(leadFn)>* leadStat = new LambdaStat<decltype(leadFn)>(leadFn);
    leadStat->init("pfLead_avg_cycles", "Average cycles between prefetch issue and matching demand");
    s->append(leadStat);

    auto avgRunLenFn = [this]() -> uint64_t {
        uint64_t runs = profPageRunCount.get();
        uint64_t total = profPageRunLengthTotal.get();
        if (hasLastAccessPage && currentPageRunLength) {
            total += currentPageRunLength;
            runs += 1;
        }
        if (!runs) return 0;
        return (total * 100) / runs;
    };
    LambdaStat<decltype(avgRunLenFn)>* avgRunLenStat = new LambdaStat<decltype(avgRunLenFn)>(avgRunLenFn);
    avgRunLenStat->init("avgPageRunLen_x100", "Average consecutive accesses per page before switching (x100)");
    s->append(avgRunLenStat);

    parentStat->append(s);
}

uint64_t StreamPrefetcher::access(MemReq& req) {
    uint32_t origChildId = req.childId;
    req.childId = childId;

    if (req.type != GETS) {
        return selectParent(req.lineAddr)->access(req); //other reqs ignored, including stores
    }

    profAccesses.inc();

    uint64_t reqCycle = req.cycle;

    Address pageAddr = req.lineAddr >> kPageShift;
    if (!hasLastAccessPage) {
        hasLastAccessPage = true;
        lastAccessPage = pageAddr;
        currentPageRunLength = 1;
    } else if (pageAddr == lastAccessPage) {
        currentPageRunLength++;
    } else {
        profPageRunLengthTotal.inc(currentPageRunLength);
        profPageRunCount.inc();
        lastAccessPage = pageAddr;
        currentPageRunLength = 1;
    }

    uint32_t pos = static_cast<uint32_t>(req.lineAddr & kPageMask);
    uint32_t idx = 16;
    // This loop gets unrolled and there are no control dependences. Way faster than a break (but should watch for the avoidable loop-carried dep)
    for (uint32_t i = 0; i < 16; i++) {
        bool match = (pageAddr == tag[i]);
        idx = match?  i : idx;  // ccmov, no branch
    }

    bool wasPrefetched = (idx != 16) && array[idx].valid[pos];
    (void)wasPrefetched;  // keep visible for debugging without affecting release builds

    MemObject* upstream = selectParent(req.lineAddr);
    uint64_t respCycle = upstream->access(req);

    DBG("%s: 0x%lx page %lx pos %d", name.c_str(), req.lineAddr, pageAddr, pos);

    if (idx == 16) {  // entry miss
        uint32_t cand = 16;
        uint64_t candScore = -1;
        //uint64_t candScore = 0;
        for (uint32_t i = 0; i < 16; i++) {
            if (array[i].lastCycle > reqCycle + 500) continue;  // warm prefetches, not even a candidate
            /*uint64_t score = (reqCycle - array[i].lastCycle)*(3 - array[i].conf.counter());
            if (score > candScore) {
                cand = i;
                candScore = score;
            }*/
            if (array[i].ts < candScore) {  // just LRU
                cand = i;
                candScore = array[i].ts;
            }
        }

        if (cand < 16) {
            idx = cand;
            array[idx].alloc(reqCycle);
            array[idx].lastPos = pos;
            array[idx].ts = timestamp++;
            tag[idx] = pageAddr;
        }
        DBG("%s: MISS alloc idx %d", name.c_str(), idx);
    } else {  // entry hit
        profPageHits.inc();
        Entry& e = array[idx];
        array[idx].ts = timestamp++;
        DBG("%s: PAGE HIT idx %d", name.c_str(), idx);

        // 1. Did we prefetch-hit?
        bool shortPrefetch = false;
        if (e.valid[pos]) {
            uint64_t pfRespCycle = e.times[pos].respCycle;
            shortPrefetch = pfRespCycle > respCycle;
            e.valid[pos] = false;  // close, will help with long-lived transactions
            respCycle = MAX(pfRespCycle, respCycle);
            e.lastCycle = MAX(respCycle, e.lastCycle);
            profHits.inc();
            uint64_t issueCycle = e.times[pos].startCycle;
            if (reqCycle >= issueCycle) {
                profPrefetchLeadSamples.inc();
                profPrefetchLeadCycles.inc(reqCycle - issueCycle);
            }
            if (shortPrefetch) profShortHits.inc();
            DBG("%s: pos %d prefetched on %ld, pf resp %ld, demand resp %ld, short %d", name.c_str(), pos, e.times[pos].startCycle, pfRespCycle, respCycle, shortPrefetch);
        }

        // 2. Update predictors, issue prefetches
        int32_t stride = pos - e.lastPos;
        DBG("%s: pos %d lastPos %d lastLastPost %d e.stride %d", name.c_str(), pos, e.lastPos, e.lastLastPos, e.stride);
        if (e.stride == stride) {
            e.conf.inc();
            if (e.conf.pred()) {  // do prefetches
                int32_t fetchDepth = (e.lastPrefetchPos - e.lastPos)/stride;
                uint32_t prefetchPos = e.lastPrefetchPos + stride;
                if (fetchDepth < 1) {
                    prefetchPos = pos + stride;
                    fetchDepth = 1;
                }
                DBG("%s: pos %d stride %d conf %d lastPrefetchPos %d prefetchPos %d fetchDepth %d", name.c_str(), pos, stride, e.conf.counter(), e.lastPrefetchPos, prefetchPos, fetchDepth);

                OOOCore* issuingOOO = nullptr;
                EventRecorder* coreEvRec = nullptr;
                bool canRecordPrefetch = false;
                if (zinfo && req.srcId < zinfo->numCores) {
                    Core* srcCore = zinfo->cores[req.srcId];
                    issuingOOO = dynamic_cast<OOOCore*>(srcCore);
                    if (issuingOOO) {
                        coreEvRec = zinfo->eventRecorders[req.srcId];
                        canRecordPrefetch = (coreEvRec != nullptr);
                    }
                }

                auto issuePrefetch = [&](Address lineAddr, uint64_t issueCycle) -> uint64_t {
                    MESIState state = I;
                    uint32_t flags = MemReq::PREFETCH | MemReq::PREFETCH_HINT;
                    bool recorded = false;
                    TimingRecord savedRecord;
                    bool savedRecordValid = false;

                    if (!canRecordPrefetch) {
                        flags |= MemReq::PREFETCH_NOREC;
                    } else if (coreEvRec->hasRecord()) {
                        savedRecord = coreEvRec->popRecord();
                        savedRecordValid = true;
                    }

                    MemReq pfReq = {lineAddr, GETS, req.childId, &state, issueCycle, req.childLock, state, req.srcId, flags};
                    uint64_t respCycle = selectParent(lineAddr)->access(pfReq);

                    if (canRecordPrefetch) {
                        if (coreEvRec->hasRecord()) {
                            TimingRecord pfRecord = coreEvRec->popRecord();
                            recorded = issuingOOO->recordPrefetch(pfRecord);
                            if (!recorded && pfRecord.startEvent) {
                                pfRecord.startEvent->queue(pfRecord.reqCycle);
                            }
                        }
                        if (savedRecordValid) {
                            assert(!coreEvRec->hasRecord());
                            coreEvRec->pushRecord(savedRecord);
                        }
                    }

                    if (recorded) {
                        profPrefetchRecorderAttached.inc();
                    } else {
                        profPrefetchRecorderBypassed.inc();
                    }

                    assert(state == I);
                    return respCycle;
                };

                if (prefetchPos < kLinesPerEntry && !e.valid[prefetchPos]) {
                    Address pfLineAddr = req.lineAddr + prefetchPos - pos;
                    uint64_t pfRespCycle = issuePrefetch(pfLineAddr, reqCycle);
                    e.valid[prefetchPos] = true;
                    e.times[prefetchPos].fill(reqCycle, pfRespCycle);
                    profPrefetches.inc();

                    if (shortPrefetch && fetchDepth < 8 && prefetchPos + stride < kLinesPerEntry && !e.valid[prefetchPos + stride]) {
                        // prefetchPos += stride;
                        // pfLineAddr += stride;
                        // pfRespCycle = issuePrefetch(pfLineAddr, reqCycle);
                        // e.valid[prefetchPos] = true;
                        // e.times[prefetchPos].fill(reqCycle, pfRespCycle);
                        // profPrefetches.inc();
                        // profDoublePrefetches.inc();
                    }
                    e.lastPrefetchPos = prefetchPos;
                }
            } else {
                profLowConfAccs.inc();
            }
        } else {
            e.conf.dec();
            // See if we need to switch strides
            if (!e.conf.pred()) {
                int32_t lastStride = e.lastPos - e.lastLastPos;

                if (stride && stride != e.stride && stride == lastStride) {
                    e.conf.reset();
                    e.stride = stride;
                    profStrideSwitches.inc();
                }
            }
            e.lastPrefetchPos = pos;
        }

        e.lastLastPos = e.lastPos;
        e.lastPos = pos;
    }

    req.childId = origChildId;
    return respCycle;
}

void StreamPrefetcher::invalidateLine(Address lineAddr) {
    constexpr uint32_t kPageStride = kLinesPerEntry;
    const Address pageAddr = lineAddr >> kPageShift;
    const uint32_t pos = static_cast<uint32_t>(lineAddr & (kPageStride - 1));

    for (uint32_t i = 0; i < 16; i++) {
        if (tag[i] == pageAddr) {
            if (array[i].valid[pos]) {
                array[i].valid[pos] = false;
            }
            break;
        }
    }
}

// nop for now; do we need to invalidate our own state?
uint64_t StreamPrefetcher::invalidate(const InvReq& req) {
    if (req.type == INV || req.type == INVX) {
        invalidateLine(req.lineAddr);
    }
    return child->invalidate(req);
}

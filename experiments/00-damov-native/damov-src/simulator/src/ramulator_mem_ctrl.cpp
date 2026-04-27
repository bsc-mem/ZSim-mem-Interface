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

#include "ramulator_mem_ctrl.h"
#include <map>
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "zsim.h"
#include "RamulatorWrapper.h"
#include "Request.h"
#include "galloc.h"

using namespace std; // NOLINT(build/namespaces)

class RamulatorAccEvent : public TimingEvent {
  private:
    Ramulator* dram;
    bool write;
    Address addr;
    uint32_t coreid;
  public:
    uint64_t sCycle;

  // DAMOV-extended
    uint64_t enqueueCycle;
  // DAMOV-extended
    uint64_t issueCycle;

    RamulatorAccEvent(Ramulator* _dram, bool _write, Address _addr, int32_t domain, uint32_t _coreid) :
            TimingEvent(0, 0, domain), dram(_dram), write(_write), addr(_addr), coreid(_coreid), enqueueCycle(0), issueCycle(0) {}

    bool isWrite() const {
      return write;
    }

    Address getAddr() const {
      return addr;
    }

    uint32_t getCoreID() const{
      return coreid;
    }

    // DAMOV-extended
    uint64_t getEnqueueCycle() const {
      return enqueueCycle;
    }

    // DAMOV-extended
    uint64_t getIssueCycle() const {
      return issueCycle;
    }

    // DAMOV-extended
    void setIssueCycle(uint64_t cycle) {
      issueCycle = cycle;
    }

    void simulate(uint64_t startCycle) {
      sCycle = startCycle;

      // DAMOV-extended
      enqueueCycle = startCycle;
      issueCycle = startCycle;

      dram->enqueue(this, startCycle);
    }
};


Ramulator::Ramulator(std::string config_file, unsigned num_cpus, unsigned cache_line_size, uint32_t _minLatency, uint32_t _domain,
  const g_string& _name, bool pim_mode, const string& application,
  unsigned _cpuFreq, bool _record_memory_trace, bool _networkOverhead, const std::vector<uint32_t>& trackedCores):
	wrapper(NULL),
	read_cb_func(std::bind(&Ramulator::DRAM_read_return_cb, this, std::placeholders::_1)),
	write_cb_func(std::bind(&Ramulator::DRAM_write_return_cb, this, std::placeholders::_1)),
	resp_stall(false),
	req_stall(false),
  trackCoreStatsEnabled(false)
{
  minLatency = _minLatency;
  m_num_cores=num_cpus;
  // DAMOV-extended
  configureTrackedCores(trackedCores);
  const char* config_path = config_file.c_str();
  string pathStr = zinfo->outputDir;
  cout << pathStr << " " << application << endl;
  application_name = pathStr+"/"+application;
  const char* app_name = application_name.c_str();

  wrapper = new ramulator::RamulatorWrapper(config_path, num_cpus, cache_line_size, pim_mode, _record_memory_trace, app_name, _networkOverhead, trackedCores);

  cpu_tick = int(1000000.0/_cpuFreq);
  mem_tick = wrapper->get_tCK()*1000;
  if(pim_mode) cpu_tick = mem_tick;

  tick_gcd = gcd(cpu_tick, mem_tick);
  cpu_tick /= tick_gcd;
  mem_tick /= tick_gcd;

  tCK = wrapper->get_tCK();
  cpuFreq = _cpuFreq;
  clockDivider = 0;
  memFreq = (1/(tCK /1000000))/1000;
  info ("[RAMULATOR] Mem frequency %f", memFreq);
  this->pim_mode = pim_mode;
  if(pim_mode) cpuFreq = memFreq;
  freqRatio = ceil(cpuFreq/memFreq);
  info("[RAMILATOR] CPU/Mem frequency ratio %d", freqRatio);

  Stats_ramulator::statlist.output(pathStr+"/"+application+".ramulator.stats");
  curCycle = 0;
  domain = _domain;
  TickEvent<Ramulator>* tickEv = new TickEvent<Ramulator>(this, domain);
  tickEv->queue(0);  // start the sim at time 0
  name = _name;
}

// DAMOV-extended: core tracking: wire tracked cores through zsim into Ramulator.
void Ramulator::configureTrackedCores(const std::vector<uint32_t>& trackedCores) {
  trackedCoreStats.clear();
  trackedCoreIndex.clear();
  trackedCoreRamStats.clear();
  for (uint32_t coreId : trackedCores) {
    if (coreId >= m_num_cores) {
      warn("[RAMULATOR] trackCores entry %u is outside of available cores (%u); ignoring", coreId, m_num_cores);
      continue;
    }
    if (trackedCoreIndex.find(coreId) != trackedCoreIndex.end()) {
      warn("[RAMULATOR] Duplicate trackCores entry %u ignored", coreId);
      continue;
    }
    TrackCoreStats stats;
    stats.coreId = coreId;
    trackedCoreIndex[coreId] = trackedCoreStats.size();
    trackedCoreStats.push_back(stats);
  }
  trackCoreStatsEnabled = !trackedCoreStats.empty();
  if (!trackCoreStatsEnabled) return;

  trackedCoreRamStats.resize(trackedCoreStats.size());
  for (size_t i = 0; i < trackedCoreStats.size(); ++i) {
    uint32_t coreId = trackedCoreStats[i].coreId;
    std::string prefix = "trackCore" + std::to_string(coreId) + ".";
    trackedCoreRamStats[i].avgReadLatency
        .name(prefix + "read_latency_avg")
        .desc("Average read latency (cycles)")
        .precision(4);
    trackedCoreRamStats[i].avgWriteLatency
        .name(prefix + "write_latency_avg")
        .desc("Average write latency (cycles)")
        .precision(4);
    trackedCoreRamStats[i].avgQueueTime
        .name(prefix + "queue_time_avg")
        .desc("Average enqueue-to-issue delay (cycles)")
        .precision(4);
    trackedCoreRamStats[i].avgInterval
        .name(prefix + "request_interval_avg")
        .desc("Average interval between issued requests (cycles)")
        .precision(4);
  }
}

Ramulator::~Ramulator(){
  delete wrapper;
}

void Ramulator::initStats(AggregateStat* parentStat) {
  AggregateStat* memStats = new AggregateStat();
  memStats->init(name.c_str(), "Memory controller stats");
  profReads.init("rd", "Read requests"); memStats->append(&profReads);
  profWrites.init("wr", "Write requests"); memStats->append(&profWrites);
  profTotalRdLat.init("rdlat", "Total latency experienced by read requests"); memStats->append(&profTotalRdLat);
  profTotalWrLat.init("wrlat", "Total latency experienced by write requests"); memStats->append(&profTotalWrLat);
  reissuedAccesses.init("reissuedAccesses", "Number of accesses that were reissued due to full queue"); memStats->append(&reissuedAccesses);
  parentStat->append(memStats);
  // DAMOV-extended
  initTrackedCoreStats(parentStat);
}

// DAMOV-extended: core tracking stats
Ramulator::TrackCoreStats* Ramulator::getTrackedCoreStats(uint32_t coreId) {
  auto it = trackedCoreIndex.find(coreId);
  if (it == trackedCoreIndex.end()) return nullptr;
  return &trackedCoreStats[it->second];
}

// DAMOV-extended: core tracking stats
void Ramulator::initTrackedCoreStats(AggregateStat* parentStat) {
  if (!trackCoreStatsEnabled) return;
  AggregateStat* trackedAgg = new AggregateStat();
  trackedAgg->init("trackCores", "Per-core Ramulator interface stats (cycles)");
  parentStat->append(trackedAgg);

  for (size_t i = 0; i < trackedCoreStats.size(); ++i) {
    TrackCoreStats& stats = trackedCoreStats[i];
    std::string coreName = "core" + std::to_string(stats.coreId);
    AggregateStat* coreStat = new AggregateStat();
    coreStat->init(gm_strdup(coreName.c_str()), "Tracked core stats");
    trackedAgg->append(coreStat);

    std::string rdLatName = coreName + ".avg_read_latency";
    ProxyStat* avgRdLat = new ProxyStat();
    avgRdLat->init(gm_strdup(rdLatName.c_str()), "Average read latency (cycles)", &stats.avgReadLatencyValue);
    coreStat->append(avgRdLat);

    std::string wrLatName = coreName + ".avg_write_latency";
    ProxyStat* avgWrLat = new ProxyStat();
    avgWrLat->init(gm_strdup(wrLatName.c_str()), "Average write latency (cycles)", &stats.avgWriteLatencyValue);
    coreStat->append(avgWrLat);

    std::string queueName = coreName + ".avg_queue_time";
    ProxyStat* avgQueue = new ProxyStat();
    avgQueue->init(gm_strdup(queueName.c_str()), "Average enqueue-to-issue delay (cycles)", &stats.avgQueueTimeValue);
    coreStat->append(avgQueue);

    std::string intervalName = coreName + ".avg_request_interval";
    ProxyStat* avgInterval = new ProxyStat();
    avgInterval->init(gm_strdup(intervalName.c_str()), "Average interval between issued requests (cycles)", &stats.avgIntervalValue);
    coreStat->append(avgInterval);
  }
}

// DAMOV-extended
void Ramulator::handleTrackedRequestIssue(RamulatorAccEvent* ev) {
  ev->setIssueCycle(curCycle);
  if (!trackCoreStatsEnabled) return;
  TrackCoreStats* stats = getTrackedCoreStats(ev->getCoreID());
  if (!stats) return;
  uint64_t issueCycle = ev->getIssueCycle();
  if (stats->seenIssue) {
    if (issueCycle >= stats->lastIssueCycle) {
      uint64_t interval = issueCycle - stats->lastIssueCycle;
      stats->totalInterval += interval;
      stats->intervalCount++;
      stats->avgIntervalValue = stats->totalInterval / stats->intervalCount;
    }
  } else {
    stats->seenIssue = true;
  }
  stats->lastIssueCycle = issueCycle;
}

// DAMOV-extended
void Ramulator::handleTrackedRequestComplete(RamulatorAccEvent* ev, uint64_t lat, bool isWrite) {
  if (!trackCoreStatsEnabled) return;
  TrackCoreStats* stats = getTrackedCoreStats(ev->getCoreID());
  if (!stats) return;

  if (isWrite) {
    stats->totalWriteLatency += lat;
    stats->writeCount++;
    stats->avgWriteLatencyValue = stats->totalWriteLatency / stats->writeCount;
  } else {
    stats->totalReadLatency += lat;
    stats->readCount++;
    stats->avgReadLatencyValue = stats->totalReadLatency / stats->readCount;
  }

  if (ev->getIssueCycle() >= ev->getEnqueueCycle()) {
    uint64_t queueTime = ev->getIssueCycle() - ev->getEnqueueCycle();
    stats->totalQueueTime += queueTime;
    stats->queueCount++;
    stats->avgQueueTimeValue = stats->totalQueueTime / stats->queueCount;
  }
}

// DAMOV-extended
void Ramulator::publishTrackedCoreStats() {
  if (!trackCoreStatsEnabled) return;
  for (size_t i = 0; i < trackedCoreStats.size(); ++i) {
    TrackCoreStats& stats = trackedCoreStats[i];
    auto& handles = trackedCoreRamStats[i];
    double avgRead = (stats.readCount > 0)? static_cast<double>(stats.totalReadLatency)/stats.readCount : 0.0;
    double avgWrite = (stats.writeCount > 0)? static_cast<double>(stats.totalWriteLatency)/stats.writeCount : 0.0;
    double avgQueue = (stats.queueCount > 0)? static_cast<double>(stats.totalQueueTime)/stats.queueCount : 0.0;
    double avgInterval = (stats.intervalCount > 0)? static_cast<double>(stats.totalInterval)/stats.intervalCount : 0.0;
    handles.avgReadLatency = avgRead;
    handles.avgWriteLatency = avgWrite;
    handles.avgQueueTime = avgQueue;
    handles.avgInterval = avgInterval;
  }
}

uint64_t Ramulator::access(MemReq& req) {
  switch (req.type) {
    case PUTS:
    case PUTX:
      *req.state = I;
      break;
    case GETS:
      *req.state = req.is(MemReq::NOEXCL)? S : E;
      break;
    case GETX:
      *req.state = M;
      break;
    default: panic("!?");
  }

  if(req.type == PUTS){
    return req.cycle; //must return an absolute value, 0 latency
  }
  else {
    bool isWrite = (req.type == PUTX);
    uint64_t respCycle = req.cycle + minLatency;

    if (zinfo->eventRecorders[req.srcId]) {
      Address addr = req.lineAddr <<lineBits;
      RamulatorAccEvent* memEv = new (zinfo->eventRecorders[req.srcId]) RamulatorAccEvent(this, isWrite, addr, domain,req.srcId);
      memEv->setMinStartCycle(req.cycle);
      TimingRecord tr = {addr, req.cycle, respCycle, req.type, memEv, memEv};
      zinfo->eventRecorders[req.srcId]->pushRecord(tr);
    }
    return respCycle;
  }
}

uint32_t Ramulator::tick(uint64_t cycle) {
  // REMOVE comments for clock divider (i.e., memory clock is different from host clock)
  // if((tickCounter % freqRatio) == 0){
  wrapper->tick();
  // }
  tickCounter++;

  if(overflowQueue.size() > 0){
    RamulatorAccEvent *ev = overflowQueue.front();
    if(ev->isWrite()){
      ramulator::Request req((long)ev->getAddr(), ramulator::Request::Type::WRITE, write_cb_func,ev->getCoreID());
      long addr_tmp = req._addr;

      if(wrapper->send(req)){
        overflowQueue.pop_front();
        inflight_w++;

        inflightRequests.insert(std::pair<uint64_t, RamulatorAccEvent*>((long)ev->getAddr(), ev));
        // DAMOV-extended
        handleTrackedRequestIssue(ev);
        ev->hold();
      }
    }
    else {
      ramulator::Request req((long)ev->getAddr(), ramulator::Request::Type::READ, read_cb_func ,ev->getCoreID());
      long addr_tmp = req._addr;

      if(wrapper->send(req)){
        overflowQueue.pop_front();
        inflight_r++;

        inflightRequests.insert(std::pair<uint64_t, RamulatorAccEvent*>((long)ev->getAddr(), ev));
        // DAMOV-extended: tracking events
        handleTrackedRequestIssue(ev);
        ev->hold();
      }
    }
  }

  curCycle++;
  return 1;
}

void Ramulator::finish(){
  wrapper->finish();
  // DAMOV-extended: tracking events
  publishTrackedCoreStats();
  Stats_ramulator::statlist.printall();
}

void Ramulator::enqueue(RamulatorAccEvent* ev, uint64_t cycle) {
  long addr_tmp;

  if(ev->isWrite()){
    ramulator::Request req((long)ev->getAddr(), ramulator::Request::Type::WRITE, write_cb_func,ev->getCoreID());
    addr_tmp = req._addr;

    if(!wrapper->send(req)){
      overflowQueue.push_back(ev);
      reissuedAccesses.inc();
      return;
    }
      inflight_w++;
  }
  else {
    ramulator::Request req((long)ev->getAddr(), ramulator::Request::Type::READ, read_cb_func, ev->getCoreID());
    long addr_tmp = req._addr;

    if(!wrapper->send(req)){
      overflowQueue.push_back(ev);
      reissuedAccesses.inc();
      return;
    }

    inflight_r++;
  }

  inflightRequests.insert(std::pair<uint64_t, RamulatorAccEvent*>((long)ev->getAddr(), ev));
  // DAMOV-extended: tracking events
  handleTrackedRequestIssue(ev);
  ev->hold();
}

void Ramulator::DRAM_read_return_cb(ramulator::Request& req) {
  std::multimap<uint64_t, RamulatorAccEvent*>::iterator it = inflightRequests.find(req._addr);
  if(it == inflightRequests.end()){
    info("[RAMULATOR] I didn't request address %ld (%ld)", req._addr, req.addr);
  }

  assert((it != inflightRequests.end()));
  RamulatorAccEvent* ev = it->second;

  uint32_t lat = curCycle+1 - ev->sCycle;

  bool isWrite = ev->isWrite();
  if (isWrite) {
    profWrites.inc();
    profTotalWrLat.inc(lat);
    inflight_w--;
  }
  else {
    profReads.inc();
    profTotalRdLat.inc(lat);
    inflight_r--;
  }
  // DAMOV-extended: tracking events
  handleTrackedRequestComplete(ev, lat, isWrite);

  ev->release();
  ev->done(curCycle+1);

  inflightRequests.erase(it);
}

void Ramulator::DRAM_write_return_cb(ramulator::Request& req) {
  //Same as read for now
  DRAM_read_return_cb(req);
}

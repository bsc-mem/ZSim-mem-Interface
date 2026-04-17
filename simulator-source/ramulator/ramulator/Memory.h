#ifndef __MEMORY_H
#define __MEMORY_H

#include "Config.h"
#include "DRAM.h"
#include "Request.h"
#include "Controller.h"
#include "HMC_Controller.h"
#include "SpeedyController.h"
#include "Statistics.h"
#include "GDDR5.h"
#include "HBM.h"
#include "HMC.h"
#include "LPDDR3.h"
#include "LPDDR4.h"
#include "WideIO2.h"
#include "DSARP.h"
#include <vector>
#include <functional>
#include <cmath>
#include <cassert>
#include <tuple>
#include <string>
using namespace std;

namespace ramulator
{

class MemoryBase{
public:
    MemoryBase() {}
    virtual ~MemoryBase() {}
    virtual double clk_ns() = 0;
    virtual void tick() = 0;
    virtual bool send(Request req) = 0;
    virtual int pending_requests() = 0;
    virtual void finish()=0;
    virtual long page_allocator(long addr, int coreid) = 0;
    virtual void record_core(int coreid) = 0;
    virtual void set_address_recorder () = 0;
    virtual void set_application_name(string) = 0;
};

template <class T, template<typename> class Controller = Controller >
class Memory : public MemoryBase
{
protected:
  ScalarStat dram_capacity;
  ScalarStat num_dram_cycles;
  ScalarStat num_incoming_requests;
  VectorStat num_read_requests;
  VectorStat num_write_requests;
  ScalarStat ramulator_active_cycles;
  ScalarStat memory_footprint;
  VectorStat incoming_requests_per_channel;
  VectorStat incoming_read_reqs_per_channel;

  ScalarStat physical_page_replacement;
  ScalarStat maximum_bandwidth;
  ScalarStat read_bandwidth;
  ScalarStat write_bandwidth;
  // shared by all Controller objects
  ScalarStat read_transaction_bytes;
  ScalarStat write_transaction_bytes;
  ScalarStat row_hits;
  ScalarStat row_misses;
  ScalarStat row_conflicts;
  VectorStat read_row_hits;
  VectorStat read_row_misses;
  VectorStat read_row_conflicts;
  VectorStat write_row_hits;
  VectorStat write_row_misses;
  VectorStat write_row_conflicts;

  ScalarStat read_latency_avg;
  ScalarStat read_latency_ns_avg;
  ScalarStat read_latency_sum;
  ScalarStat queueing_latency_avg;
  ScalarStat queueing_latency_ns_avg;
  ScalarStat queueing_latency_sum;

  ScalarStat req_queue_length_avg;
  ScalarStat req_queue_length_sum;
  ScalarStat read_req_queue_length_avg;
  ScalarStat read_req_queue_length_sum;
  ScalarStat write_req_queue_length_avg;
  ScalarStat write_req_queue_length_sum;

  long max_address;
  bool trackCoreStatsEnabled = false;
  std::vector<int> trackedCoreIds;
  struct TrackCoreStatHandles {
    ScalarStat read_latency_avg;
    ScalarStat write_latency_avg;
    ScalarStat queue_time_avg;
    ScalarStat request_interval_avg;
  };
  std::vector<TrackCoreStatHandles> trackedCoreStatHandles;
  std::vector<TrackCoreAggregate> trackedCoreAggregates;
public:
    enum class Type {
        ChRaBaRoCo,
        RoBaRaCoCh,
        MAX,
    } type = Type::RoBaRaCoCh;

    enum class Translation {
      None,
      Random,
      MAX,
    } translation = Translation::None;

    std::map<string, Translation> name_to_translation = {
      {"None", Translation::None},
      {"Random", Translation::Random},
    };

    vector<int> free_physical_pages;
    long free_physical_pages_remaining;
    map<pair<int, long>, long> page_translation;

    vector<Controller<T>*> ctrls;
    T * spec;
    vector<int> addr_bits;

    int tx_bits;
    int cacheline_size;
    bool use_skylake_address_mapping = false;

    Memory(const Config& configs, vector<Controller<T>*> ctrls)
        : ctrls(ctrls),
          spec(ctrls[0]->channel->spec),
          addr_bits(int(T::Level::MAX))
    {

        // make sure 2^N channels/ranks
        // TODO support channel number that is not powers of 2
        int *sz = spec->org_entry.count;
        assert((sz[0] & (sz[0] - 1)) == 0);
        assert((sz[1] & (sz[1] - 1)) == 0);
        // validate size of one transaction
        int tx = (spec->prefetch_size * spec->channel_width / 8);
        tx_bits = calc_log2(tx);
        assert((1<<tx_bits) == tx);
        // If hi address bits will not be assigned to Rows
        // then the chips must not be LPDDRx 6Gb, 12Gb etc.
        if (type != Type::RoBaRaCoCh && spec->standard_name.substr(0, 5) == "LPDDR")
            assert((sz[int(T::Level::Row)] & (sz[int(T::Level::Row)] - 1)) == 0);

        max_address = spec->channel_width / 8;

        for (unsigned int lev = 0; lev < addr_bits.size(); lev++) {
          addr_bits[lev] = calc_log2(sz[lev]);
            max_address *= sz[lev];
        }

        addr_bits[int(T::Level::MAX) - 1] -= calc_log2(spec->prefetch_size);

        // Initiating translation
        if (configs.contains("translation")) {
          translation = name_to_translation[configs["translation"]];
        }
        if (translation != Translation::None) {
          // construct a list of available pages
          // TODO: this should not assume a 4KB page!
          free_physical_pages_remaining = max_address >> 12;

          free_physical_pages.resize(free_physical_pages_remaining, -1);
        }

        cacheline_size = configs.get_cacheline_size();
        if (configs.contains("skylake_address_mapping")) {
          string mapping_mode = configs["skylake_address_mapping"];
          use_skylake_address_mapping =
              mapping_mode == "on" || mapping_mode == "true" || mapping_mode == "1";
        }

        init_tracked_core_stats(configs);


        // regStats
        dram_capacity
            .name("dram_capacity")
            .desc("Number of bytes in simulated DRAM")
            .precision(0)
            ;
        dram_capacity = max_address;

        num_dram_cycles
            .name("dram_cycles")
            .desc("Number of DRAM cycles simulated")
            .precision(0)
            ;
        num_incoming_requests
            .name("incoming_requests")
            .desc("Number of incoming requests to DRAM")
            .precision(0)
            ;
        num_read_requests
            .init(configs.get_core_num())
            .name("read_requests")
            .desc("Number of incoming read requests to DRAM per core")
            .precision(0)
            ;
        num_write_requests
            .init(configs.get_core_num())
            .name("write_requests")
            .desc("Number of incoming write requests to DRAM per core")
            .precision(0)
            ;
        incoming_requests_per_channel
            .init(sz[int(T::Level::Channel)])
            .name("incoming_requests_per_channel")
            .desc("Number of incoming requests to each DRAM channel")
            ;
        incoming_read_reqs_per_channel
            .init(sz[int(T::Level::Channel)])
            .name("incoming_read_reqs_per_channel")
            .desc("Number of incoming read requests to each DRAM channel")
            ;

        ramulator_active_cycles
            .name("ramulator_active_cycles")
            .desc("The total number of cycles that the DRAM part is active (serving R/W)")
            .precision(0)
            ;
        memory_footprint
            .name("memory_footprint")
            .desc("memory footprint in byte")
            .precision(0)
            ;
        physical_page_replacement
            .name("physical_page_replacement")
            .desc("The number of times that physical page replacement happens.")
            .precision(0)
            ;

        maximum_bandwidth
            .name("maximum_bandwidth")
            .desc("The theoretical maximum bandwidth (Bps)")
            .precision(0)
            ;
        read_bandwidth
            .name("read_bandwidth")
            .desc("Real read bandwidth(Bps)")
            .precision(0)
            ;
        write_bandwidth
            .name("write_bandwidth")
            .desc("Real write bandwidth(Bps)")
            .precision(0)
            ;

        // shared by all Controller objects

        read_transaction_bytes
            .name("read_transaction_bytes")
            .desc("The total byte of read transaction")
            .precision(0)
            ;
        write_transaction_bytes
            .name("write_transaction_bytes")
            .desc("The total byte of write transaction")
            .precision(0)
            ;

        row_hits
            .name("row_hits")
            .desc("Number of row hits")
            .precision(0)
            ;
        row_misses
            .name("row_misses")
            .desc("Number of row misses")
            .precision(0)
            ;
        row_conflicts
            .name("row_conflicts")
            .desc("Number of row conflicts")
            .precision(0)
            ;

        read_row_hits
            .init(configs.get_core_num())
            .name("read_row_hits")
            .desc("Number of row hits for read requests")
            .precision(0)
            ;
        read_row_misses
            .init(configs.get_core_num())
            .name("read_row_misses")
            .desc("Number of row misses for read requests")
            .precision(0)
            ;
        read_row_conflicts
            .init(configs.get_core_num())
            .name("read_row_conflicts")
            .desc("Number of row conflicts for read requests")
            .precision(0)
            ;

        write_row_hits
            .init(configs.get_core_num())
            .name("write_row_hits")
            .desc("Number of row hits for write requests")
            .precision(0)
            ;
        write_row_misses
            .init(configs.get_core_num())
            .name("write_row_misses")
            .desc("Number of row misses for write requests")
            .precision(0)
            ;
        write_row_conflicts
            .init(configs.get_core_num())
            .name("write_row_conflicts")
            .desc("Number of row conflicts for write requests")
            .precision(0)
            ;

        read_latency_sum
            .name("read_latency_sum")
            .desc("The memory latency cycles (in memory time domain) sum for all read requests in this channel")
            .precision(0)
            ;
        read_latency_avg
            .name("read_latency_avg")
            .desc("The average memory latency cycles (in memory time domain) per request for all read requests in this channel")
            .precision(6)
            ;
        queueing_latency_sum
            .name("queueing_latency_sum")
            .desc("The sum of cycles waiting in queue before first command issued")
            .precision(0)
            ;
        queueing_latency_avg
            .name("queueing_latency_avg")
            .desc("The average of cycles waiting in queue before first command issued")
            .precision(6)
            ;
        read_latency_ns_avg
            .name("read_latency_ns_avg")
            .desc("The average memory latency (ns) per request for all read requests in this channel")
            .precision(6)
            ;
        queueing_latency_ns_avg
            .name("queueing_latency_ns_avg")
            .desc("The average of time (ns) waiting in queue before first command issued")
            .precision(6)
            ;

        req_queue_length_sum
            .name("req_queue_length_sum")
            .desc("Sum of read and write queue length per memory cycle.")
            .precision(0)
            ;
        req_queue_length_avg
            .name("req_queue_length_avg")
            .desc("Average of read and write queue length per memory cycle.")
            .precision(6)
            ;

        read_req_queue_length_sum
            .name("read_req_queue_length_sum")
            .desc("Read queue length sum per memory cycle.")
            .precision(0)
            ;
        read_req_queue_length_avg
            .name("read_req_queue_length_avg")
            .desc("Read queue length average per memory cycle.")
            .precision(6)
            ;

        write_req_queue_length_sum
            .name("write_req_queue_length_sum")
            .desc("Write queue length sum per memory cycle.")
            .precision(0)
            ;
        write_req_queue_length_avg
            .name("write_req_queue_length_avg")
            .desc("Write queue length average per memory cycle.")
            .precision(6)
            ;

        for (auto ctrl : ctrls) {
          ctrl->read_transaction_bytes = &read_transaction_bytes;
          ctrl->write_transaction_bytes = &write_transaction_bytes;

          ctrl->row_hits = &row_hits;
          ctrl->row_misses = &row_misses;
          ctrl->row_conflicts = &row_conflicts;
          ctrl->read_row_hits = &read_row_hits;
          ctrl->read_row_misses = &read_row_misses;
          ctrl->read_row_conflicts = &read_row_conflicts;
          ctrl->write_row_hits = &write_row_hits;
          ctrl->write_row_misses = &write_row_misses;
          ctrl->write_row_conflicts = &write_row_conflicts;

          ctrl->read_latency_sum = &read_latency_sum;
          ctrl->queueing_latency_sum = &queueing_latency_sum;

          ctrl->req_queue_length_sum = &req_queue_length_sum;
          ctrl->read_req_queue_length_sum = &read_req_queue_length_sum;
          ctrl->write_req_queue_length_sum = &write_req_queue_length_sum;
        }
    }

    ~Memory()
    {
        for (auto ctrl: ctrls)
            delete ctrl;
        delete spec;
    }

    double clk_ns()
    {
        return spec->speed_entry.tCK;
    }

    void record_core(int coreid) {
      for (auto ctrl : ctrls) {
        ctrl->record_core(coreid);
      }
    }

    void tick()
    {
        ++num_dram_cycles;

        bool is_active = false;
        for (auto ctrl : ctrls) {
          is_active = is_active || ctrl->is_active();
          ctrl->tick();
        }
        if (is_active) {
          ramulator_active_cycles++;
        }
    }

    void set_address_recorder () {}
    void set_application_name(string _app) {}
    void init_tracked_core_stats(const Config& configs) {
        const std::vector<int>& coreList = configs.get_tracked_cores();
        trackCoreStatsEnabled = !coreList.empty();
        if (!trackCoreStatsEnabled) return;

        trackedCoreIds.assign(coreList.begin(), coreList.end());
        trackedCoreStatHandles.resize(trackedCoreIds.size());
        trackedCoreAggregates.resize(trackedCoreIds.size());

        for (size_t i = 0; i < trackedCoreIds.size(); ++i) {
            int coreId = trackedCoreIds[i];
            std::string prefix = "track_cores.core" + std::to_string(coreId);
            trackedCoreStatHandles[i].read_latency_avg
                .name(prefix + ".read_latency_avg")
                .desc("Average read latency (cycles) for tracked core")
                .precision(6);
            trackedCoreStatHandles[i].write_latency_avg
                .name(prefix + ".write_latency_avg")
                .desc("Average write latency (cycles) for tracked core")
                .precision(6);
            trackedCoreStatHandles[i].queue_time_avg
                .name(prefix + ".queue_time_avg")
                .desc("Average enqueue-to-issue delay (cycles) for tracked core")
                .precision(6);
            trackedCoreStatHandles[i].request_interval_avg
                .name(prefix + ".request_interval_avg")
                .desc("Average interval between issued requests (cycles) for tracked core")
                .precision(6);
        }

        for (auto ctrl : ctrls) {
            ctrl->set_tracked_cores(trackedCoreIds);
        }
    }

    bool send(Request req)
    {
        req.addr_vec.resize(addr_bits.size());
        req.burst_count = cacheline_size / (1 << tx_bits);
        long addr = req.addr;
        int coreid = req.coreid;

        // Each transaction size is 2^tx_bits, so first clear the lowest tx_bits bits
        clear_lower_bits(addr, tx_bits);

        switch(int(type)){
            case int(Type::ChRaBaRoCo):
                for (int i = addr_bits.size() - 1; i >= 0; i--)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                break;
            case int(Type::RoBaRaCoCh):
                if (use_skylake_address_mapping) {
                    apply_skylake_address_mapping(addr, req);
                } else {
                    req.addr_vec[0] = slice_lower_bits(addr, addr_bits[0]);
                    req.addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr, addr_bits[addr_bits.size() - 1]);
                    for (int i = 1; i <= int(T::Level::Row); i++)
                        req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                }
                break;
            
                // the mapping added by pouya to suppor Intel Skylake
            // case int(Type::RoBaRaCoCh):
            //     // mapping: RoRaBaBgCoCh + hash functions 
            //     // addr_vec index 0:  channel
            //     // addr_vec index 1:  col
            //     // addr_vec index 2:  bank group
            //     // addr_vec index 3:  bank
            //     // addr_vec index 4:  rank
            //     // addr_vec index 5:  row

            //     // this is channel. We do not care about channels. ZSim is connected to 6 single-channel DDR4-2666 modules
            //     // printf("\n\nSending request to address %lx\n", addr);
            //     // printf("addr_bits size: %d\n", addr_bits[0]);
            //     req.addr_vec[0] = slice_lower_bits(addr, addr_bits[0]);


            //     // this is col. We have 7 cols. 
            //     // printf("Sending request to address %lx\n", addr);
            //     // printf("addr_bits size: %d\n", addr_bits[5]);
            //     req.addr_vec[5] = slice_lower_bits(addr, addr_bits[5]);

            //     // this is bank group. We have 4 bank groups.
            //     // printf("Sending request to address %lx\n", addr);
            //     // printf("addr_bits size: %d\n", addr_bits[3]);
            //     req.addr_vec[3] = slice_lower_bits(addr, addr_bits[3]);

            //     // this is bank. We have 4 banks per bank group.  
            //     // printf("Sending request to address %lx\n", addr);
            //     // printf("addr_bits size: %d\n", addr_bits[2]);
            //     req.addr_vec[2] = slice_lower_bits(addr, addr_bits[2]);

            //     // this is rank. We have 2 ranks.
            //     // printf("Sending request to address %lx\n", addr);
            //     // printf("addr_bits size: %d\n", addr_bits[1]);
            //     req.addr_vec[1] = slice_lower_bits(addr, addr_bits[1]);

            //     // this is row. We have many rows (2 to the power of 16).
            //     // printf("Sending request to address %lx\n", addr);
            //     // printf("addr_bits size: %d\n", addr_bits[4]);
            //     req.addr_vec[4] = slice_lower_bits(addr, addr_bits[4]);

            //     // XOR the least-significant bit of addr_vec[3] with bit 1 (second bit) of addr_vec[5]
            //     {
            //         int bit3_lsb = req.addr_vec[3] & 1;
            //         int bit5_second = (req.addr_vec[5] >> 1) & 1;
            //         int result_bit = bit3_lsb ^ bit5_second;
            //         req.addr_vec[3] = (req.addr_vec[3] & ~1) | result_bit;
            //     }

            //     // XOR the second bit of addr_vec[3] with the least-significant bit of addr_vec[4]
            //     {
            //         int bit3_second = (req.addr_vec[3] >> 1) & 1;
            //         int bit4_first = req.addr_vec[4] & 1;
            //         int result_bit = bit3_second ^ bit4_first;
            //         req.addr_vec[3] = (req.addr_vec[3] & ~(1 << 1)) | (result_bit << 1);
            //     }
                
            //     // XOR the first bit of addr_vec[2] with the bit 1 of addr_vec[4] 
            //     {
            //         int bit2_first = req.addr_vec[2] & 1;
            //         int bit4_second = (req.addr_vec[4] >> 1) & 1;
            //         int result_bit = bit2_first ^ bit4_second;
            //         req.addr_vec[2] = (req.addr_vec[2] & ~1) | result_bit;
            //     }

            //     // XOR the second bit of addr_vec[2] with the bit 2 of addr_vec[4] 
            //     {
            //         int bit2_second = (req.addr_vec[2] >> 1) & 1;
            //         int bit4_third = (req.addr_vec[4] >> 2) & 1;
            //         int result_bit = bit2_second ^ bit4_third;
            //         req.addr_vec[2] = (req.addr_vec[2] & ~(1 << 1)) | (result_bit << 1);
            //     }

            //     // XOR the first bit of addr_vec[1] with the bit 3 of addr_vec[4] 
            //     {
            //         int bit1_first = req.addr_vec[1] & 1;
            //         int bit4_fourth = (req.addr_vec[4] >> 3) & 1;
            //         int result_bit = bit1_first ^ bit4_fourth;
            //         req.addr_vec[1] = (req.addr_vec[1] & ~1) | result_bit;
            //     } 

                
            //     // Debug: print the computed address vector
            //     // for (size_t i = 0; i < req.addr_vec.size(); ++i) {
            //     //     printf("req.addr_vec[%zu] = %d\n", i, req.addr_vec[i]);
            //     // }
                


            //     break;
            
            default:
                assert(false);
        }
	if(ctrls[req.addr_vec[0]]->enqueue(req)) {
            // tally stats here to avoid double counting for requests that aren't enqueued
            ++num_incoming_requests;

            if (req.type == Request::Type::READ) {
              ++num_read_requests[coreid];

	      ++incoming_read_reqs_per_channel[req.addr_vec[int(T::Level::Channel)]];
            }
            if (req.type == Request::Type::WRITE) {
              ++num_write_requests[coreid];
           }
            ++incoming_requests_per_channel[req.addr_vec[int(T::Level::Channel)]];
          return true;
        }
        return false;
    }

    int pending_requests()
    {
        int reqs = 0;
        for (auto ctrl: ctrls)
            reqs += ctrl->readq.size() + ctrl->writeq.size() + ctrl->otherq.size() + ctrl->pending.size();
        return reqs;
    }

    void finish() {
      dram_capacity = max_address;
      int *sz = spec->org_entry.count;
      maximum_bandwidth = spec->speed_entry.rate * 1e6 * spec->channel_width * sz[int(T::Level::Channel)] / 8;

      long dram_cycles = num_dram_cycles.value();
      long total_read_req = num_read_requests.total();
      for (auto ctrl : ctrls) {
        ctrl->finish(dram_cycles);
      }
      if (trackCoreStatsEnabled) {
        for (auto& aggregate : trackedCoreAggregates) {
          aggregate = TrackCoreAggregate();
        }
        for (auto ctrl : ctrls) {
          ctrl->accumulate_tracked_core_stats(trackedCoreAggregates);
        }
      }
      read_bandwidth = read_transaction_bytes.value() * 1e9 / (dram_cycles * clk_ns());
      write_bandwidth = write_transaction_bytes.value() * 1e9 / (dram_cycles * clk_ns());
      read_latency_avg = read_latency_sum.value() / total_read_req;
      queueing_latency_avg = queueing_latency_sum.value() / total_read_req;
      read_latency_ns_avg = read_latency_avg.value() * clk_ns();
      queueing_latency_ns_avg = queueing_latency_avg.value() * clk_ns();
      req_queue_length_avg = req_queue_length_sum.value() / dram_cycles;
      read_req_queue_length_avg = read_req_queue_length_sum.value() / dram_cycles;
      write_req_queue_length_avg = write_req_queue_length_sum.value() / dram_cycles;
      if (trackCoreStatsEnabled) {
        for (size_t i = 0; i < trackedCoreStatHandles.size(); ++i) {
          const auto& totals = trackedCoreAggregates[i];
          double avgRead = (totals.readCount > 0)? static_cast<double>(totals.readLatencySum)/totals.readCount : 0.0;
          double avgWrite = (totals.writeCount > 0)? static_cast<double>(totals.writeLatencySum)/totals.writeCount : 0.0;
          double avgQueue = (totals.queueCount > 0)? static_cast<double>(totals.queueTimeSum)/totals.queueCount : 0.0;
          double avgInterval = (totals.intervalCount > 0)? static_cast<double>(totals.intervalSum)/totals.intervalCount : 0.0;
          trackedCoreStatHandles[i].read_latency_avg = avgRead;
          trackedCoreStatHandles[i].write_latency_avg = avgWrite;
          trackedCoreStatHandles[i].queue_time_avg = avgQueue;
          trackedCoreStatHandles[i].request_interval_avg = avgInterval;
        }
      }
    }

    long page_allocator(long addr, int coreid) {
        long virtual_page_number = addr >> 12;

        switch(int(translation)) {
            case int(Translation::None): {
              auto target = make_pair(coreid, virtual_page_number);
              if(page_translation.find(target) == page_translation.end()) {
                memory_footprint += 1<<12;
                page_translation[target] = virtual_page_number;
              }
              return addr;
            }
            case int(Translation::Random): {
                auto target = make_pair(coreid, virtual_page_number);
                if(page_translation.find(target) == page_translation.end()) {
                    // page doesn't exist, so assign a new page
                    // make sure there are physical pages left to be assigned

                    // if physical page doesn't remain, replace a previous assigned
                    // physical page.
                    memory_footprint += 1<<12;
                    if (!free_physical_pages_remaining) {
                      physical_page_replacement++;
                      long phys_page_to_read = lrand() % free_physical_pages.size();
                      assert(free_physical_pages[phys_page_to_read] != -1);
                      page_translation[target] = phys_page_to_read;
                    } else {
                        // assign a new page
                        long phys_page_to_read = lrand() % free_physical_pages.size();
                        // if the randomly-selected page was already assigned
                        if(free_physical_pages[phys_page_to_read] != -1) {
                            long starting_page_of_search = phys_page_to_read;

                            do {
                                // iterate through the list until we find a free page
                                // TODO: does this introduce serious non-randomness?
                                ++phys_page_to_read;
                                phys_page_to_read %= free_physical_pages.size();
                            }
                            while((phys_page_to_read != starting_page_of_search) && free_physical_pages[phys_page_to_read] != -1);
                        }

                        assert(free_physical_pages[phys_page_to_read] == -1);

                        page_translation[target] = phys_page_to_read;
                        free_physical_pages[phys_page_to_read] = coreid;
                        --free_physical_pages_remaining;
                    }
                }

                // SAUGATA TODO: page size should not always be fixed to 4KB
                return (page_translation[target] << 12) | (addr & ((1 << 12) - 1));
            }
            default:
                assert(false);
        }

    }

private:
    void apply_skylake_address_mapping(long& addr, Request& req)
    {
        assert(addr_bits.size() >= 6);

        // Mapping: RoRaBaBgCoCh, plus the Skylake-specific XOR hashing used in
        // the address-mapping experiment.
        req.addr_vec[0] = slice_lower_bits(addr, addr_bits[0]);
        req.addr_vec[5] = slice_lower_bits(addr, addr_bits[5]);
        req.addr_vec[3] = slice_lower_bits(addr, addr_bits[3]);
        req.addr_vec[2] = slice_lower_bits(addr, addr_bits[2]);
        req.addr_vec[1] = slice_lower_bits(addr, addr_bits[1]);
        req.addr_vec[4] = slice_lower_bits(addr, addr_bits[4]);

        {
            int bank_group_lsb = req.addr_vec[3] & 1;
            int column_second = (req.addr_vec[5] >> 1) & 1;
            int result_bit = bank_group_lsb ^ column_second;
            req.addr_vec[3] = (req.addr_vec[3] & ~1) | result_bit;
        }

        {
            int bank_group_second = (req.addr_vec[3] >> 1) & 1;
            int rank_lsb = req.addr_vec[4] & 1;
            int result_bit = bank_group_second ^ rank_lsb;
            req.addr_vec[3] = (req.addr_vec[3] & ~(1 << 1)) | (result_bit << 1);
        }

        {
            int bank_lsb = req.addr_vec[2] & 1;
            int rank_second = (req.addr_vec[4] >> 1) & 1;
            int result_bit = bank_lsb ^ rank_second;
            req.addr_vec[2] = (req.addr_vec[2] & ~1) | result_bit;
        }

        {
            int bank_second = (req.addr_vec[2] >> 1) & 1;
            int rank_third = (req.addr_vec[4] >> 2) & 1;
            int result_bit = bank_second ^ rank_third;
            req.addr_vec[2] = (req.addr_vec[2] & ~(1 << 1)) | (result_bit << 1);
        }

        {
            int rank_lsb = req.addr_vec[1] & 1;
            int row_fourth = (req.addr_vec[4] >> 3) & 1;
            int result_bit = rank_lsb ^ row_fourth;
            req.addr_vec[1] = (req.addr_vec[1] & ~1) | result_bit;
        }
    }

    int calc_log2(int val){
        int n = 0;
        while ((val >>= 1))
            n ++;
        return n;
    }
    int slice_lower_bits(long& addr, int bits)
    {
        int lbits = addr & ((1<<bits) - 1);
        addr >>= bits;
        return lbits;
    }
    void clear_lower_bits(long& addr, int bits)
    {
        addr >>= bits;
    }
    long lrand(void) {
        if(sizeof(int) < sizeof(long)) {
            return static_cast<long>(rand()) << (sizeof(int) * 8) | rand();
        }

        return rand();
    }
};

} /*namespace ramulator*/

#endif /*__MEMORY_H*/

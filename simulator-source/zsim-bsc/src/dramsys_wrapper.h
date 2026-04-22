#ifndef DRAMSYS_WRAPPER_H_
#define DRAMSYS_WRAPPER_H_

#ifdef _WITH_DRAMSYS_

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <memory>
#include <string>

#define SC_INCLUDE_DYNAMIC_PROCESSES
#define SC_NO_MAIN
#include <systemc>
#include <tlm.h>
#include <tlm_utils/peq_with_cb_and_phase.h>
#include <tlm_utils/simple_initiator_socket.h>

#include "DRAMSys/DRAMSys.h"
#include "DRAMSys/common/MemoryManager.h"
#include "DRAMSys/configuration/json/DRAMSysConfiguration.h"
#include "DRAMSys/configuration/memspec/MemSpec.h"

#include "g_std/stl_galloc.h"
#include "locks.h"

/*
 * DRAMSysWrapper
 * --------------
 * Thin adapter that merges "wrapper + bridge" responsibilities for zsim:
 *
 * - Owns one DRAMSys instance and the initiator socket bound to it.
 * - Accepts zsim-style requests via trySend().
 * - Converts TLM response phases into zsim-friendly Completion records.
 * - Stores completions in an internal queue to be drained by DRAMSysMem.
 *
 * Why completion queue strategy:
 * - TLM callbacks execute in SystemC context and should stay lightweight.
 * - zsim-side event completion, stats, and estimator updates happen in the
 *   controller tick path after popCompletion().
 * - This avoids re-entrancy and keeps ownership boundaries clear.
 *
 * Timing ownership:
 * - This class does not implement global barrier policy.
 * - Runtime/leader logic is expected to call advanceOneCycle() exactly once
 *   per backend cycle.
 */
class DRAMSysWrapper : public sc_core::sc_module, public GlobAlloc {
  public:
    struct Request {
        uint64_t addr;
        bool isWrite;
        uint32_t sourceId;
        uint64_t reqTag;  // Opaque token provided by controller to match completion.
    };

    struct Completion {
        uint64_t addr;
        bool isWrite;
        uint32_t sourceId;
        uint64_t reqTag;
        uint64_t scTimePs;  // SystemC timestamp in picoseconds at completion.
    };

    DRAMSysWrapper(const sc_core::sc_module_name& name,
                   const std::string& configPath,
                   size_t dataBytes = 64);
    ~DRAMSysWrapper() override;

    DRAMSysWrapper(const DRAMSysWrapper&) = delete;
    DRAMSysWrapper(DRAMSysWrapper&&) = delete;
    DRAMSysWrapper& operator=(const DRAMSysWrapper&) = delete;
    DRAMSysWrapper& operator=(DRAMSysWrapper&&) = delete;

    /*
     * Attempt to issue one request.
     * Returns false if the wrapper cannot accept right now (backpressure).
     */
    bool trySend(const Request& req);

    /*
     * Drain one completed request record. Returns false if queue is empty.
     */
    bool popCompletion(Completion& out);

    /*
     * Advance DRAMSys by exactly one memory clock tick.
     * Must be coordinated by DRAMSysRuntime barrier (one caller/leader).
     */
    void advanceOneCycle();

    double getTckNs() const;

    // Optional inspection helpers.
    bool hasInflight() const;
    size_t completionQueueSize() const;

  private:
    uint64_t normalizeAddress(uint64_t addr) const;

    class RequestExtension : public tlm::tlm_extension<RequestExtension> {
      public:
        RequestExtension() = default;
        explicit RequestExtension(const Request& r) : req(r) {}

        tlm::tlm_extension_base* clone() const override { return new RequestExtension(req); }

        void copy_from(const tlm::tlm_extension_base& ext) override {
            req = static_cast<const RequestExtension&>(ext).req;
        }

        Request req{};
    };

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans,
                                       tlm::tlm_phase& phase,
                                       sc_core::sc_time& delay);
    void peqCallback(tlm::tlm_generic_payload& trans, const tlm::tlm_phase& phase);
    void completeTransaction(tlm::tlm_generic_payload& trans);

    tlm_utils::simple_initiator_socket<DRAMSysWrapper> iSocket_;
    tlm_utils::peq_with_cb_and_phase<DRAMSysWrapper> peq_;

    DRAMSys::MemoryManager memoryManager_;
    std::unique_ptr<DRAMSys::DRAMSys> dramsys_;
    sc_core::sc_time tCK_;
    uint64_t addressRangeBytes_;

    size_t dataBytes_;
    tlm::tlm_generic_payload* requestInProgress_;

    std::deque<Completion> completionQueue_;

    mutable lock_t stateLock_;
};

#endif  // _WITH_DRAMSYS_

#endif  // DRAMSYS_WRAPPER_H_

#ifdef _WITH_DRAMSYS_

#include "dramsys_wrapper.h"

#include <cstring>

#include "log.h"

DRAMSysWrapper::DRAMSysWrapper(const sc_core::sc_module_name& name,
                               const std::string& configPath,
                               size_t dataBytes)
    : sc_core::sc_module(name),
      iSocket_("iSocket"),
      peq_(this, &DRAMSysWrapper::peqCallback),
      memoryManager_(true),
      dramsys_(std::make_unique<DRAMSys::DRAMSys>(
          "dramsys",
          DRAMSys::Config::from_path(configPath))),
      addressRangeBytes_(0),
      dataBytes_(dataBytes),
      requestInProgress_(nullptr),
      stateLock_(0) {
    const auto& memSpec = dramsys_->getMemSpec();
    tCK_ = memSpec.tCK;
    addressRangeBytes_ = memSpec.getSimMemSizeInBytes();

    futex_init(&stateLock_);
    iSocket_.register_nb_transport_bw(this, &DRAMSysWrapper::nb_transport_bw);
    iSocket_.bind(dramsys_->tSocket);
}

DRAMSysWrapper::~DRAMSysWrapper() {
    // any payload still in flight is released to avoid leaks on teardown.
    futex_lock(&stateLock_);
    if (requestInProgress_) {
        requestInProgress_->release();
        requestInProgress_ = nullptr;
    }
    futex_unlock(&stateLock_);
}

bool DRAMSysWrapper::trySend(const Request& req) {
    futex_lock(&stateLock_);
    if (requestInProgress_ != nullptr) {
        futex_unlock(&stateLock_);
        return false;
    }
    futex_unlock(&stateLock_);

    tlm::tlm_generic_payload* trans = memoryManager_.allocate(dataBytes_);
    trans->acquire();

    trans->set_auto_extension(new RequestExtension(req));

    trans->set_command(req.isWrite ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
    trans->set_address(normalizeAddress(req.addr));
    trans->set_data_length(dataBytes_);
    trans->set_streaming_width(dataBytes_);
    trans->set_byte_enable_ptr(nullptr);
    trans->set_dmi_allowed(false);
    trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    if (dataBytes_ > 0 && trans->get_data_ptr()) {
        // TODO: random pattern or some heuristic based payload creating 
        // is better and more realistic (StoreMode is better to be false because zsim only consider timings)
        std::memset(trans->get_data_ptr(), 0, dataBytes_);
    }

    futex_lock(&stateLock_);
    if (requestInProgress_ != nullptr) {
        futex_unlock(&stateLock_);
        trans->release();
        return false;
    }
    requestInProgress_ = trans;
    futex_unlock(&stateLock_);

    tlm::tlm_phase phase = tlm::BEGIN_REQ;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    tlm::tlm_sync_enum status = iSocket_->nb_transport_fw(*trans, phase, delay);

    if (status == tlm::TLM_ACCEPTED) {
        return true;
    }

    if (status == tlm::TLM_UPDATED) {
        peq_.notify(*trans, phase, delay);
        return true;
    }

    if (status == tlm::TLM_COMPLETED) {
        futex_lock(&stateLock_);
        if (requestInProgress_ == trans) {
            requestInProgress_ = nullptr;
        }
        futex_unlock(&stateLock_);
        completeTransaction(*trans);
        trans->release();
        return true;
    }

    futex_lock(&stateLock_);
    if (requestInProgress_ == trans) {
        requestInProgress_ = nullptr;
    }
    futex_unlock(&stateLock_);
    trans->release();
    return false;
}

uint64_t DRAMSysWrapper::normalizeAddress(uint64_t addr) const {
    uint64_t normalized = addr & 0x7fffffffffffffffULL;
    // normalize range
    if (addressRangeBytes_ > 0) {
        normalized %= addressRangeBytes_;
    }
    return normalized;
}

bool DRAMSysWrapper::popCompletion(Completion& out) {
    futex_lock(&stateLock_);
    if (completionQueue_.empty()) {
        futex_unlock(&stateLock_);
        return false;
    }

    out = completionQueue_.front();
    completionQueue_.pop_front();
    futex_unlock(&stateLock_);
    return true;
}

void DRAMSysWrapper::advanceOneCycle() {
    sc_core::sc_start(tCK_);
}

double DRAMSysWrapper::getTckNs() const {
    return tCK_.to_seconds() * 1e9;
}

bool DRAMSysWrapper::hasInflight() const {
    futex_lock(&stateLock_);
    bool inflight = (requestInProgress_ != nullptr);
    futex_unlock(&stateLock_);
    return inflight;
}

size_t DRAMSysWrapper::completionQueueSize() const {
    futex_lock(&stateLock_);
    size_t n = completionQueue_.size();
    futex_unlock(&stateLock_);
    return n;
}

tlm::tlm_sync_enum DRAMSysWrapper::nb_transport_bw(tlm::tlm_generic_payload& trans,
                                                   tlm::tlm_phase& phase,
                                                   sc_core::sc_time& delay) {
    peq_.notify(trans, phase, delay);
    return tlm::TLM_ACCEPTED;
}

void DRAMSysWrapper::peqCallback(tlm::tlm_generic_payload& trans, const tlm::tlm_phase& phase) {
    tlm::tlm_generic_payload* inProgress = nullptr;
    futex_lock(&stateLock_);
    inProgress = requestInProgress_;
    futex_unlock(&stateLock_);

    if (phase == tlm::END_REQ || (&trans == inProgress && phase == tlm::BEGIN_RESP)) {
        futex_lock(&stateLock_);
        if (requestInProgress_ == &trans) {
            requestInProgress_ = nullptr;
        }
        futex_unlock(&stateLock_);
    } else if (phase == tlm::BEGIN_REQ || phase == tlm::END_RESP) {
        panic("DRAMSysWrapper received illegal phase in peqCallback");
    }

    if (phase == tlm::BEGIN_RESP) {
        completeTransaction(trans);

        tlm::tlm_phase fwPhase = tlm::END_RESP;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        (void)iSocket_->nb_transport_fw(trans, fwPhase, delay);

        trans.release();
    }
}

void DRAMSysWrapper::completeTransaction(tlm::tlm_generic_payload& trans) {
    if (trans.is_response_error()) {
        panic("DRAMSysWrapper completion returned error response");
    }

    RequestExtension* ext = nullptr;
    trans.get_extension(ext);
    if (!ext) {
        panic("DRAMSysWrapper completion missing RequestExtension");
    }

    Completion c{};
    c.addr = ext->req.addr;
    c.isWrite = ext->req.isWrite;
    c.sourceId = ext->req.sourceId;
    c.reqTag = ext->req.reqTag;
    c.scTimePs = static_cast<uint64_t>(sc_core::sc_time_stamp().to_seconds() * 1e12);

    futex_lock(&stateLock_);
    completionQueue_.push_back(c);
    futex_unlock(&stateLock_);
}

#endif  // _WITH_DRAMSYS_

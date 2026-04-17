#include "ramulator2_wrapper.h"

#include <iomanip>
#include <mutex>
#include <deque>
#include <sstream>

#include "base/config.h"
#include "base/request.h"
#include "frontend/frontend.h"

namespace Ramulator {

struct Ramulator2Wrapper::CompletionQueue {
  std::mutex mu;
  struct Completion { int64_t addr; int type_id; int source_id; };
  std::deque<Completion> q;
};

std::string Ramulator2Wrapper::toString(const Request& req) {
  std::ostringstream ss;
  if (req.type_id == Request::Type::Read) {
    ss << "READ ";
  } else if (req.type_id == Request::Type::Write) {
    ss << "WRITE ";
  } else {
    ss << "TYPE" << req.type_id << " ";
  }
  ss << req.addr;
  return ss.str();
}

void Ramulator2Wrapper::maybe_log(const Request& r) {
  if (!debug_enabled_) return;
  // std::lock_guard<std::mutex> _g(trace_mu_);
  // if (trace_.good()) {
  //   trace_ << toString(r) << '\n';
  //   trace_.flush();
  // }
}

// ---- public API ----
Ramulator2Wrapper::Ramulator2Wrapper(const char* config_path) {
  auto config = Ramulator::Config::parse_config_file(config_path, {});

  // this will shout at us if we do not specify it
  frontend = Ramulator::Factory::create_frontend(config);
  memory_system = Ramulator::Factory::create_memory_system(config);

  frontend->connect_memory_system(memory_system);
  memory_system->connect_frontend(frontend);
  std::cout << "[RAMULATOR2]: config created successfully" << std::endl;
};

Ramulator2Wrapper::~Ramulator2Wrapper() {
  disable_debug();
  if (cq_) { delete cq_; cq_ = nullptr; }
}

float Ramulator2Wrapper::get_tCK() { return memory_system->get_tCK(); }

void Ramulator2Wrapper::tick() {
  current_tick++;
  memory_system->tick();
}

bool Ramulator2Wrapper::send(int64_t addr, bool is_write, int core_id) {
  // Ensure non-negative address for internal signed mapping
  if (addr < 0) addr &= 0x7fffffffffffffffLL;
  std::function<void(Request&)> cb = nullptr;
  if (callback) {
    cb = callback;
  } else if (cq_enabled_) {
    if (!cq_cb_) {
      // Lazily create internal queueing callback
      cq_cb_ = [this](Request& r) {
        if (!cq_) return; // should not happen, but be safe
        std::lock_guard<std::mutex> _g(cq_->mu);
        cq_->q.push_back({r.addr, r.type_id, r.source_id});
      };
    }
    cb = cq_cb_;
  }
  Request request = cb ? Request(addr, is_write, core_id, cb)
                       : Request(addr, is_write);
  maybe_log(request);
  return memory_system->send(request);
}

bool Ramulator2Wrapper::send(Request& request) {
  maybe_log(request);
  return memory_system->send(request);
}

void Ramulator2Wrapper::finish() { memory_system->finalize(); }

// void Ramulator2Wrapper::register_callback(
//     std::function<void(Request&)> callback) {
//   this->callback = callback;
// }

void Ramulator2Wrapper::enable_completion_queue() {
  cq_enabled_ = true;
  if (!cq_) cq_ = new CompletionQueue();
}

bool Ramulator2Wrapper::poll_completion(int64_t* addr, int* type_id, int* source_id) {
  if (!cq_enabled_) return false;
  if (!cq_) return false;
  std::lock_guard<std::mutex> _g(cq_->mu);
  if (cq_->q.empty()) return false;
  auto c = cq_->q.front();
  cq_->q.pop_front();
  if (addr) *addr = c.addr;
  if (type_id) *type_id = c.type_id;
  if (source_id) *source_id = c.source_id;
  return true;
}

// ---- debug controls ----
void Ramulator2Wrapper::enable_debug(const char* trace_path) {
  // std::lock_guard<std::mutex> _g(trace_mu_);
  // if (trace_.is_open()) trace_.close();
  // trace_.open(trace_path, std::ios::out | std::ios::app);
  // debug_enabled_ = trace_.is_open();
}

void Ramulator2Wrapper::disable_debug() {
  // std::lock_guard<std::mutex> _g(trace_mu_);
  // debug_enabled_ = false;
  // if (trace_.is_open()) trace_.close();
}

Ramulator2Wrapper* getRamulator2Wrapper(const char* config_path){
  return new Ramulator2Wrapper(config_path);
}


};  // namespace Ramulator

#include <string>
#include <cstdint>
#include <functional>

namespace Ramulator {
class IFrontEnd;
class IMemorySystem;
class Request;
};  // namespace Ramulator

namespace Ramulator {

class Ramulator2Wrapper {
 public:
  Ramulator2Wrapper(const char* config_path);
  ~Ramulator2Wrapper();

  // get 1/freq in ns
  float get_tCK();

  void tick();
  bool send(int64_t addr, bool is_write, int core_id = 0);
  bool send(Request& request);
  void finish();

  // void register_callback(std::function<void(Request&)> callback);

  // Completion queue API (avoid cross-DSO std::function):
  // If enabled, wrapper will attach an internal callback to each request that
  // pushes completions into an internal queue. The host can poll them via
  // poll_completion() without passing a std::function across the boundary.
  void enable_completion_queue();
  bool poll_completion(int64_t* addr, int* type_id, int* source_id);

  // Debug controls
  void enable_debug(const char* trace_path);
  void disable_debug();

 private:
  uint64_t current_tick = 0;
  IFrontEnd* frontend;
  IMemorySystem* memory_system;
  std::function<void(Request&)> callback;

  // Completion queue state (opaque to avoid pulling in <mutex> into this header)
  bool cq_enabled_ = false;
  std::function<void(Request&)> cq_cb_;
  struct CompletionQueue;  // defined in .cpp
  CompletionQueue* cq_ = nullptr;

  // Debug state
  bool debug_enabled_ = false;
  // std::ofstream trace_;

  // Helpers
  static std::string toString(const Request& r);
  void maybe_log(const Request& r);
};

Ramulator2Wrapper* getRamulator2Wrapper(const char* config_path);

};  // namespace Ramulator

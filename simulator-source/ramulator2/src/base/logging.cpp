#include "base/logging.h"


namespace Ramulator {

Logger_t Logging::create_logger(std::string name, std::string pattern) {
  const auto logger_name = "Ramulator::" + name;
  // the pattern will have no effect if the logger exists
  auto logger = spdlog::get(logger_name);
  if (logger) return logger;

  logger = spdlog::stdout_color_st(logger_name);
  if (!logger) {
    throw InitializationError("Error creating logger {}!", name);
  }

  logger->set_pattern(pattern);
  logger->set_level(spdlog::level::debug);
  return logger;
}

Logger_t Logging::get(std::string name) {
  auto logger = spdlog::get("Ramulator::" + name);
  if (logger) {
    return logger;
  } else {
    throw std::runtime_error(
      fmt::format(
        "Logger {} does not exist!",
        name
      )
    );
  }
}

bool Logging::_create_base_logger() {
  auto logger = create_logger("Base");
  if (logger) {
    return true;
  } else {
    throw InitializationError("Error creating the base logger!");
  }
  return false;
}

}        // namespace Ramulator

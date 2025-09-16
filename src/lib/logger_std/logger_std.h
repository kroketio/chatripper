#pragma once
#include <cstdio>
#include <string>
#include <chrono>
#include <iostream>

namespace globals {
  extern FILE* CLOCK_FILE;
  void logger_std_init();

  inline std::chrono::high_resolution_clock::time_point logger_std_start() {
    return std::chrono::high_resolution_clock::now();
  }

  void logger_std_end(const std::chrono::high_resolution_clock::time_point& start, const std::string& prefix);

#ifdef ENABLE_DEBUG_TIMINGS
#define CLOCK_MEASURE_START(start) auto (start) = globals::logger_std_start()
#else
#define CLOCK_MEASURE_START(start)
#endif
#ifdef ENABLE_DEBUG_TIMINGS
#define CLOCK_MEASURE_END(start, label) globals::logger_std_end(start, label)
#else
#define CLOCK_MEASURE_END(start, label)
#endif
}
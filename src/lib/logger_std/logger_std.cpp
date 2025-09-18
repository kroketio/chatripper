#include "logger_std.h"

namespace globals {
  FILE* CLOCK_FILE = nullptr;

  void logger_std_init() {
    const char* filePath = "/tmp/server.timings";
    CLOCK_FILE = fopen(filePath, "w");
    if (!CLOCK_FILE) {
      fprintf(stderr, "Failed to open %s for writing\n", filePath);
    }
  }

  void logger_std_end(const std::chrono::high_resolution_clock::time_point& start, const std::string& prefix) {
#ifdef ENABLE_DEBUG_TIMINGS
    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (CLOCK_FILE) {
      const std::time_t now = std::time(nullptr);
      char timeStr[9];  // HH:MM:SS + null terminator
      std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&now));

      constexpr size_t BUFFER_SIZE = 512;
      char logLine[BUFFER_SIZE];

      const int written = std::snprintf(
        logLine, BUFFER_SIZE, "[%s] %s: %lld ms\n",
        timeStr, prefix.c_str(), static_cast<long long>(duration.count()));

      if (written < 0) {
        // encoding error
      } else if (static_cast<size_t>(written) >= BUFFER_SIZE) {
        // ...truncated
      }

      fputs(logLine, CLOCK_FILE);
      fflush(CLOCK_FILE);

      fputs(logLine, stderr);
      fflush(stderr);
    }
#endif
  }
}

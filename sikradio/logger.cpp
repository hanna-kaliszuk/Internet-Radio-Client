#include "logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm *local_time = std::localtime(&now_time);

    std::ostringstream oss;
    oss << std::put_time(local_time, "%Y.%m.%d %H.%M.%S");
    return oss.str();
}

void log_message(int current_verbosity, VerbosityLevel target_level, const std::string &msg, bool prepend_timestamp) {
    // Sprawdzamy, czy ustawiony poziom pozwala na wypisanie tego komunikatu
    if (current_verbosity >= static_cast<int>(target_level)) {
        if (prepend_timestamp) {
            std::cerr << get_current_timestamp() << "\n";
        }
        std::cerr << msg << "\n";
    }
}

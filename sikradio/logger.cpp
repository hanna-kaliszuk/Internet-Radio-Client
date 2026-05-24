#include "logger.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

/**
 * @brief Generates a formatted timestamp string (YYYY.MM.DD HH.MM.SS).
 * @return The current timestamp as a string.
 */
std::string get_current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    const std::tm *local_time = std::localtime(&now_time);

    std::ostringstream oss;
    oss << std::put_time(local_time, "%Y.%m.%d %H.%M.%S");
    return oss.str();
}

/**
 * @brief Logs a message to stderr if the current verbosity meets the target level.
 * @param current_verbosity The verbosity level configured by the user.
 * @param target_level The required verbosity level to print this message.
 * @param msg The message content.
 * @param prepend_timestamp Whether to print the timestamp before the message.
 */
void log_message(int current_verbosity, VerbosityLevel target_level, const std::string &msg, bool prepend_timestamp) {
    if (current_verbosity >= static_cast<int>(target_level)) {
        if (prepend_timestamp) {
            std::cerr << get_current_timestamp() << "\n";
        }
        std::cerr << msg << "\n";
    }
}

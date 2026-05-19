#pragma once

#include <string>

enum class VerbosityLevel {
    NONE = 0,
    COMMON = 1,
    CRITICAL = 2,
    NON_CRITICAL = 3,
    DEBUG = 4
};

std::string get_current_timestamp();

void log_message(int current_verbosity, VerbosityLevel target_level, const std::string& msg, bool prepend_timestamp = false);
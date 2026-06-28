#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace redis {

inline int64_t nowNanos() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

inline std::string toUpper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    return s;
}

}  // namespace redis

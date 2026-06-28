#pragma once

#include "pubsub/hub.hpp"
#include "store/store.hpp"

#include <functional>
#include <string>
#include <vector>

namespace redis {

struct Executor {
    Store* store = nullptr;
    PubSubHub* hub = nullptr;
    std::function<bool()> saveSnapshot;

    // Returns true if connection should close.
    bool execute(int fd, const std::vector<std::string>& args);
};

}  // namespace redis

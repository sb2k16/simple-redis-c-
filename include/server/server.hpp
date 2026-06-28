#pragma once

#include "commands/executor.hpp"
#include "pubsub/hub.hpp"
#include "store/store.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace redis {

struct ServerConfig {
    std::string addr = "0.0.0.0";
    uint16_t port = 6380;
    size_t maxKeys = 10000;
    std::string snapshotPath = "data/dump.json";
    int snapshotEverySec = 30;
};

class Server {
public:
    explicit Server(ServerConfig cfg);
    ~Server();

    bool run();
    void shutdown();

private:
    ServerConfig cfg_;
    Store store_;
    PubSubHub hub_;
    Executor executor_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<bool> saveOnExit_{true};
    std::thread snapshotThread_;

    bool saveSnapshot();
    void snapshotLoop();
    void handleClient(int clientFd);
};

}  // namespace redis

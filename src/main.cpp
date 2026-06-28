#include "server/server.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    redis::ServerConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--addr" && i + 1 < argc) {
            cfg.addr = argv[++i];
        } else if (arg == "--max-keys" && i + 1 < argc) {
            cfg.maxKeys = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--snapshot" && i + 1 < argc) {
            cfg.snapshotPath = argv[++i];
        } else if (arg == "--snapshot-every" && i + 1 < argc) {
            cfg.snapshotEverySec = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: simple-redis [--port 6380] [--max-keys 10000] [--snapshot data/dump.json]\n";
            return 0;
        }
    }

    redis::Server server(cfg);
    if (!server.run()) return 1;
    return 0;
}

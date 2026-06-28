#include "server/server.hpp"

#include "common/util.hpp"
#include "persistence/snapshot.hpp"
#include "protocol/resp.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace redis {

namespace {
Server* gServer = nullptr;

void onSignal(int) {
    if (gServer) gServer->shutdown();
}

void drainMessages(int fd, const std::unordered_map<std::string, std::shared_ptr<SubQueue>>& subs) {
    for (const auto& [channel, q] : subs) {
        while (true) {
            std::string msg;
            {
                std::lock_guard<std::mutex> lock(q->mu);
                if (q->msgs.empty()) break;
                msg = std::move(q->msgs.front());
                q->msgs.pop_front();
            }
            respWritePubSubMessage(fd, channel, msg);
        }
    }
}

bool waitReadable(int fd, double timeoutSec) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv{};
    tv.tv_sec = static_cast<long>(timeoutSec);
    tv.tv_usec = static_cast<long>((timeoutSec - static_cast<long>(timeoutSec)) * 1e6);
    return select(fd + 1, &rfds, nullptr, nullptr, &tv) > 0;
}

}  // namespace

Server::Server(ServerConfig cfg)
    : cfg_(std::move(cfg)),
      store_(StoreConfig{cfg_.maxKeys}),
      hub_(),
      executor_{&store_, &hub_, nullptr} {
    executor_.saveSnapshot = [this]() { return saveSnapshot(); };
}

Server::~Server() {
    shutdown();
}

bool Server::saveSnapshot() {
    return redis::saveSnapshot(cfg_.snapshotPath, store_.snapshot());
}

void Server::snapshotLoop() {
    while (running_) {
        for (int i = 0; i < cfg_.snapshotEverySec * 10 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (running_) saveSnapshot();
    }
}

bool Server::run() {
    bool found = false;
    std::unordered_map<std::string, Value> loaded;
    if (!loadSnapshot(cfg_.snapshotPath, loaded, found)) {
        std::cerr << "failed to load snapshot\n";
        return false;
    }
    if (found) {
        store_.load(loaded);
        std::cerr << "loaded snapshot from " << cfg_.snapshotPath << " (" << store_.keyCount() << " keys)\n";
    } else {
        std::cerr << "no snapshot at " << cfg_.snapshotPath << ", starting fresh\n";
    }

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::perror("socket");
        return false;
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg_.port);
    if (cfg_.addr == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, cfg_.addr.c_str(), &addr.sin_addr);
    }

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return false;
    }
    if (::listen(listenFd_, 128) < 0) {
        std::perror("listen");
        return false;
    }

    running_ = true;
    gServer = this;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    snapshotThread_ = std::thread([this]() { snapshotLoop(); });

    std::cerr << "simple-redis listening on " << cfg_.addr << ":" << cfg_.port
              << " (max keys: " << cfg_.maxKeys << ")\n";

    while (running_) {
        int client = ::accept(listenFd_, nullptr, nullptr);
        if (client < 0) {
            if (!running_) break;
            std::perror("accept");
            continue;
        }
        std::thread([this, client]() { handleClient(client); }).detach();
    }

    if (snapshotThread_.joinable()) snapshotThread_.join();
    if (saveOnExit_) saveSnapshot();
    if (listenFd_ >= 0) ::close(listenFd_);
    return true;
}

void Server::shutdown() {
    if (!running_.exchange(false)) return;
    if (listenFd_ >= 0) ::close(listenFd_);
}

void Server::handleClient(int fd) {
    RespReader reader(fd);
    bool pubsubMode = false;
    std::unordered_map<std::string, std::shared_ptr<SubQueue>> subs;

    while (running_) {
        if (pubsubMode) {
            drainMessages(fd, subs);
            if (!waitReadable(fd, 0.05)) continue;
        }

        std::vector<std::string> args;
        try {
            if (!reader.readCommand(args)) break;
        } catch (...) {
            break;
        }
        if (args.empty()) continue;

        std::string cmd = toUpper(args[0]);

        if (cmd == "SUBSCRIBE") {
            if (args.size() < 2) {
                respWriteError(fd, "wrong number of arguments for SUBSCRIBE");
                continue;
            }
            pubsubMode = true;
            for (size_t i = 1; i < args.size(); ++i) {
                const std::string& ch = args[i];
                if (!subs.count(ch)) subs[ch] = hub_.subscribe(ch);
                respWriteArray(fd, {"subscribe", ch, "1"});
            }
            continue;
        }

        if (cmd == "UNSUBSCRIBE") {
            std::vector<std::string> channels;
            if (args.size() == 1) {
                for (const auto& [ch, _] : subs) channels.push_back(ch);
            } else {
                channels.assign(args.begin() + 1, args.end());
            }
            for (const auto& ch : channels) {
                auto it = subs.find(ch);
                if (it != subs.end()) {
                    hub_.unsubscribe(ch, it->second);
                    subs.erase(it);
                }
                respWriteArray(fd, {"unsubscribe", ch, "0"});
            }
            if (subs.empty()) pubsubMode = false;
            continue;
        }

        if (cmd == "QUIT") {
            respWriteSimple(fd, "OK");
            break;
        }

        if (pubsubMode) {
            if (cmd == "PING") {
                respWriteSimple(fd, "PONG");
                continue;
            }
            respWriteError(fd, "only SUBSCRIBE, UNSUBSCRIBE, PING, QUIT allowed in subscribed mode");
            continue;
        }

        if (executor_.execute(fd, args)) break;
    }

    for (auto& [ch, q] : subs) hub_.unsubscribe(ch, q);
    ::close(fd);
}

}  // namespace redis

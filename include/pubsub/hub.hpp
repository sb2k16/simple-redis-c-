#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace redis {

struct SubQueue {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> msgs;
    bool closed = false;
};

class PubSubHub {
public:
    std::shared_ptr<SubQueue> subscribe(const std::string& channel);
    void unsubscribe(const std::string& channel, const std::shared_ptr<SubQueue>& q);
    int publish(const std::string& channel, const std::string& message);

private:
    std::mutex mu_;
    std::unordered_map<std::string, std::vector<std::weak_ptr<SubQueue>>> subs_;
};

}  // namespace redis

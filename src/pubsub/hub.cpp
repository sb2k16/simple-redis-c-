#include "pubsub/hub.hpp"

#include <algorithm>

namespace redis {

std::shared_ptr<SubQueue> PubSubHub::subscribe(const std::string& channel) {
    auto q = std::make_shared<SubQueue>();
    std::lock_guard<std::mutex> lock(mu_);
    subs_[channel].push_back(q);
    return q;
}

void PubSubHub::unsubscribe(const std::string& channel, const std::shared_ptr<SubQueue>& q) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = subs_.find(channel);
    if (it == subs_.end()) return;
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](const std::weak_ptr<SubQueue>& w) {
                                 auto s = w.lock();
                                 return !s || s == q;
                             }),
              vec.end());
    if (vec.empty()) subs_.erase(it);

    std::lock_guard<std::mutex> qlock(q->mu);
    q->closed = true;
    q->cv.notify_all();
}

int PubSubHub::publish(const std::string& channel, const std::string& message) {
    std::vector<std::shared_ptr<SubQueue>> targets;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = subs_.find(channel);
        if (it == subs_.end()) return 0;
        for (auto& w : it->second) {
            if (auto s = w.lock()) targets.push_back(s);
        }
    }
    int count = 0;
    for (auto& q : targets) {
        std::lock_guard<std::mutex> lock(q->mu);
        if (q->closed) continue;
        if (q->msgs.size() >= 256) continue;
        q->msgs.push_back(message);
        q->cv.notify_one();
        ++count;
    }
    return count;
}

}  // namespace redis

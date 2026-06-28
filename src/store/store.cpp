#include "store/store.hpp"

#include "common/util.hpp"

#include <thread>

namespace redis {

Store::Store(StoreConfig cfg) : cfg_(cfg) {
    std::thread([this]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lock(mu_);
            evictExpired();
        }
    }).detach();
}

void Store::touch(const std::string& key) {
    auto it = lruIndex_.find(key);
    if (it != lruIndex_.end()) {
        lru_.erase(it->second);
        lruIndex_.erase(it);
    }
    lru_.push_front(key);
    lruIndex_[key] = lru_.begin();
}

void Store::removeKey(const std::string& key) {
    data_.erase(key);
    auto it = lruIndex_.find(key);
    if (it != lruIndex_.end()) {
        lru_.erase(it->second);
        lruIndex_.erase(it);
    }
}

void Store::evictExpired() {
    int64_t now = nowNanos();
    std::vector<std::string> expired;
    expired.reserve(data_.size());
    for (const auto& [k, v] : data_) {
        if (v.isExpired(now)) expired.push_back(k);
    }
    for (const auto& k : expired) removeKey(k);
}

void Store::enforceLimit() {
    while (lru_.size() > cfg_.maxKeys) {
        const std::string oldest = lru_.back();
        removeKey(oldest);
    }
}

Value* Store::getValue(const std::string& key, bool touchLru) {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    int64_t now = nowNanos();
    if (it->second.isExpired(now)) {
        removeKey(key);
        return nullptr;
    }
    if (touchLru) touch(key);
    return &it->second;
}

Value* Store::listKey(const std::string& key) {
    if (auto* v = getValue(key, false)) {
        if (v->type == ValueType::List) {
            touch(key);
            return v;
        }
        v->type = ValueType::List;
        v->str.clear();
        v->list.clear();
        v->zset.clear();
        touch(key);
        return v;
    }
    auto& v = data_[key];
    v = Value{};
    v.type = ValueType::List;
    touch(key);
    enforceLimit();
    return &v;
}

Value* Store::zsetKey(const std::string& key) {
    if (auto* v = getValue(key, false)) {
        if (v->type == ValueType::ZSet) {
            touch(key);
            return v;
        }
        v->type = ValueType::ZSet;
        v->str.clear();
        v->list.clear();
        v->zset.clear();
        touch(key);
        return v;
    }
    auto& v = data_[key];
    v = Value{};
    v.type = ValueType::ZSet;
    touch(key);
    enforceLimit();
    return &v;
}

void Store::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = data_.find(key);
    if (it != data_.end() && it->second.type != ValueType::String) {
        it->second.type = ValueType::String;
        it->second.str = value;
        it->second.list.clear();
        it->second.zset.clear();
    } else if (it != data_.end()) {
        it->second.str = value;
    } else {
        Value v;
        v.type = ValueType::String;
        v.str = value;
        data_[key] = std::move(v);
    }
    touch(key);
    enforceLimit();
}

bool Store::get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v || v->type != ValueType::String) return false;
    value = v->str;
    return true;
}

int Store::del(const std::vector<std::string>& keys) {
    std::lock_guard<std::mutex> lock(mu_);
    int count = 0;
    for (const auto& key : keys) {
        if (getValue(key, false)) {
            removeKey(key);
            ++count;
        }
    }
    return count;
}

bool Store::expire(const std::string& key, int64_t seconds) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v) return false;
    if (seconds <= 0) {
        v->expiresAt = 0;
    } else {
        v->expiresAt = nowNanos() + seconds * int64_t(1e9);
    }
    return true;
}

int64_t Store::ttl(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v) return -2;
    if (v->expiresAt == 0) return -1;
    int64_t rem = (v->expiresAt - nowNanos()) / int64_t(1e9);
    return rem < 0 ? -2 : rem;
}

int Store::lpush(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = listKey(key);
    std::vector<std::string> rev(values.rbegin(), values.rend());
    v->list.insert(v->list.begin(), rev.begin(), rev.end());
    return static_cast<int>(v->list.size());
}

int Store::rpush(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = listKey(key);
    v->list.insert(v->list.end(), values.begin(), values.end());
    return static_cast<int>(v->list.size());
}

bool Store::lpop(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v || v->type != ValueType::List || v->list.empty()) return false;
    value = v->list.front();
    v->list.erase(v->list.begin());
    if (v->list.empty()) {
        removeKey(key);
    }
    return true;
}

bool Store::rpop(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v || v->type != ValueType::List || v->list.empty()) return false;
    value = v->list.back();
    v->list.pop_back();
    if (v->list.empty()) {
        removeKey(key);
    }
    return true;
}

std::vector<std::string> Store::lrange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v || v->type != ValueType::List) return {};
    int n = static_cast<int>(v->list.size());
    if (n == 0) return {};
    if (start < 0) start = n + start;
    if (stop < 0) stop = n + stop;
    if (start < 0) start = 0;
    if (stop >= n) stop = n - 1;
    if (start > stop || start >= n) return {};
    return std::vector<std::string>(v->list.begin() + start, v->list.begin() + stop + 1);
}

int Store::llen(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v || v->type != ValueType::List) return 0;
    return static_cast<int>(v->list.size());
}

bool Store::zadd(const std::string& key, double score, const std::string& member) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = zsetKey(key);
    return v->zAdd(member, score);
}

bool Store::zrem(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v || v->type != ValueType::ZSet) return false;
    if (!v->zRem(member)) return false;
    if (v->zset.empty()) {
        removeKey(key);
    }
    return true;
}

std::vector<ZMember> Store::zrange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v || v->type != ValueType::ZSet) return {};
    return v->zRange(start, stop);
}

bool Store::zscore(const std::string& key, const std::string& member, double& score) {
    std::lock_guard<std::mutex> lock(mu_);
    auto* v = getValue(key);
    if (!v || v->type != ValueType::ZSet) return false;
    return v->zScore(member, score);
}

std::unordered_map<std::string, Value> Store::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    int64_t now = nowNanos();
    std::unordered_map<std::string, Value> out;
    for (const auto& [k, v] : data_) {
        if (v.isExpired(now)) continue;
        out[k] = v;
    }
    return out;
}

void Store::load(const std::unordered_map<std::string, Value>& data) {
    std::lock_guard<std::mutex> lock(mu_);
    data_.clear();
    lru_.clear();
    lruIndex_.clear();
    int64_t now = nowNanos();
    for (const auto& [k, v] : data) {
        if (v.isExpired(now)) continue;
        data_[k] = v;
        touch(k);
    }
}

size_t Store::keyCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return data_.size();
}

}  // namespace redis

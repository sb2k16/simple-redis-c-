#pragma once

#include "store/value.hpp"

#include <cstddef>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace redis {

struct StoreConfig {
    size_t maxKeys = 10000;
};

class Store {
public:
    explicit Store(StoreConfig cfg = {});

    void set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    int del(const std::vector<std::string>& keys);
    bool expire(const std::string& key, int64_t seconds);
    int64_t ttl(const std::string& key);

    int lpush(const std::string& key, const std::vector<std::string>& values);
    int rpush(const std::string& key, const std::vector<std::string>& values);
    bool lpop(const std::string& key, std::string& value);
    bool rpop(const std::string& key, std::string& value);
    std::vector<std::string> lrange(const std::string& key, int start, int stop);
    int llen(const std::string& key);

    bool zadd(const std::string& key, double score, const std::string& member);
    bool zrem(const std::string& key, const std::string& member);
    std::vector<ZMember> zrange(const std::string& key, int start, int stop);
    bool zscore(const std::string& key, const std::string& member, double& score);

    std::unordered_map<std::string, Value> snapshot() const;
    void load(const std::unordered_map<std::string, Value>& data);
    size_t keyCount() const;

private:
    mutable std::mutex mu_;
    StoreConfig cfg_;
    std::unordered_map<std::string, Value> data_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lruIndex_;

    void touch(const std::string& key);
    void removeKey(const std::string& key);
    void enforceLimit();
    Value* getValue(const std::string& key, bool touchLru = true);
    Value* listKey(const std::string& key);
    Value* zsetKey(const std::string& key);
    void evictExpired();
};

}  // namespace redis

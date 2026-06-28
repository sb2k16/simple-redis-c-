#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace redis {

enum class ValueType { String, List, ZSet };

struct ZMember {
    std::string member;
    double score = 0.0;
};

struct Value {
    ValueType type = ValueType::String;
    std::string str;
    std::vector<std::string> list;
    std::vector<ZMember> zset;
    int64_t expiresAt = 0;  // 0 = no expiry

    bool isExpired(int64_t now) const;
    void sortZSet();
    int zIndex(const std::string& member) const;
    bool zAdd(const std::string& member, double score);
    bool zRem(const std::string& member);
    bool zScore(const std::string& member, double& score) const;
    std::vector<ZMember> zRange(int start, int stop) const;
};

}  // namespace redis

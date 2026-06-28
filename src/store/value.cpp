#include "store/value.hpp"

#include <algorithm>

namespace redis {

bool Value::isExpired(int64_t now) const {
    return expiresAt > 0 && now >= expiresAt;
}

void Value::sortZSet() {
    std::sort(zset.begin(), zset.end(), [](const ZMember& a, const ZMember& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.member < b.member;
    });
}

int Value::zIndex(const std::string& member) const {
    for (size_t i = 0; i < zset.size(); ++i) {
        if (zset[i].member == member) return static_cast<int>(i);
    }
    return -1;
}

bool Value::zAdd(const std::string& member, double score) {
    int idx = zIndex(member);
    if (idx >= 0) {
        zset[idx].score = score;
        sortZSet();
        return false;
    }
    zset.push_back({member, score});
    sortZSet();
    return true;
}

bool Value::zRem(const std::string& member) {
    int idx = zIndex(member);
    if (idx < 0) return false;
    zset.erase(zset.begin() + idx);
    return true;
}

bool Value::zScore(const std::string& member, double& score) const {
    int idx = zIndex(member);
    if (idx < 0) return false;
    score = zset[idx].score;
    return true;
}

std::vector<ZMember> Value::zRange(int start, int stop) const {
    int n = static_cast<int>(zset.size());
    if (n == 0) return {};
    if (start < 0) start = n + start;
    if (stop < 0) stop = n + stop;
    if (start < 0) start = 0;
    if (stop >= n) stop = n - 1;
    if (start > stop || start >= n) return {};
    return std::vector<ZMember>(zset.begin() + start, zset.begin() + stop + 1);
}

}  // namespace redis

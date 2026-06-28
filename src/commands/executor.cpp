#include "commands/executor.hpp"

#include "common/util.hpp"
#include "protocol/resp.hpp"

#include <cstdlib>

namespace redis {

static int parseInt(const std::string& s) {
    return std::stoi(s);
}

static double parseDouble(const std::string& s) {
    return std::stod(s);
}

bool Executor::execute(int fd, const std::vector<std::string>& args) {
    if (args.empty()) {
        respWriteError(fd, "empty command");
        return false;
    }
    const std::string cmd = toUpper(args[0]);

    if (cmd == "PING") {
        respWriteSimple(fd, "PONG");
        return false;
    }
    if (cmd == "SET") {
        if (args.size() != 3) { respWriteError(fd, "wrong number of arguments for SET"); return false; }
        store->set(args[1], args[2]);
        respWriteSimple(fd, "OK");
        return false;
    }
    if (cmd == "GET") {
        if (args.size() != 2) { respWriteError(fd, "wrong number of arguments for GET"); return false; }
        std::string val;
        if (!store->get(args[1], val)) { respWriteNullBulk(fd); return false; }
        respWriteBulk(fd, val);
        return false;
    }
    if (cmd == "DEL") {
        if (args.size() < 2) { respWriteError(fd, "wrong number of arguments for DEL"); return false; }
        respWriteInt(fd, store->del(std::vector<std::string>(args.begin() + 1, args.end())));
        return false;
    }
    if (cmd == "EXPIRE") {
        if (args.size() != 3) { respWriteError(fd, "wrong number of arguments for EXPIRE"); return false; }
        respWriteInt(fd, store->expire(args[1], parseInt(args[2])) ? 1 : 0);
        return false;
    }
    if (cmd == "TTL") {
        if (args.size() != 2) { respWriteError(fd, "wrong number of arguments for TTL"); return false; }
        respWriteInt(fd, store->ttl(args[1]));
        return false;
    }
    if (cmd == "LPUSH") {
        if (args.size() < 3) { respWriteError(fd, "wrong number of arguments for LPUSH"); return false; }
        respWriteInt(fd, store->lpush(args[1], std::vector<std::string>(args.begin() + 2, args.end())));
        return false;
    }
    if (cmd == "RPUSH") {
        if (args.size() < 3) { respWriteError(fd, "wrong number of arguments for RPUSH"); return false; }
        respWriteInt(fd, store->rpush(args[1], std::vector<std::string>(args.begin() + 2, args.end())));
        return false;
    }
    if (cmd == "LPOP") {
        if (args.size() != 2) { respWriteError(fd, "wrong number of arguments for LPOP"); return false; }
        std::string val;
        if (!store->lpop(args[1], val)) { respWriteNullBulk(fd); return false; }
        respWriteBulk(fd, val);
        return false;
    }
    if (cmd == "RPOP") {
        if (args.size() != 2) { respWriteError(fd, "wrong number of arguments for RPOP"); return false; }
        std::string val;
        if (!store->rpop(args[1], val)) { respWriteNullBulk(fd); return false; }
        respWriteBulk(fd, val);
        return false;
    }
    if (cmd == "LRANGE") {
        if (args.size() != 4) { respWriteError(fd, "wrong number of arguments for LRANGE"); return false; }
        auto vals = store->lrange(args[1], parseInt(args[2]), parseInt(args[3]));
        respWriteArray(fd, vals);
        return false;
    }
    if (cmd == "LLEN") {
        if (args.size() != 2) { respWriteError(fd, "wrong number of arguments for LLEN"); return false; }
        respWriteInt(fd, store->llen(args[1]));
        return false;
    }
    if (cmd == "ZADD") {
        if (args.size() != 4) { respWriteError(fd, "wrong number of arguments for ZADD"); return false; }
        respWriteInt(fd, store->zadd(args[1], parseDouble(args[2]), args[3]) ? 1 : 0);
        return false;
    }
    if (cmd == "ZREM") {
        if (args.size() != 3) { respWriteError(fd, "wrong number of arguments for ZREM"); return false; }
        respWriteInt(fd, store->zrem(args[1], args[2]) ? 1 : 0);
        return false;
    }
    if (cmd == "ZRANGE") {
        if (args.size() != 4) { respWriteError(fd, "wrong number of arguments for ZRANGE"); return false; }
        auto members = store->zrange(args[1], parseInt(args[2]), parseInt(args[3]));
        std::vector<std::string> out;
        for (const auto& m : members) out.push_back(m.member);
        respWriteArray(fd, out);
        return false;
    }
    if (cmd == "ZSCORE") {
        if (args.size() != 3) { respWriteError(fd, "wrong number of arguments for ZSCORE"); return false; }
        double score = 0;
        if (!store->zscore(args[1], args[2], score)) { respWriteNullBulk(fd); return false; }
        respWriteBulk(fd, std::to_string(score));
        return false;
    }
    if (cmd == "PUBLISH") {
        if (args.size() != 3) { respWriteError(fd, "wrong number of arguments for PUBLISH"); return false; }
        respWriteInt(fd, hub->publish(args[1], args[2]));
        return false;
    }
    if (cmd == "SAVE") {
        if (!saveSnapshot || !saveSnapshot()) {
            respWriteError(fd, "snapshot failed");
            return false;
        }
        respWriteSimple(fd, "OK");
        return false;
    }
    if (cmd == "DBSIZE") {
        respWriteInt(fd, static_cast<int64_t>(store->keyCount()));
        return false;
    }
    if (cmd == "QUIT") {
        respWriteSimple(fd, "OK");
        return true;
    }
    respWriteError(fd, "unknown command '" + cmd + "'");
    return false;
}

}  // namespace redis

#include "protocol/resp.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

namespace redis {

namespace {

ssize_t readFull(int fd, char* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::read(fd, buf + off, len - off);
        if (n == 0) return 0;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(off);
}

}  // namespace

RespReader::RespReader(int fd) : fd_(fd) {}

std::string RespReader::readLine() {
    std::string line;
    char c;
    while (true) {
        ssize_t n = ::read(fd_, &c, 1);
        if (n <= 0) throw std::runtime_error("connection closed");
        if (c == '\r') continue;
        if (c == '\n') break;
        line.push_back(c);
    }
    return line;
}

std::string RespReader::readBulk(int n) {
    if (n < 0) return {};
    std::string buf(n + 2, '\0');
    ssize_t got = readFull(fd_, buf.data(), static_cast<size_t>(n + 2));
    if (got <= 0) throw std::runtime_error("bulk read failed");
    return buf.substr(0, n);
}

bool RespReader::readArray(std::vector<std::string>& out, int count) {
    out.clear();
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        std::string bulk;
        int64_t num = 0;
        std::vector<std::string> nested;
        char type = 0;
        if (!readValue(bulk, num, nested, type)) return false;
        if (type != '$') return false;
        out.push_back(bulk);
    }
    return true;
}

bool RespReader::readValue(std::string& bulkOut, int64_t& intOut, std::vector<std::string>& arrayOut, char& type) {
    std::string line = readLine();
    if (line.empty()) return false;
    type = line[0];
    switch (type) {
        case '+':
        case '-':
            bulkOut = line.substr(1);
            return true;
        case ':':
            intOut = std::stoll(line.substr(1));
            return true;
        case '$': {
            int n = std::stoi(line.substr(1));
            bulkOut = readBulk(n);
            return true;
        }
        case '*': {
            int n = std::stoi(line.substr(1));
            return readArray(arrayOut, n);
        }
        default:
            return false;
    }
}

bool RespReader::readCommand(std::vector<std::string>& args) {
    std::string line = readLine();
    if (line.empty() || line[0] != '*') return false;
    int n = std::stoi(line.substr(1));
    return readArray(args, n);
}

static void writeAll(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::write(fd, s.data() + off, s.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("write failed");
        }
        off += static_cast<size_t>(n);
    }
}

void respWriteSimple(int fd, const std::string& s) {
    writeAll(fd, "+" + s + "\r\n");
}

void respWriteError(int fd, const std::string& msg) {
    writeAll(fd, "-ERR " + msg + "\r\n");
}

void respWriteInt(int fd, int64_t n) {
    writeAll(fd, ":" + std::to_string(n) + "\r\n");
}

void respWriteBulk(int fd, const std::string& s) {
    writeAll(fd, "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n");
}

void respWriteNullBulk(int fd) {
    writeAll(fd, "$-1\r\n");
}

void respWriteArray(int fd, const std::vector<std::string>& items) {
    std::string out = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        out += "$" + std::to_string(item.size()) + "\r\n" + item + "\r\n";
    }
    writeAll(fd, out);
}

void respWritePubSubMessage(int fd, const std::string& channel, const std::string& message) {
    std::vector<std::string> parts = {"message", channel, message};
    respWriteArray(fd, parts);
}

}  // namespace redis

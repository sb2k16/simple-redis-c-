#include <arpa/inet.h>
#include <cstdio>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static void writeAll(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::write(fd, s.data() + off, s.size() - off);
        if (n < 0) return;
        off += static_cast<size_t>(n);
    }
}

static void writeArray(int fd, const std::vector<std::string>& args) {
    writeAll(fd, "*" + std::to_string(args.size()) + "\r\n");
    for (const auto& a : args) {
        writeAll(fd, "$" + std::to_string(a.size()) + "\r\n" + a + "\r\n");
    }
}

static std::string readLine(int fd) {
    std::string line;
    char c;
    while (read(fd, &c, 1) == 1) {
        if (c == '\r') continue;
        if (c == '\n') break;
        line += c;
    }
    return line;
}

static bool readResponse(int fd, bool keepOpen) {
    std::string line = readLine(fd);
    if (line.empty()) return false;
    switch (line[0]) {
        case '+':
        case '-':
        case ':':
            std::cout << line.substr(1) << '\n';
            return true;
        case '$':
            if (line == "$-1") {
                std::cout << "(nil)\n";
                return true;
            }
            {
                int n = std::stoi(line.substr(1));
                std::string buf(n + 2, '\0');
                read(fd, buf.data(), n + 2);
                std::cout << buf.substr(0, n) << '\n';
            }
            return true;
        case '*': {
            int n = std::stoi(line.substr(1));
            for (int i = 0; i < n; ++i) {
                std::string l = readLine(fd);
                if (l[0] == '$') {
                    int sz = std::stoi(l.substr(1));
                    std::string data(sz + 2, '\0');
                    read(fd, data.data(), sz + 2);
                    std::cout << (i + 1) << ") " << data.substr(0, sz) << '\n';
                }
            }
            return keepOpen;
        }
        default:
            return false;
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string host = "127.0.0.1";
    uint16_t port = 6380;
    int argi = 1;

    if (argc > 1 && std::string(argv[1]).find(':') != std::string::npos) {
        std::string addr = argv[1];
        auto pos = addr.find(':');
        host = addr.substr(0, pos);
        port = static_cast<uint16_t>(std::stoi(addr.substr(pos + 1)));
        argi = 2;
    }

    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) {
        std::cerr << "resolve failed\n";
        return 1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        std::perror("connect");
        return 1;
    }
    freeaddrinfo(res);

    if (argc > argi) {
        std::vector<std::string> args;
        for (int i = argi; i < argc; ++i) args.emplace_back(argv[i]);
        writeArray(fd, args);
        bool subscribe = !args.empty() && (args[0] == "SUBSCRIBE" || args[0] == "subscribe");
        while (readResponse(fd, subscribe)) {
            if (!subscribe) break;
        }
        close(fd);
        return 0;
    }

    std::cerr << "Connected to simple-redis at " << host << ":" << port << '\n';
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        std::vector<std::string> args;
        std::string cur;
        for (char c : line) {
            if (c == ' ') {
                if (!cur.empty()) { args.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) args.push_back(cur);
        writeArray(fd, args);
        bool subscribe = !args.empty() && (args[0] == "SUBSCRIBE" || args[0] == "subscribe");
        while (readResponse(fd, subscribe)) {
            if (!subscribe) break;
        }
    }
    close(fd);
    return 0;
}

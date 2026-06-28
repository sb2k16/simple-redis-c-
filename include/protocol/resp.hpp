#pragma once

#include <string>
#include <vector>

namespace redis {

class RespReader {
public:
    explicit RespReader(int fd);

    bool readCommand(std::vector<std::string>& args);
    std::string readLine();

private:
    int fd_;
    std::string readBulk(int n);

    bool readValue(std::string& bulkOut, int64_t& intOut, std::vector<std::string>& arrayOut, char& type);
    bool readArray(std::vector<std::string>& out, int count);
};

void respWriteSimple(int fd, const std::string& s);
void respWriteError(int fd, const std::string& msg);
void respWriteInt(int fd, int64_t n);
void respWriteBulk(int fd, const std::string& s);
void respWriteNullBulk(int fd);
void respWriteArray(int fd, const std::vector<std::string>& items);
void respWritePubSubMessage(int fd, const std::string& channel, const std::string& message);

}  // namespace redis

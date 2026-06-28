#include "persistence/snapshot.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace redis {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

void writeValue(std::ostream& os, const Value& v) {
    os << "{\n";
    os << "      \"type\": ";
    switch (v.type) {
        case ValueType::String: os << "\"string\""; break;
        case ValueType::List: os << "\"list\""; break;
        case ValueType::ZSet: os << "\"zset\""; break;
    }
    os << ",\n";
    if (v.type == ValueType::String) {
        os << "      \"string\": \"" << jsonEscape(v.str) << "\"";
    } else if (v.type == ValueType::List) {
        os << "      \"list\": [";
        for (size_t i = 0; i < v.list.size(); ++i) {
            if (i) os << ", ";
            os << "\"" << jsonEscape(v.list[i]) << "\"";
        }
        os << "]";
    } else {
        os << "      \"zset\": [";
        for (size_t i = 0; i < v.zset.size(); ++i) {
            if (i) os << ", ";
            os << "{\"member\": \"" << jsonEscape(v.zset[i].member) << "\", \"score\": "
               << v.zset[i].score << "}";
        }
        os << "]";
    }
    if (v.expiresAt > 0) {
        os << ",\n      \"expires_at\": " << v.expiresAt;
    }
    os << "\n    }";
}

std::string readString(const std::string& json, size_t& pos) {
    while (pos < json.size() && json[pos] != '"') ++pos;
    if (pos >= json.size()) return {};
    ++pos;
    std::string out;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < json.size()) {
            char e = json[pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += e;
            }
        } else {
            out += c;
        }
    }
    return out;
}

size_t skipWs(const std::string& json, size_t pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t' || json[pos] == ','))
        ++pos;
    return pos;
}

bool parseValue(const std::string& json, size_t& pos, Value& v) {
    pos = skipWs(json, pos);
    if (pos >= json.size() || json[pos] != '{') return false;
    ++pos;
    while (pos < json.size()) {
        pos = skipWs(json, pos);
        if (json[pos] == '}') { ++pos; return true; }
        if (json[pos] != '"') return false;
        std::string key = readString(json, pos);
        pos = skipWs(json, pos);
        if (json[pos] != ':') return false;
        ++pos;
        pos = skipWs(json, pos);
        if (key == "type") {
            std::string t = readString(json, pos);
            if (t == "string") v.type = ValueType::String;
            else if (t == "list") v.type = ValueType::List;
            else if (t == "zset") v.type = ValueType::ZSet;
        } else if (key == "string") {
            v.str = readString(json, pos);
        } else if (key == "list") {
            pos = skipWs(json, pos);
            if (json[pos] != '[') return false;
            ++pos;
            while (pos < json.size()) {
                pos = skipWs(json, pos);
                if (json[pos] == ']') { ++pos; break; }
                v.list.push_back(readString(json, pos));
            }
        } else if (key == "zset") {
            pos = skipWs(json, pos);
            if (json[pos] != '[') return false;
            ++pos;
            while (pos < json.size()) {
                pos = skipWs(json, pos);
                if (json[pos] == ']') { ++pos; break; }
                if (json[pos] != '{') return false;
                ++pos;
                ZMember m;
                while (pos < json.size() && json[pos] != '}') {
                    pos = skipWs(json, pos);
                    std::string zk = readString(json, pos);
                    pos = skipWs(json, pos);
                    ++pos;
                    pos = skipWs(json, pos);
                    if (zk == "member") m.member = readString(json, pos);
                    else if (zk == "score") m.score = std::stod(json.substr(pos));
                    while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
                    if (pos < json.size() && json[pos] == ',') ++pos;
                }
                if (pos < json.size() && json[pos] == '}') ++pos;
                v.zset.push_back(m);
            }
            v.sortZSet();
        } else if (key == "expires_at") {
            v.expiresAt = std::stoll(json.substr(pos));
            while (pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))) ++pos;
        } else {
            while (pos < json.size() && json[pos] != ',' && json[pos] != '}') ++pos;
        }
    }
    return false;
}

}  // namespace

bool saveSnapshot(const std::string& path, const std::unordered_map<std::string, Value>& data) {
    std::string tmp = path + ".tmp";
    std::ofstream os(tmp);
    if (!os) return false;
    os << "{\n  \"keys\": {\n";
    bool first = true;
    for (const auto& [k, v] : data) {
        if (!first) os << ",\n";
        first = false;
        os << "    \"" << jsonEscape(k) << "\": ";
        writeValue(os, v);
    }
    os << "\n  }\n}\n";
    os.close();
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool loadSnapshot(const std::string& path, std::unordered_map<std::string, Value>& data, bool& found) {
    found = false;
    std::ifstream is(path);
    if (!is) return true;
    found = true;
    std::stringstream ss;
    ss << is.rdbuf();
    std::string json = ss.str();
    size_t pos = json.find("\"keys\"");
    if (pos == std::string::npos) return false;
    pos = json.find('{', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size()) {
        pos = skipWs(json, pos);
        if (json[pos] == '}') break;
        if (json[pos] != '"') return false;
        std::string key = readString(json, pos);
        pos = skipWs(json, pos);
        if (json[pos] != ':') return false;
        ++pos;
        Value v;
        if (!parseValue(json, pos, v)) return false;
        data[key] = std::move(v);
    }
    return true;
}

}  // namespace redis

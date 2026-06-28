#pragma once

#include "store/value.hpp"

#include <string>
#include <unordered_map>

namespace redis {

bool saveSnapshot(const std::string& path, const std::unordered_map<std::string, Value>& data);
bool loadSnapshot(const std::string& path, std::unordered_map<std::string, Value>& data, bool& found);

}  // namespace redis

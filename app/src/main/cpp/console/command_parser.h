#pragma once

#include <string>
#include <vector>

namespace dg::console {

std::vector<std::string> splitCommand(const std::string& line);
std::vector<std::string> commandSuggestions(const std::string& text);
bool parseCommandFloat(const std::string& token,float reference,float& value);

} // namespace dg::console

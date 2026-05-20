// SPDX-License-Identifier: Apache-2.0

#include "utils.hpp"

#include <print>

namespace datasynctool::utils
{

template <typename T>
void printParam(std::string key, const T& value)
{
    key.push_back(':');
    std::println("{:25}{}", key, value);
}

// Explicit template instantiations for common types
template void printParam<bool>(std::string, const bool&);
template void printParam<int>(std::string, const int&);
template void printParam<std::string>(std::string, const std::string&);

void displayJsonAsText(const json& data)
{
    std::println();
    for (const auto& [name, value] : data.items())
    {
        if (value.is_boolean())
        {
            printParam(name, value.get<bool>());
        }
        else if (value.is_string())
        {
            printParam(name, value.get<std::string>());
        }
        else if (value.is_number_integer())
        {
            printParam(name, value.get<int>());
        }
        else
        {
            printParam(name, value.dump());
        }
    }
    std::println();
}

std::string extractEnumValue(const std::string& fullPath)
{
    auto lastDot = fullPath.find_last_of('.');
    if (lastDot != std::string::npos)
    {
        return fullPath.substr(lastDot + 1);
    }
    return fullPath;
}

} // namespace datasynctool::utils

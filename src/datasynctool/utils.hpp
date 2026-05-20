// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace datasynctool::utils
{

using json = nlohmann::ordered_json;

/**
 * @brief Print a parameter in text format based on the given key and value
 *
 * @param[in] key - Parameter name
 * @param[in] value - Parameter value
 */
template <typename T>
void printParam(std::string key, const T& value);

/**
 * @brief Display JSON data in human-readable text format
 *
 * @param[in] data - JSON object to display
 */
void displayJsonAsText(const json& data);

/**
 * @brief Extract the enum value from the full D-Bus string
 *
 * @param[in] fullPath - Full D-Bus string value
 *
 * @return std::string - The last part of the D-Bus string
 *
 * Example:
 * "xyz.openbmc_project.Control.SyncBMCData.FullSyncStatus.FullSyncCompleted"
 *          returns "FullSyncCompleted"
 */
std::string extractEnumValue(const std::string& fullPath);

} // namespace datasynctool::utils

// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "data_sync_config.hpp"

#include <filesystem>

namespace data_sync::notify
{
namespace fs = std::filesystem;

namespace file_operations
{

/**
 * @brief API to create a temporary file and write the given data to the file
 */
fs::path writeToFile(const auto& data);

} // namespace file_operations

/**
 * @class NotifySibling
 *
 * @brief To handle the creation of sibling notification requests
 */
class NotifySibling
{
  public:
    /**
     * @brief The constructor
     *
     * @param[in] dataSyncConfig - Reference to the DataSyncConfig object
     * @param[in] modifiedDataPath - The absolute path of the data which is
     *                               modified inside the configured path
     */
    NotifySibling(const config::DataSyncConfig& dataSyncConfig,
                  const fs::path& modifiedDataPath);

    /**
     * @brief API which returns the notify file path
     */
    fs::path getNotifyFilePath() const;

  private:
    /**
     * @brief API to frame the sibling notification request in JSON form.
     *
     * @param[in] dataSyncConfig - Reference to the DataSyncConfig object
     * @param[in] modifiedDataPath - The absolute path of the data which is
     *                               modified inside the configured path
     */
    static nlohmann::json
        frameNotifyReq(const config::DataSyncConfig& dataSyncConfig,
                       const fs::path& modifiedDataPath);

    /**
     * @brief The path of the json file which contains the framed notify
     * request.
     */
    fs::path _notifyInfoFile;
};

} // namespace data_sync::notify

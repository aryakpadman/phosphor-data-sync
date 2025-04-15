// SPDX-License-Identifier: Apache-2.0

// TODO : check header files
#pragma once

#include "data_sync_config.hpp"

#include <filesystem>

namespace data_sync::notify
{
namespace fs = std::filesystem;

namespace file_operations
{
/**
 * @brief Macro for the directory path which contains the files for sibling
 * notification
 */ 
//#define NOTIFY_SIBLING_DIR "/var/lib/phosphor-data-sync/notify-sibling/"


/**
 * @brief API to create a temporary file and write the given data to the file
 */
fs::path writeToFile(const auto& data);

} // namespace file_operations

/**
 * @brief The class for sibling notification
 */
class NotifySibling
{
  public:
    /**
     * @brief The constructor
     */
    NotifySibling(const config::DataSyncConfig& dataSyncConfig, const fs::path
                modifiedDataPath);

    /**
     * @brief API which returns the notify file path
     */
    fs::path getNotifyFilePath();

  private:
    /**
     * @brief API to form the notify the data in JSON form in order to send to the
     * sibling BMC for triggering notification
     */
    nlohmann::json frameNotifyInfo(const config::DataSyncConfig& dataSyncConfig,
                const fs::path modifiedDataPath);

    /**
     * @brief The file path which has the notify information and need to send to
     * sibling BMC
     */
     fs::path _notifyInfoFile;
};


} // namespace data_sync::notify

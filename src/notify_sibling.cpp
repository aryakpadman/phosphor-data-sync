// SPDX-License-Identifier: Apache-2.0

#include "notify_sibling.hpp"
#include "config.h"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <iostream>
#include <fstream>
#include <map>

namespace data_sync::notify
{
namespace file_operations
{
fs::path writeToFile(const auto& jsonData)
{
    // TODO : generate unique across machine
    fs::path notifyFilePath = NOTIFY_SIBLING_DIR / fs::path("notify_" +
                                std::to_string(std::time(nullptr)) + ".json");
    lg2::info("Notify file path : {PATH}", "PATH", notifyFilePath);

    std::ofstream notifyFile(notifyFilePath);
    if (!notifyFile)
    {
        // TODO : add dataSyncCfg._path in the trace
        throw std::runtime_error("Failed to create the notify file");
    }

    notifyFile << jsonData.dump(4);
    notifyFile.close();

    return notifyFilePath; 
}
} // namespace file_operations

NotifySibling::NotifySibling(const config::DataSyncConfig& dataSyncConfig, const
            fs::path modifiedDataPath)
{
    try
    {
        nlohmann::json notifyInfoJson = frameNotifyInfo(dataSyncConfig,
                modifiedDataPath);
        _notifyInfoFile = file_operations::writeToFile(notifyInfoJson);
    }
    catch (std::exception& e)
    {
        lg2::error("Failed to create sibling notification for {DATA}", "DATA",
        dataSyncConfig._path);
    }
}

fs::path NotifySibling::getNotifyFilePath()
{
    return _notifyInfoFile;
}

nlohmann::json NotifySibling::frameNotifyInfo(const config::DataSyncConfig&
            dataSyncConfig, const fs::path modifiedDataPath)
{
    nlohmann::json notifyInfoJson = nlohmann::json::object({
        //{"configuredDataPath", dataSyncConfig._path},
        {"ModifiedDataPath", modifiedDataPath},
        {"NotifyInfo", dataSyncConfig._notifySibling.value()}
    });
    
    return notifyInfoJson;
}

} // namespace data_sync::notify

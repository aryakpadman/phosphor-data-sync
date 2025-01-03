// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "data_sync_config.hpp"

#include <filesystem>
#include <vector>

namespace data_sync
{

namespace fs = std::filesystem;

/**
 * @class Manager
 *
 * @brief This class manages all configured data for synchronization
 *        between BMCs.
 */
class Manager
{
  public:
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;
    ~Manager() = default;

    /**
     * @brief The constructor parses the configuration, monitors the data, and
     *        synchronizes it.
     *
     * @param[in] dataSyncCfgDir - The data sync configuration directory
     */
    Manager(const fs::path& dataSyncCfgDir);

  private:
    /**
     * @brief A helper API to parse the data sync configuration
     *
     * @param[in] dataSyncCfgDir - The data sync configuration directory
     *
     * @return NULL
     *
     * @note It will continue parsing all files even if one file fails to parse.
     */
    void parseConfiguration(const fs::path& dataSyncCfgDir);

    /**
     * @brief The list of data to synchronize.
     */
    std::vector<config::DataSyncConfig> _dataSyncConfiguration;
};

} // namespace data_sync

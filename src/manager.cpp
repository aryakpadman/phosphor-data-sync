// SPDX-License-Identifier: Apache-2.0

#include "manager.hpp"

#include "data_watcher.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace data_sync
{

using namespace watch::inotify;
Manager::Manager(sdbusplus::async::context& ctx,
                 std::unique_ptr<ext_data::ExternalDataIFaces>&& extDataIfaces,
                 const fs::path& dataSyncCfgDir) :
    _ctx(ctx), _extDataIfaces(std::move(extDataIfaces)),
    _dataSyncCfgDir(dataSyncCfgDir)
{
    _ctx.spawn(init());
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::init()
{
    co_await sdbusplus::async::execution::when_all(
        parseConfiguration(), _extDataIfaces->startExtDataFetches());

    co_return co_await startSyncEvents();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::parseConfiguration()
{
    auto parse = [this](const auto& configFile) {
        try
        {
            std::ifstream file;
            file.open(configFile.path());

            nlohmann::json configJSON(nlohmann::json::parse(file));

            if (configJSON.contains("Files"))
            {
                std::ranges::transform(
                    configJSON["Files"],
                    std::back_inserter(this->_dataSyncConfiguration),
                    [](const auto& element) {
                    return config::DataSyncConfig(element, false);
                });
            }
            if (configJSON.contains("Directories"))
            {
                std::ranges::transform(
                    configJSON["Directories"],
                    std::back_inserter(this->_dataSyncConfiguration),
                    [](const auto& element) {
                    return config::DataSyncConfig(element, true);
                });
            }
        }
        catch (const std::exception& e)
        {
            // TODO Create error log
            lg2::error("Failed to parse the configuration file : {CONFIG_FILE},"
                       " exception : {EXCEPTION}",
                       "CONFIG_FILE", configFile.path(), "EXCEPTION", e);
        }
    };

    if (fs::exists(_dataSyncCfgDir) && fs::is_directory(_dataSyncCfgDir))
    {
        std::ranges::for_each(fs::directory_iterator(_dataSyncCfgDir), parse);
    }

    co_return;
}

bool Manager::isSyncEligible(const config::DataSyncConfig& dataSyncCfg)
{
    using enum config::SyncDirection;
    using enum ext_data::BMCRole;

    if ((dataSyncCfg._syncDirection == Bidirectional) ||
        ((dataSyncCfg._syncDirection == Active2Passive) &&
         this->_extDataIfaces->bmcRole() == Active) ||
        ((dataSyncCfg._syncDirection == Passive2Active) &&
         this->_extDataIfaces->bmcRole() == Passive))
    {
        return true;
    }
    else
    {
        // TODO Trace is required, will overflow?
        lg2::debug("Sync is not required for [{PATH}] due to "
                   "SyncDirection: {SYNC_DIRECTION} BMCRole: {BMC_ROLE}",
                   "PATH", dataSyncCfg._path, "SYNC_DIRECTION",
                   dataSyncCfg.getSyncDirectionInStr(), "BMC_ROLE",
                   _extDataIfaces->bmcRole());
    }
    return false;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::startSyncEvents()
{
    std::ranges::for_each(
        _dataSyncConfiguration |
            std::views::filter([this](const auto& dataSyncCfg) {
        return this->isSyncEligible(dataSyncCfg);
    }),
        [this](const auto& dataSyncCfg) {
        using enum config::SyncType;
        if (dataSyncCfg._syncType == Immediate)
        {
            this->_ctx.spawn(this->monitorDataToSync(dataSyncCfg));
        }
        else if (dataSyncCfg._syncType == Periodic)
        {
            this->_ctx.spawn(this->monitorTimerToSync(dataSyncCfg));
        }
    });
    co_return;
}

void Manager::syncData(const config::DataSyncConfig& dataSyncCfg, bool isDelete)
{
    using namespace std::string_literals;

    std::filesystem::path srcPath{dataSyncCfg._path};
    std::filesystem::path destPath{
        dataSyncCfg._destPath.value_or(dataSyncCfg._path)};
    std::string syncCmd{"rsync --archive --compress"};

    if (isDelete)
    {
        std::string syncCmd{"rsync --delete --archive --compress"};
        if (!std::filesystem::exists(srcPath))
        {
            srcPath = srcPath.parent_path();
            destPath = destPath.parent_path();
        }
    }

    // Add source data path
    syncCmd.append(" "s + srcPath.string());

#ifdef UNIT_TEST
    syncCmd.append(" "s);
#else
    // TODO Support for remote (i,e sibling BMC) copying needs to be added.
#endif

    // Add destination data path
    syncCmd.append(destPath.string());

    int result = std::system(syncCmd.c_str()); // NOLINT
    if (result != 0)
    {
        // TODO:
        // Retry and create error log and disable redundancy if retry is failed.
        lg2::error("Error syncing: {PATH}", "PATH", dataSyncCfg._path);
    }
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    Manager::monitorDataToSync(const config::DataSyncConfig& dataSyncCfg)
{
    try
    {
        uint32_t eventMasksToWatch = IN_CLOSE_WRITE | IN_DELETE |
                                     IN_DELETE_SELF;
        if (dataSyncCfg._isPathDir)
        {
            eventMasksToWatch |= IN_CREATE;
        }

        // Create watcher for the dataSyncCfg._path
        watch::inotify::DataWatcher dataWatcher(
            _ctx, IN_NONBLOCK, eventMasksToWatch, dataSyncCfg._path);

        while (!_ctx.stop_requested())
        {
            switch (co_await dataWatcher.onDataChange())
            {
                case RequiredAction::SYNC:
                {
                    syncData(dataSyncCfg);
                    break;
                }
                case RequiredAction::DELETE:
                {
                    syncData(dataSyncCfg, true);
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
    }
    catch (std::exception& e)
    {
        lg2::error("Failed to create watcher object for {PATH} :  exception "
                   "{ERROR}",
                   "PATH", dataSyncCfg._path, "ERROR", e.what());
    }
    co_return;
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    Manager::monitorTimerToSync(const config::DataSyncConfig& dataSyncCfg)
{
    while (!_ctx.stop_requested())
    {
        co_await sdbusplus::async::sleep_for(
            _ctx, dataSyncCfg._periodicityInSec.value());
        syncData(dataSyncCfg);
    }
    co_return;
}

} // namespace data_sync

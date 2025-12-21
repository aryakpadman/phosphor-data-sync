// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "notify_service.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <experimental/scope>
#include <fstream>
#include <iostream>
#include <map>

namespace data_sync::notify
{

Retry::Retry(uint8_t maxRetryAttempts,
             const std::chrono::seconds& retryIntervalInSec) :
    _maxRetryAttempts(maxRetryAttempts), _retryIntervalInSec(retryIntervalInSec)
{}

namespace file_operations
{
nlohmann::json readFromFile(const fs::path& notifyFilePath)
{
    std::ifstream notifyFile;
    notifyFile.open(fs::path(NOTIFY_SERVICES_DIR) / notifyFilePath);

    return nlohmann::json::parse(notifyFile);
}
} // namespace file_operations

NotifyService::NotifyService(
    sdbusplus::async::context& ctx,
    data_sync::ext_data::ExternalDataIFaces& extDataIfaces,
    const fs::path& notifyFilePath, CleanupCallback cleanup,
    uint8_t retryAttempts, std::chrono::seconds retryIntervalInSec) :
    _ctx(ctx), _extDataIfaces(extDataIfaces),
    _retryInfo(retryAttempts, retryIntervalInSec), _cleanup(std::move(cleanup))
{
    _ctx.spawn(init(notifyFilePath));
}

sdbusplus::async::task<>
    NotifyService::sendSystemDNotification(const std::string& service,
                                           const std::string& systemdMethod)
{
    // retryAttempt = 0 indicates initial attempt, rest implies retries
    uint8_t retryAttempt = 0;
    while (retryAttempt++ <= _retryInfo._maxRetryAttempts)
    {
        bool success = co_await _extDataIfaces.systemDServiceAction(
            service, systemdMethod);

        if (success)
        {
            // No retries required
            co_return;
        }

        if (retryAttempt <= _retryInfo._maxRetryAttempts)
        {
            lg2::debug(
                "Scheduling retry[{ATTEMPT}/{MAX}] for {SERVICE} after {SEC}s",
                "ATTEMPT", retryAttempt, "MAX", _retryInfo._maxRetryAttempts,
                "SERVICE", service, "SEC",
                _retryInfo._retryIntervalInSec.count());

            co_await sleep_for(_ctx, _retryInfo._retryIntervalInSec);
        }
    }

    // TODO : Create info PEL here
    lg2::error(
        "Failed to notify {SERVICE} via {METHOD} ; All retries[{ATTEMPT}/{MAX}] "
        "exhausted",
        "SERVICE", service, "METHOD", systemdMethod, "ATTEMPT",
        _retryInfo._maxRetryAttempts, "MAX", _retryInfo._maxRetryAttempts);
    co_return;
}

sdbusplus::async::task<>
    NotifyService::systemDNotify(const nlohmann::json& notifyRqstJson)
{
    const auto services = notifyRqstJson["NotifyInfo"]["NotifyServices"]
                              .get<std::vector<std::string>>();
    const std::string& modifiedPath =
        notifyRqstJson["ModifiedDataPath"].get<std::string>();
    const std::string& systemdMethod =
        ((notifyRqstJson["NotifyInfo"]["Method"].get<std::string>()) == "Reload"
             ? "ReloadUnit"
             : "RestartUnit");

    for (const auto& service : services)
    {
        // Will notify each service sequentially assuming they are dependent
        co_await sendSystemDNotification(service, systemdMethod);
    }
    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::init(fs::path notifyFilePath)
{
    // Ensure cleanup is called when coroutine completes
    using std::experimental::scope_exit;
    auto cleanupGuard = scope_exit([this] {
        if (_cleanup)
        {
            _cleanup(this);
        }
    });

    nlohmann::json notifyRqstJson{};
    try
    {
        notifyRqstJson = file_operations::readFromFile(notifyFilePath);
    }
    catch (const std::exception& exc)
    {
        lg2::error(
            "Failed to read the notify request file[{FILEPATH}], Error : {ERR}",
            "FILEPATH", notifyFilePath, "ERR", exc);
        throw std::runtime_error("Failed to read the notify request file");
    }
    if (notifyRqstJson["NotifyInfo"]["Mode"] == "DBus")
    {
        // TODO : Implement DBus notification method
        lg2::warning(
            "Unable to process the notify request[{PATH}], as DBus mode is"
            " not available!!!. Received rqst : {RQSTJSON}",
            "PATH", notifyFilePath, "RQSTJSON",
            nlohmann::to_string(notifyRqstJson));
    }
    else if ((notifyRqstJson["NotifyInfo"]["Mode"] == "Systemd"))
    {
        co_await systemDNotify(notifyRqstJson);
    }
    else
    {
        lg2::error(
            "Notify failed due to unknown Mode in notify request[{PATH}], "
            "Request : {RQSTJSON}",
            "PATH", notifyFilePath, "RQSTJSON",
            nlohmann::to_string(notifyRqstJson));
    }

    try
    {
        fs::remove(notifyFilePath);
    }
    catch (const std::exception& exc)
    {
        lg2::error("Failed to remove notify file[{PATH}], Error: {ERR}", "PATH",
                   notifyFilePath, "ERR", exc);
    }

    co_return;
}

} // namespace data_sync::notify

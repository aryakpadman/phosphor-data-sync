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

Retry::Retry(uint8_t retryAttempts,
             const std::chrono::seconds& retryIntervalInSec) :
    _retryAttempts(retryAttempts), _retryIntervalInSec(retryIntervalInSec)
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
    const fs::path& notifyFilePath, uint8_t retryAttempts,
    std::chrono::seconds retryIntervalInSec, CleanupCallback cleanup) :
    _ctx(ctx), _extDataIfaces(extDataIfaces),
    _retryInfo(retryAttempts, retryIntervalInSec),
    _cleanup(std::move(cleanup))
{
    _ctx.spawn(init(notifyFilePath));
}

sdbusplus::async::task<> NotifyService::sendSystemDNotification(
    const std::string& service, const std::string& systemdMethod)
{
    // retryIndex = 0 indicates initial attempt, rest implies retries
    uint8_t retryIndex = 0;
    while (retryIndex <= _retryInfo._retryAttempts)
    {
        bool result = co_await _extDataIfaces.systemDServiceAction(service, systemdMethod);

        if (result)
        {
            // No retries required
            co_return;
        }

        lg2::error("DBus call to [{METHOD}:{SERVICE}] failed...",
            "METHOD", systemdMethod, "SERVICE", service);

        retryIndex++;
        if (retryIndex <= _retryInfo._retryAttempts)
        {
            lg2::debug("Scheduling retry[{ATTEMPT}/{MAX}] for {SERVICE} after {SEC}s",
                       "ATTEMPT", retryIndex, "MAX", _retryInfo._retryAttempts,
                       "SERVICE", service, "SEC", _retryInfo._retryIntervalInSec.count());

            co_await sleep_for(_ctx, _retryInfo._retryIntervalInSec);
        }
    }

    //TODO : Create info PEL here
    lg2::error("Failed to notify {SERVICE} by {METHOD} ; All retries[{ATTEMPT}/{MAX}] "
        "exhausted","SERVICE", service, "METHOD", systemdMethod,
        "ATTEMPT", _retryInfo._retryAttempts,
        "MAX", _retryInfo._retryAttempts);
    co_return;
}


// NOLINTNEXTLINE
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
            "Failed to process the notify request[{PATH}], Request : {RQSTJSON}",
            "PATH", notifyFilePath, "RQSTJSON",
            nlohmann::to_string(notifyRqstJson));
        co_return;
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

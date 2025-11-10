// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "notify_service.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <iostream>
#include <map>

namespace data_sync::notify
{
namespace file_operations
{
nlohmann::json readFromFile(const fs::path& notifyFilePath)
{
    std::ifstream notifyFile;
    notifyFile.open(fs::path(NOTIFY_SERVICES_DIR) / notifyFilePath);
    nlohmann::json notifyInfoJSON(nlohmann::json::parse(notifyFile));

    return notifyInfoJSON;
}
} // namespace file_operations

NotifyService::NotifyService(sdbusplus::async::context& ctx,
                             const fs::path& notifyFilePath) : _ctx(ctx)
{
    _ctx.spawn(init(notifyFilePath));
}

sdbusplus::async::task<std::string>
    // NOLINTNEXTLINE
    NotifyService::getDbusObjectPath(const std::string& service,
                                     const std::string& interface)
{
    std::string objectPath{};

    constexpr auto objMapper =
        sdbusplus::async::proxy()
            .service("xyz.openbmc_project.ObjectMapper")
            .path("/xyz/openbmc_project/object_mapper")
            .interface("xyz.openbmc_project.ObjectMapper");

    /**
     * Output of the DBus call will be a nested map, where
     * outer map : Map of DBus Object Path and the inner map
     * inner map : Map of the service name and list of interfaces it has
     * implemented.
     */
    using Services = std::map<std::string, std::vector<std::string>>;
    using Paths = std::map<std::string, Services>;
    Paths subTreesInfo = co_await objMapper.call<Paths>(
        _ctx, "GetSubTree", "/", 0, std::vector<std::string>{interface});

    auto outerMapItr = std::ranges::find_if(subTreesInfo,
                                            [&service](const auto& outerMap) {
        return std::ranges::any_of(outerMap.second,
                                   [&service](const auto& serviceIfacesList) {
            return serviceIfacesList.first == service;
        });
    });
    if (outerMapItr != subTreesInfo.end())
    {
        objectPath = outerMapItr->first;
    }
    else
    {
        lg2::error(
            "Unable to find the object path which hosts the interface[{INTERFACE}],"
            " of the service {SERVICE}",
            "INTERFACE", interface, "SERVICE", service);
        throw std::runtime_error("Unable to find the DBus object path");
    }
    co_return objectPath;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::invokeNotifyDBusMethod(
    [[maybe_unused]] const std::string& service,
    [[maybe_unused]] const fs::path& modifiedDataPath)
{
    // TODO : Complete DBus notification method once INTERFACE and method name
    // finalized
    // Step 1 : Invoke getDbusObjectPath()
    // Step 2 : Using the object path,interface and service, frame the DBus
    // :method call
    lg2::warning("DBus notification support is not available currently");

    co_return;
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    NotifyService::sendDBusNotification(std::vector<std::string> services,
                                        fs::path modifiedDataPath)
{
    for (const auto& service : services)
    {
        // NOLINTNEXTLINE
        co_await invokeNotifyDBusMethod(service, modifiedDataPath);
    }

    co_return;
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    NotifyService::sendSystemDNotification(
        const std::vector<std::string>& services, const std::string& method)
{
    for (const auto& service : services)
    {
        auto systemdReload = sdbusplus::async::proxy()
                                 .service("org.freedesktop.systemd1")
                                 .path("/org/freedesktop/systemd1")
                                 .interface("org.freedesktop.systemd1.Manager");
        try
        {
            using objectPath = sdbusplus::message::object_path;
            if (method == "Restart")
            {
                lg2::debug("Restart request send for {SERVICE}", "SERVICE",
                           service);
                co_await systemdReload.call<objectPath>(_ctx, "RestartUnit",
                                                        service, "replace");
            }
            else if (method == "Reload")
            {
                lg2::debug("Reload request send for {SERVICE}", "SERVICE",
                           service);
                co_await systemdReload.call<objectPath>(_ctx, "ReloadUnit",
                                                        service, "replace");
            }
        }
        catch (const sdbusplus::exception::SdBusError& e)
        {
            lg2::error(
                "Failed to send systemd notification request to {SERVICE}: {ERROR}",
                "SERVICE", service, "ERROR", e.what());
        }
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::init(fs::path notifyFilePath)
{
    nlohmann::json notifyfileData{};
    try
    {
        notifyfileData = file_operations::readFromFile(notifyFilePath);
    }
    catch (const std::exception& exc)
    {
        lg2::error(
            "Failed to read the notify request file[{FILEPATH}], Error : {ERR}",
            "FILEPATH", notifyFilePath, "ERR", exc);
        throw std::runtime_error("Failed to read the notify request file");
    }
    if (notifyfileData["NotifyInfo"]["Mode"] == "DBus")
    {
        // Send DBUS notification
        // NOLINTNEXTLINE
        co_await sendDBusNotification(
            notifyfileData["NotifyInfo"]["NotifyServices"]
                .get<std::vector<std::string>>(),
            notifyfileData["ModifiedDataPath"]);
    }
    else if ((notifyfileData["NotifyInfo"]["Mode"] == "Systemd"))
    {
        // Do systemd reload/restart
        // NOLINTNEXTLINE
        co_await sendSystemDNotification(
            notifyfileData["NotifyInfo"]["NotifyServices"]
                .get<std::vector<std::string>>(),
            notifyfileData["NotifyInfo"]["Method"].get<std::string>());
    }
    else
    {
        lg2::error("Failed to process the notify request[{PATH}], Error : "
                   "Unknown Mode/Method in the request",
                   "PATH", notifyFilePath);
        co_return;
    }
    fs::remove(notifyFilePath);

    co_return;
}

} // namespace data_sync::notify

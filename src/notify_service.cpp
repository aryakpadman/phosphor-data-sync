// SPDX-License-Identifier: Apache-2.0

#include "notify_service.hpp"
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

nlohmann::json readfromFile(const fs::path& notifyFilePath)
{
    std::ifstream notifyFile;
    notifyFile.open(fs::path(NOTIFY_SERVICE_DIR)/notifyFilePath);
    nlohmann::json notifyInfoJSON(nlohmann::json::parse(notifyFile));

    return notifyInfoJSON;
}
} // namespace file_operations

NotifyService::NotifyService(sdbusplus::async::context& ctx,
                             const fs::path& notifyFilePath)
{
    // Initiate notification based on the received request
    ctx.spawn(init(ctx, notifyFilePath));
}

// NOLINTNEXTLINE
sdbusplus::async::task<std::string> NotifyService::getDbusObjectPath(
                [[maybe_unused]] sdbusplus::async::context& ctx, 
                [[maybe_unused]] const std::string& service,
                const std::string& interface)
{
    std::string objectPath{};
    
    constexpr auto objMapper = sdbusplus::async::proxy()
                                    .service("xyz.openbmc_project.ObjectMapper")
                                    .path("/xyz/openbmc_project/object_mapper")
                                    .interface("xyz.openbmc_project.ObjectMapper");

    using services = std::map<std::string, std::vector<std::string>>;
    using paths = std::map<std::string, services>;
    paths subTreesInfo = co_await objMapper.call<paths>(ctx,
        "GetSubTree", "/", 0, std::vector<std::string>{interface});

    lg2::info("Size of subTree : {SIZE}", "SIZE",subTreesInfo.size());
    for (const auto& [objPath, serviceInterfaceMap] : subTreesInfo)
    {
        bool objPathFound{false};
        for (const auto& [serviceName, interfaces] : serviceInterfaceMap)
        {
            lg2::warning("{OBJ} -> {SERVICE}", "OBJ", objPath, "SERVICE",
                    serviceName);
            if (serviceName == service)
            {
                objectPath = objPath;
                objPathFound = true;
                break;
            }
        }
        if (objPathFound)
        {
            break;
        }
    }

    lg2::info("Object path found : {PATH}", "PATH", objectPath);
    co_return objectPath;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::invokeNotifyDBusMethod(
                sdbusplus::async::context& ctx, const std::string& service)
{
    auto objPath = co_await getDbusObjectPath(ctx, service,
                                                DBUS_NOTIFY_INTERFACE);

    auto dbusNotify = sdbusplus::async::proxy()
                                    .service(service)
                                    .path(objPath)
                                    .interface(DBUS_NOTIFY_INTERFACE);
    co_await dbusNotify.call<>(ctx, DBUS_NOTIFY_METHOD);

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::sendDBusNotification(
                sdbusplus::async::context& ctx, std::vector<std::string>
                services, fs::path modifiedDataPath)
{
    lg2::debug("Sending DBus notification");

    for (const auto& service : services)
    {
        lg2::info("Inform {SERVICE} as {DATA} changed", "SERVICE", service,
                            "DATA", modifiedDataPath);
        co_await invokeNotifyDBusMethod(ctx, service);
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::sendSystemDNotification(
                sdbusplus::async::context& ctx, std::vector<std::string>
                services, std::string method)
{
    for (const auto& service : services)
    {
        lg2::debug("Reload request for {SERVICE}", "SERVICE", service);
        //auto result{"100"};
        auto systemdReload = sdbusplus::async::proxy()
                                        .service("org.freedesktop.systemd1")
                                        .path("/org/freedesktop/systemd1")
                                        .interface("org.freedesktop.systemd1.Manager");
        try
        {
            using objectPath = sdbusplus::message::object_path;
            if (method == "Restart")
            {
                auto result = co_await systemdReload.call<objectPath>(ctx, "RestartUnit", service, "replace");
            }
            else if (method == "Reload")
            {
                auto result = co_await systemdReload.call<objectPath>(ctx, "ReloadUnit", service, "replace");
            }
            
            //lg2::debug("Return value of dbus call : {RESULT}", "RESULT", result);
        }
        catch (const sdbusplus::exception::SdBusError& e)
        {
            lg2::error("Failed to restart unit {SERVICE}: {ERROR}", "SERVICE", service, "ERROR", e.what());
        }
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> NotifyService::init(sdbusplus::async::context& ctx,
                                            fs::path notifyFilePath)
{
    nlohmann::json notifyfileData = file_operations::readfromFile(notifyFilePath);
    if (notifyfileData["NotifyInfo"]["Mode"] == "DBus")
    {
        //Send DBUS notification
        co_await sendDBusNotification(ctx,
                notifyfileData["NotifyInfo"]["NotifyServices"].
                                    get<std::vector<std::string>>(),
                notifyfileData["ModifiedDataPath"]);

    }
    else if ((notifyfileData["NotifyInfo"]["Mode"] == "Systemd"))
    {
        //Do systemd reload
        co_await sendSystemDNotification(ctx,
                notifyfileData["NotifyInfo"]["NotifyServices"].
                                    get<std::vector<std::string>>(),
                notifyfileData["NotifyInfo"]["Method"].get<std::string>());
    }
    else
    {
        lg2::error("Failed to process the notify request[{PATH}], Error : "
            "Unknown Mode/Method in the request", "PATH", notifyFilePath);
        co_return;
    }
    fs::remove(notifyFilePath);

    co_return;
}

} // namespace data_sync::notify

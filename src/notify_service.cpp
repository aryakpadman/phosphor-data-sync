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

nlohmann::json readfromFile(fs::path notifyFilePath)
{
    std::ifstream notifyFile;
    notifyFile.open(fs::path(NOTIFY_SERVICE_DIR)/notifyFilePath);
    nlohmann::json notifyInfoJSON(nlohmann::json::parse(notifyFile));

    return notifyInfoJSON;
}
} // namespace file_operations

ServicesToNotify::ServicesToNotify(const std::vector<std::string>& services) :
                _services(services)
{}

NotifyService::NotifyService(sdbusplus::async::context& ctx, const fs::path
                                notifyFilePath)
            /*_servicesToNotify(ServicesToNotify(
                        notifyInfoJson["NotifyInfo"]["NotifyServices"]
                        .get<std::vector<std::string>>())),
            _modifiedDataPath(notifyInfoJson["ModifiedDataPath"])*/
{
    /*if (notifyInfoJson["NotifyInfo"]["Mode"] == "DBUS")
    {
        _notifyMode = NotifyMode::DBUS;
    }
    else if (notifyInfoJson["NotifyInfo"]["Mode"] == "Systemd")
    {
        _notifyMode = NotifyMode::Systemd;
        //TODO : Handle else condition??
    }*/

    // Initiate notification based on the received request
    ctx.spawn(init(ctx, notifyFilePath));
}

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
            //lg2::info("{OBJ} -> {SERVICE}", "OBJ", objPath, "SERVICE",
              //      serviceName);
            if (serviceName.compare(service) == 0)
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

sdbusplus::async::task<> NotifyService::invokeNotifyMethod(
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

sdbusplus::async::task<> NotifyService::sendDBusNotification(
                sdbusplus::async::context& ctx, std::vector<std::string>
                services, fs::path modifiedDataPath)
{
    lg2::debug("Sending DBus notification");

    for (const auto& service : services)
    {
        lg2::info("Inform {SERVICE} as {DATA} changed", "SERVICE", service,
                            "DATA", modifiedDataPath);
        co_await invokeNotifyMethod(ctx, service);
    }

    co_return;
}

sdbusplus::async::task<> NotifyService::systemdReload(
                sdbusplus::async::context& ctx, std::vector<std::string>
                services)
{
    for (const auto& service : services)
    {
        lg2::debug("Reload {SERVICE}", "SERVICE", service);
        //auto result{"100"};
        auto systemdReload = sdbusplus::async::proxy()
                                        .service("org.freedesktop.systemd1")
                                        .path("/org/freedesktop/systemd1")
                                        .interface("org.freedesktop.systemd1.Manager");
        try
        {
            using objectPath = sdbusplus::message::object_path;
            auto result = co_await systemdReload.call<objectPath>(ctx, "RestartUnit", service, "replace");
            lg2::debug("Return value of dbus call : {RESULT}", "RESULT", result);
        }
        catch (const sdbusplus::exception::SdBusError& e)
        {
            lg2::error("Failed to restart unit {SERVICE}: {ERROR}", "SERVICE", service, "ERROR", e.what());
        }
    }

    co_return;
}

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
    else if (notifyfileData["NotifyInfo"]["Mode"] == "Systemd")
    {
        //Do systemd reload
        co_await systemdReload(ctx,
                notifyfileData["NotifyInfo"]["NotifyServices"].
                                    get<std::vector<std::string>>());
    }
    else
    {
        lg2::error("Unknown Notify mode");
    }
    fs::remove(notifyFilePath);

    co_return;
}

} // namespace data_sync::notify

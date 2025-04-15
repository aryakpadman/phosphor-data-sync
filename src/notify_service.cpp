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
        // TODO : Send DBUS notification
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

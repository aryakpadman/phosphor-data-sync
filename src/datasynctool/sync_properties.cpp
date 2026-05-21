// SPDX-License-Identifier: Apache-2.0

#include "sync_properties.hpp"

#include "utils.hpp"

#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/common.hpp>

#include <iostream>
#include <print>
#include <variant>

namespace datasynctool::sync_properties
{

using SyncBMCData =
    sdbusplus::common::xyz::openbmc_project::control::SyncBMCData;

PropertyMap getAllProperties(sdbusplus::bus_t& bus, const std::string& service,
                              const std::string& path,
                              const std::string& interface)
{
    auto method = bus.new_method_call(service.c_str(), path.c_str(),
                                      "org.freedesktop.DBus.Properties",
                                      "GetAll");
    method.append(interface);

    auto reply = bus.call(method);
    PropertyMap properties;
    reply.read(properties);

    return properties;
}

json buildStatusJson(const PropertyMap& properties)
{
    json output;

    if (auto it = properties.find("DisableSync"); it != properties.end())
    {
        bool disableSync = std::get<bool>(it->second);
        output["Sync Enabled"] = !disableSync;
    }

    if (auto it = properties.find("FullSyncStatus"); it != properties.end())
    {
        std::string fullSyncStatus = std::get<std::string>(it->second);
        output["Full Sync Status"] = utils::extractEnumValue(fullSyncStatus);
    }

    if (auto it = properties.find("SyncEventsHealth"); it != properties.end())
    {
        std::string syncEventsHealth = std::get<std::string>(it->second);
        output["Background Sync Status"] =
            utils::extractEnumValue(syncEventsHealth);
    }

    return output;
}

int displayStatus(bool jsonOutput)
{
    try
    {
        auto bus = sdbusplus::bus::new_default();
        const std::string service = SyncBMCData::interface;
        const std::string path = SyncBMCData::instance_path;
        const std::string interface = SyncBMCData::interface;

        auto properties = getAllProperties(bus, service, path, interface);
        json statusData = buildStatusJson(properties);

        if (jsonOutput)
        {
            std::println("{}", statusData.dump(4));
        }
        else
        {
            utils::displayJsonAsText(statusData);
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading D-Bus properties: " << e.what() << "\n";
        return -1;
    }
}

int setSyncEnabled(bool enable)
{
    try
    {
        auto bus = sdbusplus::bus::new_default();
        const std::string service = SyncBMCData::interface;
        const std::string path = SyncBMCData::instance_path;
        const std::string interface = SyncBMCData::interface;

        auto method = bus.new_method_call(service.c_str(), path.c_str(),
                                          "org.freedesktop.DBus.Properties",
                                          "Set");
        method.append(interface, "DisableSync", std::variant<bool>(!enable));

        auto reply = bus.call(method);

        std::println("Sync {} successfully", enable ? "enabled" : "disabled");
        return 0;
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::cerr << "Error setting sync state: " << e.what() << "\n";
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return 1;
    }
}

} // namespace datasynctool::sync_properties

// SPDX-License-Identifier: Apache-2.0

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/common.hpp>

#include <format>
#include <iostream>
#include <map>
#include <print>
#include <string>
#include <variant>

namespace
{
using SyncBMCData =
    sdbusplus::common::xyz::openbmc_project::control::SyncBMCData;
using json = nlohmann::ordered_json;

// D-Bus property value type
using DbusVariant = std::variant<bool, std::string>;
using PropertyMap = std::map<std::string, DbusVariant>;

/**
 * @brief Get all D-Bus properties hosted by the given application
 *        using GetAll method
 *
 * @param[in] bus - D-Bus connection
 * @param[in] service -  D-Bus service name
 * @param[in] path - D-Bus object path
 * @param[in] interface -  D-Bus interface name
 *
 * @return PropertyMap - Map of property names to values
 */
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

/**
 * @brief Extract the enum value from the full D-Bus string
 *
 * @param[in] fullPath - Full D-Bus string value
 *
 * @return std::string - The last part of the D-Bus string
 *
 * Example:
 * "xyz.openbmc_project.Control.SyncBMCData.FullSyncStatus.FullSyncCompleted"
 *          returns "FullSyncCompleted"
 */
std::string extractEnumValue(const std::string& fullPath)
{
    auto lastDot = fullPath.find_last_of('.');
    if (lastDot != std::string::npos)
    {
        return fullPath.substr(lastDot + 1);
    }
    return fullPath;
}

/**
 * @brief Build and return JSON object from the D-Bus properties
 *
 * @param[in] properties - Output of the GetAll method
 *
 * @return json - JSON object containing the D-Bus properties
 */
json buildStatusJson(const PropertyMap& properties)
{
    json output;

    if (auto it = properties.find("DisableSync"); it != properties.end())
    {
        bool disableSync = std::get<bool>(it->second);
        output["SyncEnable"] = !disableSync;
    }

    if (auto it = properties.find("FullSyncStatus"); it != properties.end())
    {
        std::string fullSyncStatus = std::get<std::string>(it->second);
        output["FullSyncStatus"] = extractEnumValue(fullSyncStatus);
    }

    if (auto it = properties.find("SyncEventsHealth"); it != properties.end())
    {
        std::string syncEventsHealth = std::get<std::string>(it->second);
        output["BackgroundSyncStatus"] = extractEnumValue(syncEventsHealth);
    }

    return output;
}

/**
 * @brief Print a parameter in text format based on the given Key and value
 */
template <typename T>
void printParam(std::string key, const T& value)
{
    key.push_back(':');
    std::println("{:25}{}", key, value);
}

/**
 * @brief Display the JSON info in text format.
 *
 * @param[in] data - The JSON data to be displayed
 */
void displayJsonAsText(const json& data)
{
    std::println();
    for (const auto& [name, value] : data.items())
    {
        if (value.is_boolean())
        {
            printParam(name, value.get<bool>());
        }
        else if (value.is_string())
        {
            printParam(name, value.get<std::string>());
        }
        else if (value.is_number_integer())
        {
            printParam(name, value.get<int>());
        }
        else
        {
            printParam(name, value.dump());
        }
    }
    std::println();
}

/**
 * @brief Handler for --status/-s flag
 *        The API dumps the dbus properties hosted by the service
 *
 * @param[in] - jsonOutput - Dump the output in JSON format if true.
 *
 * @return - return code
 */
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
            displayJsonAsText(statusData);
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading D-Bus properties: " << e.what() << "\n";
        return -1;
    }
}

} // namespace

int main(int argc, char* argv[])
{
    CLI::App app{"Data Sync Tool - Command line utility for phosphor-data-sync"};

    // Add status flag
    bool showStatus{false};
    app.add_flag("-s,--status", showStatus,
                 "Display all D-Bus properties hosted by data sync");

    // Add json flag
    bool jsonOutput{false};
    app.add_flag("-j,--json", jsonOutput, "Display in JSON format");

    // Parse command line arguments
    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        return app.exit(e);
    }

    // Handle status option
    if (showStatus)
    {
        return displayStatus(jsonOutput);
    }

    // Default behavior when no options are provided
    std::cout << "Data Sync Tool initialized\n";
    std::cout << "Use --help for available options.\n";

    return 0;
}

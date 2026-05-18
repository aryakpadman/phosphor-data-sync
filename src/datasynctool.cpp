// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/common.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <print>
#include <string>
#include <variant>
#include <vector>

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

/**
 * @brief Start a full synchronization
 */
int startFullSync()
{
    try
    {
        auto bus = sdbusplus::bus::new_default();
        const std::string service = SyncBMCData::interface;
        const std::string path = SyncBMCData::instance_path;
        const std::string interface = SyncBMCData::interface;

        auto method = bus.new_method_call(service.c_str(), path.c_str(),
                                          interface.c_str(), "StartFullSync");

        auto reply = bus.call(method);

        std::println("Full sync initiated. See progress in journal logs");
        return 0;
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::cerr << "Error starting full sync: " << e.what() << "\n";

        // Provide more specific error messages for known error types
        std::string errorName = e.name();
        if (errorName.find("SyncDisabled") != std::string::npos)
        {
            std::cerr << "Sync is currently disabled\n";
        }
        else if (errorName.find("SiblingBMCNotAvailable") != std::string::npos)
        {
            std::cerr << "Sibling BMC is not available\n";
        }
        else if (errorName.find("FullSyncInProgress") != std::string::npos)
        {
            std::cerr << "Full sync is already in progress\n";
        }

        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return 1;
    }
}

/**
 * @brief Set the DisableSync property
 */
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
        method.append(interface, "DisableSync",
                      std::variant<bool>(!enable));

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

/**
 * @brief List all configured sync paths from JSON config files
 */
int listConfigPaths(bool jsonOutput)
{
    namespace fs = std::filesystem;

    try
    {
        const fs::path configDir = DATA_SYNC_CONFIG_DIR;

        if (!fs::exists(configDir) || !fs::is_directory(configDir))
        {
            std::cerr << "Config directory not found: " << configDir << "\n";
            return 1;
        }

        std::vector<std::string> files;
        std::vector<std::string> directories;

        // Read all JSON files in the config directory
        for (const auto& entry : fs::directory_iterator(configDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                std::ifstream configFile(entry.path());
                if (!configFile.is_open())
                {
                    std::cerr << "Failed to open: " << entry.path() << "\n";
                    continue;
                }

                try
                {
                    json config = json::parse(configFile);

                    // Extract Files
                    if (config.contains("Files") && config["Files"].is_array())
                    {
                        for (const auto& fileEntry : config["Files"])
                        {
                            if (fileEntry.contains("Path"))
                            {
                                files.push_back(fileEntry["Path"].get<std::string>());
                            }
                        }
                    }

                    // Extract Directories
                    if (config.contains("Directories") && config["Directories"].is_array())
                    {
                        for (const auto& dirEntry : config["Directories"])
                        {
                            if (dirEntry.contains("Path"))
                            {
                                directories.push_back(dirEntry["Path"].get<std::string>());
                            }
                        }
                    }
                }
                catch (const json::exception& e)
                {
                    std::cerr << "JSON parse error in " << entry.path() << ": " << e.what() << "\n";
                    continue;
                }
            }
        }

        // Output the results
        if (jsonOutput)
        {
            json output;
            output["Files"] = files;
            output["Directories"] = directories;
            std::println("{}", output.dump(4));
        }
        else
        {
            std::println();
            std::println("Files:");
            std::println("------");
            if (files.empty())
            {
                std::println("  None");
            }
            else
            {
                for (const auto& file : files)
                {
                    std::println("  {}", file);
                }
            }

            std::println();
            std::println("Directories:");
            std::println("------------");
            if (directories.empty())
            {
                std::println("  None");
            }
            else
            {
                for (const auto& dir : directories)
                {
                    std::println("  {}", dir);
                }
            }
            std::println();
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error listing config paths: " << e.what() << "\n";
        return 1;
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

    // Add fullSync flag
    bool fullSync{false};
    app.add_flag("-f,--fullSync", fullSync, "Start a full synchronization");

    // Add enable/disable sync flags
    bool enableSync{false};
    bool disableSync{false};
    app.add_flag("-e,--enableSync", enableSync, "Enable sync");
    app.add_flag("-d,--disableSync", disableSync, "Disable sync");

    // Add configPaths flag
    bool showConfigPaths{false};
    app.add_flag("-c,--configPaths", showConfigPaths,
                 "List all configured paths for syncing");

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

    // Handle fullSync option
    if (fullSync)
    {
        return startFullSync();
    }

    // Handle enableSync option
    if (enableSync)
    {
        return setSyncEnabled(true);
    }

    // Handle disableSync option
    if (disableSync)
    {
        return setSyncEnabled(false);
    }

    // Handle configPaths option
    if (showConfigPaths)
    {
        return listConfigPaths(jsonOutput);
    }

    // Default behavior when no options are provided
    std::cout << "Data Sync Tool initialized\n";
    std::cout << "Use --help for available options.\n";

    return 0;
}

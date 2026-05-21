// SPDX-License-Identifier: Apache-2.0

#include "fullsync.hpp"
#include "sync_properties.hpp"

#include <CLI/CLI.hpp>

#include <iostream>

int main(int argc, char* argv[])
{
    CLI::App app{
        "Data Sync Tool - Command line utility for phosphor-data-sync"};

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
        return datasynctool::sync_properties::displayStatus(jsonOutput);
    }

    // Handle fullSync option
    if (fullSync)
    {
        return datasynctool::fullsync::startFullSync();
    }

    // Handle enableSync option
    if (enableSync)
    {
        return datasynctool::sync_properties::setSyncEnabled(true);
    }

    // Handle disableSync option
    if (disableSync)
    {
        return datasynctool::sync_properties::setSyncEnabled(false);
    }

    // Default behavior when no options are provided
    std::cout << "Data Sync Tool initialized\n";
    std::cout << "Use --help for available options.\n";

    return 0;
}

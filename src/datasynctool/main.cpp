// SPDX-License-Identifier: Apache-2.0

#include "fullsync.hpp"
#include "status.hpp"

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
        return datasynctool::status::displayStatus(jsonOutput);
    }

    // Handle fullSync option
    if (fullSync)
    {
        return datasynctool::fullsync::startFullSync();
    }

    // Default behavior when no options are provided
    std::cout << "Data Sync Tool initialized\n";
    std::cout << "Use --help for available options.\n";

    return 0;
}

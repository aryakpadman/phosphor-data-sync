// SPDX-License-Identifier: Apache-2.0

#include "fullsync.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/common.hpp>

#include <iostream>
#include <print>

namespace datasynctool::fullsync
{

using SyncBMCData =
    sdbusplus::common::xyz::openbmc_project::control::SyncBMCData;

int startFullSync()
{
    try
    {
        auto bus = sdbusplus::bus::new_default();
        const std::string service = SyncBMCData::interface;
        const std::string path = SyncBMCData::instance_path;
        const std::string interface = SyncBMCData::interface;

        lg2::info("datasynctool attempting a full sync.");

        auto method = bus.new_method_call(service.c_str(), path.c_str(),
                                          interface.c_str(), "StartFullSync");

        auto reply = bus.call(method);

        std::println("Full sync initiated. See progress in journal logs");
        return 0;
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::cerr << "Error starting full sync: " << e.what() << "\n";
        return -1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return -1;
    }
}

} // namespace datasynctool::fullsync

// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "external_data_ifaces_impl.hpp"
#include "manager.hpp"

#include <csignal>
#include <systemd/sd-daemon.h> // sd_notify()
#include <thread> 
#include <chrono> 
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async/context.hpp>
#include <sdbusplus/server/manager.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/common.hpp>

void sighupHandler(int signum)
{
    if (signum == SIGHUP)
    {
        // To inform systemd manager that reload has been initated
        sd_notify(0, "RELOADING=1\nSTATUS=Data sync Reloadg started...");

        lg2::warning("Received SIGHUP! Reloading configurations!!");
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        lg2::warning("Reload finished!!!");

        //Inform that reload has finished
        sd_notify(0, "READY=1");
    }
}

int main()
{
    // Register the SIGHUP handler
    if (std::signal(SIGHUP, sighupHandler) == SIG_ERR)
    {
        lg2::error("Error registering SIGHUP handler");
        return 1;
    }

    using SyncBMCData =
        sdbusplus::common::xyz::openbmc_project::control::SyncBMCData;

    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t objManager{ctx, SyncBMCData::instance_path};

    data_sync::Manager manager{
        ctx, std::make_unique<data_sync::ext_data::ExternalDataIFacesImpl>(ctx),
        DATA_SYNC_CONFIG_DIR};

    // clang-tidy currently mangles this into something unreadable
    // NOLINTNEXTLINE
    ctx.spawn([](sdbusplus::async::context& ctx) -> sdbusplus::async::task<> {
        ctx.request_name(SyncBMCData::interface);
        co_return;
    }(ctx));
    sd_notify(0, "READY=1");
    ctx.run();

    return 0;
}

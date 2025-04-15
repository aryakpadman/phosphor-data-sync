// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "data_sync_config.hpp"

#include <sdbusplus/async.hpp>

#include <filesystem>

namespace data_sync::notify
{

namespace fs = std::filesystem;

#define DBUS_NOTIFY_INTERFACE "xyz.openbmc_project.Collection.DeleteAll"
#define DBUS_NOTIFY_METHOD "DeleteAll"

/**
 * @brief The structure which tells the mode of notification and how to notify
 * upon successful syncing of data
 */
class NotifyService
{
  public:
    
    /**
     * @brief The constructor
     */
    NotifyService(sdbusplus::async::context& ctx,
                  const fs::path& notifyFilePath);

  private:

    /**
     * @brief API to find the DBUS object path associated with the service which
     * implements the notify interface.
     */
     sdbusplus::async::task<std::string> getDbusObjectPath(
                sdbusplus::async::context& ctx, const std::string& service,
                const std::string& interface);

    /**
     * @brief API to initiate the DBUS notification call to the given service 
     */
     sdbusplus::async::task<> invokeNotifyDBusMethod(
                sdbusplus::async::context& ctx, const std::string& service); 

    /**
     *  @brief API to the initiate the DBUS notification call to all the
     *  configured services if the mode of notification is DBUS
     */
    sdbusplus::async::task<> sendDBusNotification(
                    sdbusplus::async::context& ctx, std::vector<std::string>
                    services, fs::path modifiedDataPath);

    /**
     *  @brief API to initiate the systemd service reload/restart if the mode of 
     * notification is systemd. depends on Method
     */
    sdbusplus::async::task<> sendSystemDNotification(
                    sdbusplus::async::context& ctx, std::vector<std::string>
                    services, std::string method);

    /**
     * @brief The API to trigger the notification to the configured service upon
     * receiving the request from the sibling BMC
     */
    sdbusplus::async::task<> init(sdbusplus::async::context& ctx, fs::path
                                        notifyFilePath);
};

} // namespace data_sync::notify



// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "data_sync_config.hpp"

#include <sdbusplus/async.hpp>

#include <filesystem>

namespace data_sync::notify
{

namespace fs = std::filesystem;

/**
 * @class NotifyService
 *
 * @brief The class which contains the APIs to process the sibling notification
 *        requests received from the sibling BMC on the local BMC and issue the
 *        necessary notifications to the configured services.
 */
class NotifyService
{
  public:
    /**
     * @brief Constructor
     *
     * @param[in] notifyFilePath - The root path of the received notify request
     * file.
     */
    NotifyService(sdbusplus::async::context& ctx,
                  const fs::path& notifyFilePath);

  private:
    /**
     * @brief Get the Dbus Object Path object associated with the service
     *        which implements the given interface.
     *
     * @param[in] service - The DBus service name
     * @param[in] interface - The DBus Interface name
     *
     * @return DBus Object path as sdbusplus::async::task<std::string>
     */
    sdbusplus::async::task<std::string>
        getDbusObjectPath(const std::string& service,
                          const std::string& interface);

    /**
     * @brief API to notify the requested services by triggering the DBus method
     *        implemented at the service to be notified.
     *
     * @param[in] service - The DBus service name which need to be notified
     *
     * @return void
     */
    sdbusplus::async::task<>
        invokeNotifyDBusMethod(const std::string& service,
                               const fs::path& modifiedDataPath);

    /**
     * @brief API to the initiate the DBus notification to all the
     *.       configured services if the mode of notification is DBus.
     *
     * @param[in] services - The vector of DBus service name which need to be
     *                       notified
     * @param[in] modifiedDataPath - The root path of the modified path given in
     *the request
     *
     * @return void
     */
    sdbusplus::async::task<>
        sendDBusNotification(std::vector<std::string> services,
                             fs::path modifiedDataPath);

    /**
     *  @brief API to initiate the systemd notification to all the configured
     *         services if the mode of notification is systemd. It will trigger
     *         either systemd reload or restart depends on the configured Method
     *         in the received request.
     *
     * @param[in] services - The vector of DBus service name which need to be
     *                       notified
     * @param[in] method - The method to be invoked as part of systemd
     * notification. Can have either 'Restart' or 'Reload' as values.
     */
    sdbusplus::async::task<>
        sendSystemDNotification(const std::vector<std::string>& services,
                                const std::string& method);

    /**
     * @brief The API to trigger the notification to the configured service upon
     * receiving the request from the sibling BMC
     *
     * @param notifyFilePath[in] - The root path of the received notify request
     * file.
     *
     * @return void
     */
    sdbusplus::async::task<> init(fs::path notifyFilePath);

    /**
     * @brief The async context object used to perform operations asynchronously
     *        as required.
     */
    sdbusplus::async::context& _ctx;
};

} // namespace data_sync::notify

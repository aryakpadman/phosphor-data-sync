// SPDX-License-Identifier: Apache-2.0
// TODO : check header files
#pragma once

#include "data_sync_config.hpp"

#include <sdbusplus/async.hpp>

#include <filesystem>

namespace data_sync::notify
{

namespace fs = std::filesystem;

#define DBUS_NOTIFY_INTERFACE "xyz.openbmc_project.Collection.DeleteAll"
#define DBUS_NOTIFY_METHOD "DeleteAll"

namespace file_operations
{
/**
 * @brief Macro for the directory path which contains the files for sibling
 * notification
 */ 
//#define NOTIFY_SERVICE_DIR "/var/lib/phosphor-data-sync/notify-services/"

/**
 * @brief API to read the notify Info in JSON format.
 */
nlohmann::json readfromFile(fs::path notifyFilePath);

}

/**
 * @brief The structure which contains the list of systemd services to be
 * notified as part of sibling notification.
 */
struct ServicesToNotify
{
    /**
     * @brief The constructor
     */
    ServicesToNotify(const std::vector<std::string>& services);

	std::vector<std::string> _services;
};

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
    NotifyService(sdbusplus::async::context& ctx, fs::path notifyFilePath);

  private:

    /**
     * @brief The enum which indicates the type of notification
     */
    //NotifyMode _notifyMode;

    /**
     * @brief The list of services which need to be notified.
     */
	//ServicesToNotify _servicesToNotify;

    /**
     * @brief The modified data path which caused the notification.
     */
    //fs::path _modifiedDataPath;

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
     sdbusplus::async::task<> invokeNotifyMethod(
                sdbusplus::async::context& ctx, const std::string& service); 

    /**
     *  @brief API to the initiate the DBUS notification call to all the
     *  configured services if the mode of notification is DBUS
     */
    sdbusplus::async::task<> sendDBusNotification(
                    sdbusplus::async::context& ctx, std::vector<std::string>
                    services, fs::path modifiedDataPath);

    /**
     *  @brief API to initiate the systemd service reload if the mode of 
     * notification is systemd.
     */
    sdbusplus::async::task<> systemdReload(
                    sdbusplus::async::context& ctx, std::vector<std::string>
                    services);

    /**
     * @brief The API to trigger the notification to the configured service upon
     * receiving the request from the sibling BMC
     */
    sdbusplus::async::task<> init(sdbusplus::async::context& ctx, fs::path
                                        notifyFilePath);
};

} // namespace data_sync::notify



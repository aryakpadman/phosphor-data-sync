// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "data_sync_config.hpp"
#include "external_data_ifaces_impl.hpp"

#include <sdbusplus/async.hpp>

#include <filesystem>

namespace data_sync::notify
{

namespace fs = std::filesystem;

/**
 * @brief The structure contains all retry-related details
 *        if notification fails for a service
 */
struct Retry
{
    /**
     * @brief The constructor
     *
     * @param[in] maxRetryAttempts - The number of retries
     * @param[in] retryIntervalInSec - The interval in which retry will
     *                                 occur.
     */
    Retry(uint8_t maxRetryAttempts,
          const std::chrono::seconds& retryIntervalInSec);

    /**
     * @brief Number of retries.
     */
    uint8_t _maxRetryAttempts;

    /**
     * @brief Retry interval in seconds
     */
    std::chrono::seconds _retryIntervalInSec;
};

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
    using CleanupCallback = std::function<void(NotifyService*)>;

    /**
     * @brief Construct a new Notify Service object
     *
     * @param[in] ctx - The async context object for asynchronous operation
     * @param[in] extDataIfaces - The external data interface object to get
     *                            the external data
     * @param[in] notifyFilePath - The root path of the received notify request
     * @param[in] maxRetryAttempts - The number of retries if notify fails
     * @param[in] retryIntervalInSec - The interval in which retry happens
     * @param[in] cleanup - Callback function to remove the object from parent
     *                      container
     */
    NotifyService(
        sdbusplus::async::context& ctx,
        data_sync::ext_data::ExternalDataIFaces& extDataIfaces,
        const fs::path& notifyFilePath, CleanupCallback cleanup,
        uint8_t maxRetryAttempts = 0,
        std::chrono::seconds retryIntervalInSec = std::chrono::seconds{0});

  private:
    /**
     * @brief API to parse the received notification request and to trigger
     *        systemd reload/restart for all the services and wait until the
     *        services reaches a terminal state (active / failed).
     *
     * @param[in] service - The systemd service to reload/restart
     * @param[in] systemdMethod - The action eed to perform on the service
     */
    sdbusplus::async::task<>
        sendSystemDNotification(const std::string& service,
                                const std::string& systemdMethod);

    /**
     * @brief API to parse the received notification request and to trigger
     *        systemd reload/restart for all the services
     *
     * @param[in] notifyRqstJson - The reference to the received notify request
     *
     */
    sdbusplus::async::task<>
        systemDNotify(const nlohmann::json& notifyRqstJson);

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

    /**
     * @brief An external data interface object used to seamlessly retrieve
     *        external dependent data.
     */
    data_sync::ext_data::ExternalDataIFaces& _extDataIfaces;

    /**
     * @brief The object containing retry related info
     *
     */
    Retry _retryInfo;

    /**
     * @brief  Callback function invoked when notification processing
     *         completes to remove the NotifyService object from the
     *         parent container
     */
    CleanupCallback _cleanup;
};

} // namespace data_sync::notify

#pragma once

#include <app.hpp>
#include <async_resp.hpp>
#include <dbus_utility.hpp>
#include <error_messages.hpp>
#include <nlohmann/json.hpp>
#include <query.hpp>
#include <sdbusplus/asio/property.hpp>

namespace redfish
{

inline void requestPowerSpikeLogEntries(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    // Explicitly seed the default root path metadata parameters to match parent schemas
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Managers/bmc/LogServices";
    asyncResp->res.jsonValue["@odata.type"] = "#LogServiceCollection.LogServiceCollection";
    asyncResp->res.jsonValue["Name"] = "Open BMC Log Services Collection";
    asyncResp->res.jsonValue["Description"] = "Collection of LogServices for this Manager";

    // Dynamic asynchronous D-Bus sensor parameter lookup loop
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, 
        "xyz.openbmc_project.OobPowerMitigator",
        "/xyz/openbmc_project/OobPowerMitigator",
        "xyz.openbmc_project.OobPowerMitigator", 
        "LastSpikeEvent",
        [asyncResp](const boost::system::error_code ec, const std::string& lastSpikeJson) {
            
            // 1. Safeguard array initialization directly inside the async execution loop context
            nlohmann::json& logEntryArray = asyncResp->res.jsonValue["Members"];
            if (!logEntryArray.is_array()) {
                logEntryArray = nlohmann::json::array();
                // If the system cleared it, ensure the base Journal loop is preserved
                nlohmann::json::object_t journalNode;
                journalNode["@odata.id"] = "/redfish/v1/Managers/bmc/LogServices/Journal";
                logEntryArray.push_back(std::move(journalNode));
            }

            if (ec)
            {
                // Fallback gracefully if the background out-of-band daemon drops off the system bus
                asyncResp->res.jsonValue["Members@odata.count"] = logEntryArray.size();
                return;
            }

            try
            {
                // 2. Parse the dynamic C daemon JSON string parameter
                nlohmann::json spikeData = nlohmann::json::parse(lastSpikeJson);
                
                // 3. Build a valid, inline standard Redfish LogService data block profile node
                nlohmann::json::object_t entry;
                entry["@odata.id"] = "/redfish/v1/Managers/bmc/LogServices/VoltSentinel";
                entry["@odata.type"] = "#LogService.v1_1_0.LogService";
                entry["Id"] = "VoltSentinel";
                entry["Name"] = "VoltSentinel Power Diagnostics Log Service";
                entry["Description"] = spikeData.value("Message", "Out-of-band transient power tracking active.");
                
                nlohmann::json::object_t statusObj;
                statusObj["State"] = "Enabled";
                statusObj["Health"] = "Critical"; // Injects alert states into datacenter dashboards
                entry["Status"] = std::move(statusObj);

                // 4. Safely push our verified metadata asset array node onto the visible stack list
                logEntryArray.push_back(std::move(entry));
                asyncResp->res.jsonValue["Members@odata.count"] = logEntryArray.size();
            }
            catch (const nlohmann::json::parse_error&)
            {
                // Fallback gracefully if JSON structures are malformed
                asyncResp->res.jsonValue["Members@odata.count"] = logEntryArray.size();
            }
        });
}

inline void registerPowerSpikeMitigatorRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/bmc/LogServices")
        .privileges({{"ConfigureComponents"}, {"ConfigureManager"}})
        (std::bind_front(requestPowerSpikeLogEntries, std::ref(app)));
}

} // namespace redfish


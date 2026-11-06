#include "power_spike_mitigator.hpp"
#include <app.hpp>

// This namespace initialization block executes automatically on binary startup
namespace redfish {
    class VoltSentinelInitializer {
    public:
        VoltSentinelInitializer() {
            // Instantiate a local reference to latch our custom endpoint definitions
            // natively into bmcweb's running core routing engine tables
            crow::App app;
            registerPowerSpikeMitigatorRoutes(app);
        }
    };

    // Static instance forces instant, seamless execution before main() initializes
    static VoltSentinelInitializer voltSentinelInit;
}


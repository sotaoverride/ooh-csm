#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <systemd/sd-bus.h>

#define HARDWARE_PATH       "/sys/class/hwmon/hwmon0/curr1_input"
#define SIMULATION_PATH     "/tmp/hwmon0/curr1_input"
#define THRESHOLD_MA        120000 
#define SAMPLING_INTERVAL_NS 5000000 

// Explicitly size the global array buffer to securely hold incoming string text
char global_last_spike_json[512] = "{}";
sd_bus *bus = NULL;

void trigger_mitigation(int current_ma) {
    fprintf(stderr, "\n[CRITICAL] Spike Intercepted: %d mA! Deploying throttle.\n", current_ma);
    
    time_t rawtime;
    time(&rawtime);
    struct tm *timeinfo = gmtime(&rawtime);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", timeinfo);

    // Single backslashes correctly escape quotes for a valid internal JSON string
    snprintf(global_last_spike_json, sizeof(global_last_spike_json),
             "{\"Message\": \"Out-of-band current spike detected: %d mA. Throttling engaged.\", \"Severity\": \"Critical\", \"Created\": \"%s\"}", 
             current_ma, timestamp);

    if (bus) {
        sd_bus_emit_properties_changed(bus, "/xyz/openbmc_project/OobPowerMitigator", "xyz.openbmc_project.OobPowerMitigator", "LastSpikeEvent", NULL);
    }
}

static int get_last_spike(sd_bus *b, const char *path, const char *interface, const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *error) {
    return sd_bus_message_append(reply, "s", global_last_spike_json);
}

static const sd_bus_vtable mitigator_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("LastSpikeEvent", "s", get_last_spike, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END
};

int main(void) {
    printf("[OOB-CSM] Initializing Out-of-Band Spike Mitigator D-Bus Interface...\n");
    
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        fprintf(stderr, "[ERROR] Failed to connect to system bus: %s\n", strerror(-r));
        return EXIT_FAILURE;
    }

    r = sd_bus_add_object_vtable(bus, NULL, "/xyz/openbmc_project/OobPowerMitigator", "xyz.openbmc_project.OobPowerMitigator", mitigator_vtable, NULL);
    if (r < 0) {
        fprintf(stderr, "[ERROR] Failed to add VTABLE layout: %s\n", strerror(-r));
        return EXIT_FAILURE;
    }

    r = sd_bus_request_name(bus, "xyz.openbmc_project.OobPowerMitigator", 0);
    if (r < 0) {
        fprintf(stderr, "[ERROR] Failed to request bus name: %s\n", strerror(-r));
        return EXIT_FAILURE;
    }

    const char *target_path = HARDWARE_PATH;
    int fd = open(target_path, O_RDONLY);
    if (fd < 0 && errno == ENOENT) {
        target_path = SIMULATION_PATH;
        fd = open(target_path, O_RDONLY);
    }

    if (fd < 0) {
        fprintf(stderr, "[ERROR] Failed to open path: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    struct timespec next_sample;
    clock_gettime(CLOCK_MONOTONIC, &next_sample);
    int mitigation_active = 0; 
    char buffer[32]; // Explicitly sized buffer array

    while (1) {
        // Run D-Bus message processing tasks
        sd_bus_process(bus, NULL);

        // Explicitly clear the memory buffer out on every single pass
        memset(buffer, 0, sizeof(buffer));
        
        // Reset the file descriptor read offset back to index 0
        if (lseek(fd, 0, SEEK_SET) < 0) {
            fprintf(stderr, "[ERROR] lseek failed: %s\n", strerror(errno));
            break;
        }

        // Only process data if we successfully read fresh bytes from disk
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0'; // Enforce hard null-termination boundary
            int current_ma = atoi(buffer);
            
            if (current_ma >= THRESHOLD_MA) {
                if (!mitigation_active) {
                    trigger_mitigation(current_ma);
                    mitigation_active = 1; 
                }
            } else if (current_ma < (THRESHOLD_MA - 10000)) {
                if (mitigation_active) {
                    printf("[OOB-CSM] Current stabilized at %d mA. Re-arming safety trigger.\n", current_ma);
                    mitigation_active = 0;
                }
            }
        }

        // Dynamic 5ms scheduling delay matrix configuration
        next_sample.tv_nsec += SAMPLING_INTERVAL_NS;
        if (next_sample.tv_nsec >= 1000000000) {
            next_sample.tv_sec += 1;
            next_sample.tv_nsec -= 1000000000;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_sample, NULL);
    }


    close(fd);
    sd_bus_unref(bus);
    return EXIT_SUCCESS;
}


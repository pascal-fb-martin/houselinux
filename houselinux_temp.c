/* Houselinux - a web server to collect Linux metrics.
 *
 * Copyright 2024, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * houselinux_temp.c - Collect metrics on local temperature sensors.
 *
 * SYNOPSYS:
 *
 * void houselinux_temp_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void houselinux_temp_background (time_t now);
 *
 *    The periodic function that manages the collect of metrics.
 *
 * int houselinux_temp_summary (char *buffer, int size);
 *
 *    A function that populates a short summary of the temperatures in JSON.
 *
 * int houselinux_temp_status (char *buffer, int size);
 *
 *    A function that populates a compact status of the temperatures in JSON.
 *
 * int houselinux_temp_details (char *buffer, int size, time_t now, time_t since);
 *
 *    A function that populates a detailed report of the temperatures in JSON.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include "houselog.h"
#include "houselinux_reduce.h"
#include "houselinux_temp.h"

#include "houselog_sensor.h"

#define HOUSE_TEMP_PERIOD  5 // Sample temperature metrics every 5 seconds.
#define HOUSE_TEMP_SPAN   60 // MUST KEEP A 5 MINUTES HISTORY.

struct HouseTempMetrics {
    time_t timestamp[HOUSE_TEMP_SPAN];
    long long cpu[HOUSE_TEMP_SPAN];
    long long gpu[HOUSE_TEMP_SPAN];
};

static struct HouseTempMetrics HouseTempLatest;

static char HouseTempCpuPath[512] = {0};
static char HouseTempGpuPath[512] = {0};

void houselinux_temp_initialize (int argc, const char **argv) {

    // Find out which sensors represent the CPU and GPU (if any).
    int i;
    for (i = 0; i < 32; ++i) {
        char path[512];
        snprintf (path, sizeof(path), "/sys/class/hwmon/hwmon%d/name", i);
        FILE *f = fopen (path, "r");
        if (!f) break;
        char buffer[512];
        char *line = fgets (buffer, sizeof(buffer), f);
        int cursor = 0;
        while (line[cursor] >= ' ') cursor += 1;
        line[cursor] = 0;
        int is_cpu = 0;
        int is_gpu = 0;
        if (!strcmp (line, "k10temp")) {            // AMD CPU.
            is_cpu = 1;
        } else if (!strcmp (line, "cpu_thermal")) { // Raspberry Pi (others?)
            is_cpu = 1;
        } else if (!strcmp (line, "coretemp")) {    // Intel.
            is_cpu = 1;
        } else if (!strcmp (line, "amdgpu")) {      // AMD Radeon GPU.
            is_gpu = 1;
        } else if (!strcmp (line, "radeon")) {      // Old AMD Radeon driver.
            is_gpu = 1;
        }
        if (is_cpu) {
            snprintf (HouseTempCpuPath, sizeof(HouseTempCpuPath),
                      "/sys/class/hwmon/hwmon%d/temp1_input", i);
        } else if (is_gpu) {
            snprintf (HouseTempGpuPath, sizeof(HouseTempGpuPath),
                      "/sys/class/hwmon/hwmon%d/temp1_input", i);
        }
        fclose (f);
    }
}

int houselinux_temp_status (char *buffer, int size) {

    int cursor = 0;

    cursor = snprintf (buffer, size, ",\"temp\":");
    if (cursor >= size) return 0;
    int start = cursor;

    if (HouseTempCpuPath[0]) {
        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "cpu",
                                          HouseTempLatest.cpu,
                                          HOUSE_TEMP_SPAN, "mC");
        if (cursor >= size) return 0;
    }

    if (HouseTempGpuPath[0]) {
        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "gpu",
                                          HouseTempLatest.gpu,
                                          HOUSE_TEMP_SPAN, "mC");
        if (cursor >= size) return 0;
    }

    if (cursor <= start) return 0; // No data to report.

    buffer[start] = '{';
    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) return 0;

    return cursor;
}

int houselinux_temp_summary (char *buffer, int size) {
    return houselinux_temp_status (buffer, size); // Already the shortest.
}

int houselinux_temp_details (char *buffer, int size, time_t now, time_t since) {

    int cursor = 0;

    cursor = snprintf (buffer, size, ",\"temp\":");
    if (cursor >= size) return 0;

    int start = cursor;

    if (HouseTempCpuPath[0]) {
        cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor,
                                                  since, "cpu", "mC", now,
                                                  HOUSE_TEMP_PERIOD, HOUSE_TEMP_SPAN,
                                                  HouseTempLatest.timestamp,
                                                  HouseTempLatest.cpu);
        if (cursor >= size) return 0;
    }

    if (HouseTempGpuPath[0]) {
        cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor,
                                                  since, "gpu", "mC", now,
                                                  HOUSE_TEMP_PERIOD, HOUSE_TEMP_SPAN,
                                                  HouseTempLatest.timestamp,
                                                  HouseTempLatest.gpu);
        if (cursor >= size) return 0;
    }

    if (cursor <= start) return 0; // No data to report.

    buffer[start] = '{';
    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) return 0;

    return cursor;
}

static void houselinux_temp_read (const char *path, long long *item) {

    *item = 0;
    FILE *f = fopen (path, "r");
    if (!f) return;
    char buffer[512];
    char *line = fgets (buffer, sizeof(buffer), f);
    fclose (f);
    *item = atoll (line);
}

void houselinux_temp_background (time_t now) {

    static time_t NextTempCollect = 0;
    static time_t NextTempRecord = 0;
    static char LocalHost[256] = {0};

    if (now >= NextTempCollect) {

        NextTempCollect = now + HOUSE_TEMP_PERIOD;
        int index = (now / HOUSE_TEMP_PERIOD) % HOUSE_TEMP_SPAN;

        if (HouseTempCpuPath[0])
            houselinux_temp_read (HouseTempCpuPath, HouseTempLatest.cpu + index);
        if (HouseTempGpuPath[0])
            houselinux_temp_read (HouseTempGpuPath, HouseTempLatest.gpu + index);
        HouseTempLatest.timestamp[index] = now;
    }

    // Record the CPU temperature's average value as sensor data.
    // This is to plug in with any application consoming sensor data.

    if (!NextTempRecord) {
        houselog_sensor_initialize ("metrics", 0, 0);
        gethostname (LocalHost, sizeof(LocalHost));
        int collectioninterval = HOUSE_TEMP_PERIOD * HOUSE_TEMP_SPAN;
        NextTempRecord = now + collectioninterval;

        // Record data at multiple of the collection interval.
        if (NextTempRecord % collectioninterval) {
           NextTempRecord -= NextTempRecord % collectioninterval;
           NextTempRecord += collectioninterval;
        }
    }

    if (now >= NextTempRecord) {

        NextTempRecord = now + (HOUSE_TEMP_PERIOD * HOUSE_TEMP_SPAN);

        struct timeval timestamp;
        timestamp.tv_sec = now;
        timestamp.tv_usec = 0;

        int i;
        long long total = 0;
        for (i = HOUSE_TEMP_SPAN - 1; i >= 0; --i)
            total += HouseTempLatest.cpu[i];
        long long average = total / (1000 * HOUSE_TEMP_SPAN);

        houselog_sensor_numeric
            (&timestamp, LocalHost, "temp.cpu", average, "°C");
        houselog_sensor_flush ();
    }
    houselog_sensor_background (now);
}


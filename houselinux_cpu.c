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
 * houselinux_cpu.c - Collect metrics on the Linux CPU resources.
 *
 * SYNOPSYS:
 *
 * void houselinux_cpu_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void houselinux_cpu_background (time_t now);
 *
 *    The periodic function that manages the collect of metrics.
 *
 * int houselinux_cpu_status (char *buffer, int size);
 *
 *    A function that populates a compact status of the CPU usage in JSON.
 *
 * int houselinux_cpu_details (char *buffer, int size, time_t now, time_t since);
 *
 *    A function that populates a detailed report of the CPU usage in JSON.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include "houselog.h"
#include "houselinux_reduce.h"
#include "houselinux_cpu.h"

#define HOUSE_CPU_PERIOD  5 // Sample CPU metrics every 5 seconds.
#define HOUSE_CPU_SPAN   60 // MUST KEEP A 5 MINUTES HISTORY.

struct HouseCpuMetrics {
    time_t timestamp[HOUSE_CPU_SPAN];
    long long busy[HOUSE_CPU_SPAN];
    long long iowait[HOUSE_CPU_SPAN];
    long long steal[HOUSE_CPU_SPAN];
    long long load1;
    long long load5;
    long long load15;
};

static struct HouseCpuMetrics HouseCpuLatest;


void houselinux_cpu_initialize (int argc, const char **argv) {
    // TBD
}

int houselinux_cpu_status (char *buffer, int size) {

    int cursor = 0;

    cursor = snprintf (buffer, size, ",\"cpu\":");
    if (cursor >= size) return 0;

    int start = cursor;
    cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                      "busy",
                                      HouseCpuLatest.busy,
                                      HOUSE_CPU_SPAN, "%");
    if (cursor >= size) return 0;

    cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                      "iowait",
                                      HouseCpuLatest.iowait,
                                      HOUSE_CPU_SPAN, "%");
    if (cursor >= size) return 0;

    cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                      "steal",
                                      HouseCpuLatest.steal,
                                      HOUSE_CPU_SPAN, "%");
    if (cursor >= size) return 0;

    if ((HouseCpuLatest.load1 > 0) ||
        (HouseCpuLatest.load5 > 0) ||
        (HouseCpuLatest.load15 > 0)) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            ",\"load\":[%lld,%lld,%lld,0]",
                            HouseCpuLatest.load1,
                            HouseCpuLatest.load5,
                            HouseCpuLatest.load15);
        if (cursor >= size) return 0;
    }
    if (cursor <= start) return 0; // No data to report.

    buffer[start] = '{';
    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) return 0;

    return cursor;
}

int houselinux_cpu_details (char *buffer, int size, time_t now, time_t since) {

    int cursor = 0;

    cursor = snprintf (buffer, size, ",\"cpu\":");
    if (cursor >= size) return 0;

    int start = cursor;
    cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                                              "busy", "%", now,
                                              HOUSE_CPU_PERIOD, HOUSE_CPU_SPAN,
                                              HouseCpuLatest.timestamp,
                                              HouseCpuLatest.busy);
    if (cursor >= size) return 0;

    cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                                              "iowait", "%", now,
                                              HOUSE_CPU_PERIOD, HOUSE_CPU_SPAN,
                                              HouseCpuLatest.timestamp,
                                              HouseCpuLatest.iowait);
    if (cursor >= size) return 0;

    cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                                              "steal", "%", now,
                                              HOUSE_CPU_PERIOD, HOUSE_CPU_SPAN,
                                              HouseCpuLatest.timestamp,
                                              HouseCpuLatest.steal);
    if (cursor >= size) return 0;

    if ((HouseCpuLatest.load1 > 0) ||
        (HouseCpuLatest.load5 > 0) ||
        (HouseCpuLatest.load15 > 0)) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            ",\"load\":[%lld,%lld,%lld,0]",
                            HouseCpuLatest.load1,
                            HouseCpuLatest.load5,
                            HouseCpuLatest.load15);
        if (cursor >= size) return 0;
    }
    if (cursor <= start) return 0; // No data to report.

    buffer[start] = '{';
    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) return 0;

    return cursor;
}

static void houselinux_cpu_load (struct HouseCpuMetrics *latest) {

    char buffer[80];
    FILE *f = fopen ("/proc/loadavg", "r");
    if (!f) return;

    char *line = fgets (buffer, sizeof(buffer), f);
    if (line) {
        float v[3];
        int count = sscanf (line, "%f %f %f ", &v[0], &v[1], &v[2]);
        if (count == 3) {
            latest->load1 = (long long)(v[0] * 100);
            latest->load5 = (long long)(v[1] * 100);
            latest->load15 = (long long)(v[2] * 100);
        }
    }
    fclose (f);
}

static void houselinux_cpu_stat (struct HouseCpuMetrics *latest, int index) {

    static long long Previous[16];

    if (latest) {
        // Reset all the metrics, in case these are not accessible;
        latest->busy[index] = latest->iowait[index] = 0;
    }

    char buffer[80];
    FILE *f = fopen ("/proc/stat", "r");
    if (!f) return;

    long long value[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    while (!feof (f)) {
        char *line = fgets (buffer, sizeof(buffer), f);
        if (!line) break;

        // This is an optimization: the only line with a space at that position
        // is the aggregated CPU numbers line. This eliminate the other lines
        // at the lowest possible cost.
        if (line[3] != ' ') continue;

        if (strncmp (line, "cpu", 3)) continue;

        // At this point, this can only be the "cpu" line.
        line += 3;

        int i;
        for (i = 0; i < 16; ++i) {
            for (line += 1; *line > ' '; ++line) ; // Skip previous item.
            if (line[0] < ' ') break; // end of line.
            value[i] = atoll(line);
        }
        if (i > 10) printf ("WARNING: %d items (> 10) in /proc/stat.\n", i);
        if (i < 10) printf ("WARNING: %d items (< 10) in /proc/stat.\n", i);

        // Now that we have the raw data, calculate the metrics that will
        // be published. The values are:
        // 0: CPU in user mode
        // 1: CPU in user low priority (nice)
        // 2: CPU in system
        // 3: CPU idle.
        // 4: CPU io wait.
        // 5: CPU in IRQ
        // 6: CPU in soft IRQ
        // 7: CPU stolen (if a VM guest)
        // 8: CPU for guest (if a VM host)
        // 9: CPU for guest at low priority (if a VM host)

        if (latest) {
            // Exclude guests, apparently already counted in 0 and 1?
            long long total = 0;
            for (i = 7; i >= 0; --i) total += value[i] - Previous[i];

            if (total <= 0) {
                latest->busy[index] = 0;
                latest->iowait[index] = 0;
            } else {
                long long steal = value[7] - Previous[7];
                long long iowait = value[4] - Previous[4];
                long long idle = (value[3] - Previous[3]) + iowait;
                latest->busy[index] = (100 * (total - idle)) / total;
                latest->iowait[index] = (100 * iowait) / total;
                latest->steal[index] = (100 * steal) / total;
            }
        }
        memcpy (Previous, value, sizeof(Previous)); // Baseline for next time.
        break; // We got all that we were looking for.
    }
    fclose (f);
}

void houselinux_cpu_background (time_t now) {

    static time_t NextCpuCollect = 0;

    if (now < NextCpuCollect) return;
    int firsttime = (NextCpuCollect == 0);
    NextCpuCollect = now + HOUSE_CPU_PERIOD;

    if (firsttime) {
        houselinux_cpu_stat (0, 0); // Just setup the first baseline.
    } else {
        int index = (now / HOUSE_CPU_PERIOD) % HOUSE_CPU_SPAN;
        houselinux_cpu_stat (&HouseCpuLatest, index);
        houselinux_cpu_load (&HouseCpuLatest);
        HouseCpuLatest.timestamp[index] = now;
    }
}


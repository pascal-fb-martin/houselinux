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
 *
 * houselinux_memory.c - Collect metrics on the Linux memory resources.
 *
 * SYNOPSYS:
 *
 * void houselinux_memory_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void houselinux_memory_background (time_t now);
 *
 *    The periodic function that manages the collect of metrics.
 *
 * int houselinux_memory_status (char *buffer, int size);
 *
 *    A function that populates a status overview of the memory usage in JSON.
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <echttp.h>
#include <echttp_static.h>
#include <echttp_json.h>

#include "houselog.h"
#include "houselinux_reduce.h"
#include "houselinux_memory.h"

#define DEBUG if (echttp_isdebug()) printf

#define HOUSE_MEMORY_PERIOD 10 // Sample memory metrics every 10 seconds.
#define HOUSE_MEMORY_SPAN   30 // MUST KEEP A 5 MINUTES HISTORY.

// Here swaptotal is considered to be fixed for the whole cycle, even
// while it may change if the swap is extended. That is exceedingly
// infrequent, so consider swaptotal as constant anyway.
// In addition, the free swap space is not really the focus here:
// since using the swap is bad for performances, the main information
// is how much data was swapped out.
//
struct HouseMemoryMetrics {
    time_t timestamp[HOUSE_MEMORY_SPAN];
    long long memtotal;
    long long memavailable[HOUSE_MEMORY_SPAN];
    long long memdirty[HOUSE_MEMORY_SPAN];
    long long swaptotal;
    long long swapped[HOUSE_MEMORY_SPAN];
};

static struct HouseMemoryMetrics HouseMemoryLatest;


void houselinux_memory_initialize (int argc, const char **argv) {
    // TBD
}

int houselinux_memory_status (char *buffer, int size) {

    if (HouseMemoryLatest.memtotal == 0) return 0; // Nothing to report?
    int cursor = 0;

    cursor = snprintf (buffer, size,
                       "\"memory\":{\"size\":[%lld,\"MB\"]",
                       HouseMemoryLatest.memtotal);
    if (cursor >= size) return 0;

    cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                      "available",
                                      HouseMemoryLatest.memavailable,
                                      HOUSE_MEMORY_SPAN, "MB");

    cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                      "dirty",
                                      HouseMemoryLatest.memdirty,
                                      HOUSE_MEMORY_SPAN, "MB");

    if (HouseMemoryLatest.swaptotal > 0) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            ",\"swap\":[%lld,\"MB\"]",
                            HouseMemoryLatest.swaptotal);
        if (cursor >= size) return 0;

        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "swapped",
                                          HouseMemoryLatest.swapped,
                                          HOUSE_MEMORY_SPAN, "MB");
        if (cursor >= size) return 0;
    }
    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) return 0;

    return cursor;
}

static void houselinux_memory_meminfo (struct HouseMemoryMetrics *latest, int index) {

    // First reset all the metrics, in case these are not accessible;
    latest->memavailable[index] = latest->memdirty[index] = 0;
    latest->swaptotal = latest->swapped[index] = 0;

    char buffer[80];
    FILE *f = fopen ("/proc/meminfo", "r");
    if (!f) return;

    long long swapfree = 0;

    while (!feof (f)) {
        char *line = fgets (buffer, sizeof(buffer), f);
        if (!line) break;

        // This is an accelerator, checking if the 2nd character matches
        // anything of interest. UPDATE THE STRINGS IF NEW ITEMS ARE RECOVERED.
        // This check eliminates 40 of the 55 lines.
        // Check the 2nd character since it has less matches than the 1st.
        // (The code checks the first character anyway--see later.)
        if (!strchr ("ewi", line[1])) continue;

        char *sep = strchr (line, ':');
        if (!sep) continue;
        *(sep++) = 0;

        if (line[0] == 'M') {
            if (!strcmp (line, "MemTotal")) {
                latest->memtotal = atoll (sep) / 1024; // kB to MB unit.
            } else if (!strcmp (line, "MemAvailable")) {
                latest->memavailable[index] =
                    atoll (sep) / 1024; // kB to MB unit.
            }
            continue;
        }
        if (line[0] == 'S') {
            if (!strcmp (line, "SwapTotal")) {
                latest->swaptotal = atoll (sep) / 1024; // kB to MB unit.
            } else if (!strcmp (line, "SwapFree")) {
                swapfree = atoll (sep) / 1024; // kB to MB unit.
            }
            continue;
        }
        if (line[3] == 't') { // 4th Character has a lot less matches.
            if (!strcmp (line, "Dirty")) {
                latest->memdirty[index] = atoll (sep) / 1024; // kB to MB unit.
            }
            continue;
        }
    }
    if (latest->swaptotal > 0)
        latest->swapped[index] = latest->swaptotal - swapfree;

    fclose (f);
}

void houselinux_memory_background (time_t now) {

    static time_t NextMemoryCollect = 0;

    if (now < NextMemoryCollect) return;
    NextMemoryCollect = now + HOUSE_MEMORY_PERIOD;

    int index = (now / HOUSE_MEMORY_PERIOD) % HOUSE_MEMORY_SPAN;

    houselinux_memory_meminfo (&HouseMemoryLatest, index);
    HouseMemoryLatest.timestamp[index] = now;
}


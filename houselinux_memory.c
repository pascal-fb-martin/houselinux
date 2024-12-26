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
#include "houselinux_memory.h"

#define DEBUG if (echttp_isdebug()) printf

struct HouseMemoryMetrics {
    time_t timestamp;
    long long memtotal;
    long long memavailable;
    long long memdirty;
    long long swaptotal;
    long long swapfree;
};

#define HOUSE_MEMORY_PERIOD 10 // Sample memory metrics every 10 seconds.
#define HOUSE_MEMORY_SPAN   30 // MUST KEEP A 5 MINUTES HISTORY.

static struct HouseMemoryMetrics HouseMemoryLatest[HOUSE_MEMORY_SPAN];


void houselinux_memory_initialize (int argc, const char **argv) {
    // TBD
}

void houselinux_memory_minimum (long long value, long long *dest) {
    if ((*dest == 0) || (value < *dest)) *dest = value;
}

void houselinux_memory_maximum (long long value, long long *dest) {
    if (value > *dest) *dest = value;
}

int houselinux_memory_status (char *buffer, int size) {

    int cursor = 0;
    long long memtotal = 0;
    long long swaptotal = 0;


    struct HouseMemoryMetrics min = {0, 0, 0, 0, 0, 0};
    struct HouseMemoryMetrics max = {0, 0, 0, 0, 0, 0};

    int n;
    int i = (time(0) / HOUSE_MEMORY_PERIOD) % HOUSE_MEMORY_SPAN;
    for (n = HOUSE_MEMORY_SPAN; n > 0; --n) {
        if (++i >= HOUSE_MEMORY_SPAN) i = 0;
        struct HouseMemoryMetrics *metrics = HouseMemoryLatest + i;
        if (! metrics->timestamp) continue;

        houselinux_memory_minimum (metrics->memavailable, &(min.memavailable));
        houselinux_memory_maximum (metrics->memavailable, &(max.memavailable));

        houselinux_memory_minimum (metrics->memdirty, &(min.memdirty));
        houselinux_memory_maximum (metrics->memdirty, &(max.memdirty));

        houselinux_memory_minimum (metrics->swapfree, &(min.swapfree));
        houselinux_memory_maximum (metrics->swapfree, &(max.swapfree));

        if (memtotal == 0) { // Same total values everytime (swap could be 0).
            memtotal = metrics->memtotal;
            swaptotal = metrics->swaptotal;
        }
    }
    if (memtotal == 0) return 0; // Nothing to report?

    char mem_ascii[128];
    if (min.memavailable == max.memavailable) {
        snprintf (mem_ascii, sizeof(mem_ascii),
                  "\"available\":[%lld,\"MB\"],\"used\":[%d,\"%%\"]",
                  min.memavailable,
                  (int)((100 * (memtotal - max.memavailable)) / memtotal));
    } else {
        snprintf (mem_ascii, sizeof(mem_ascii),
                  "\"available\":[%lld,%lld,\"MB\"],\"used\":[%d,%d,\"%%\"]",
                  min.memavailable,
                  max.memavailable,
                  (int)((100 * (memtotal - max.memavailable)) / memtotal),
                  (int)((100 * (memtotal - min.memavailable)) / memtotal));
    }

    char dirty_ascii[64];
    if (min.memdirty == max.memdirty) {
        if (min.memdirty == 0) {
            dirty_ascii[0] = 0;
        } else {
            snprintf (dirty_ascii, sizeof(dirty_ascii),
                      "\"dirty\":[%lld,\"MB\"]", min.memdirty);
        }
    } else {
        snprintf (dirty_ascii, sizeof(dirty_ascii),
                  "\"dirty\":[%lld,%lld,\"MB\"]", min.memdirty, max.memdirty);
    }

    char swap_ascii[128];
    if (swaptotal == 0) {
        swap_ascii[0] = 0;
    } else if (min.swapfree == max.swapfree) {
        snprintf (swap_ascii, sizeof(swap_ascii),
                  "\"swap\":[%lld,\"MB\"],\"swapped\":[%lld,\"MB\"]",
                  swaptotal, swaptotal - max.swapfree);
    } else {
        snprintf (swap_ascii, sizeof(swap_ascii),
                  "\"swap\":[%lld,\"MB\"],\"swapped\":[%lld,%lld,\"MB\"]",
                  swaptotal,
                  swaptotal - max.swapfree, swaptotal - min.swapfree);
    }

    cursor = snprintf (buffer, size,
                       "\"memory\":{\"ram\":[%lld,\"MB\"],%s,%s,%s}",
                      memtotal, mem_ascii, dirty_ascii, swap_ascii);
    if (cursor >= size) return 0;
    return cursor;
}

static void houselinux_memory_meminfo (struct HouseMemoryMetrics *metrics) {

    // First reset all the metrics, in case these are not accessible;
    metrics->memtotal = metrics->memavailable = metrics->memdirty = 0;
    metrics->swaptotal = metrics->swapfree = 0;

    char buffer[80];
    FILE *f = fopen ("/proc/meminfo", "r");
    if (!f) return;

    while (!feof (f)) {
        char *line = fgets (buffer, sizeof(buffer), f);
        if (!line) break;

        // This is an accelerator, checking if the first character matches
        // anything of interest. UPDATE THE STRING IF NEW ITEMS ARE RECOVERED.
        if (!strchr ("MSD", line[0])) continue;

        char *sep = strchr (line, ':');
        if (!sep) continue;
        *sep = 0;

        if (!strncmp (line, "Mem", 3)) {
            if (!strcmp (line+3, "Total")) {
                metrics->memtotal = atoll (sep+1) / 1024; // kB to MB unit.
                continue;
            }
            if (!strcmp (line+3, "Available")) {
                metrics->memavailable = atoll (sep+1) / 1024; // kB to MB unit.
                continue;
            }
        }
        if (!strncmp (line, "Swap", 4)) {
            if (!strcmp (line+4, "Total")) {
                metrics->swaptotal = atoll (sep+1) / 1024; // kB to MB unit.
                continue;
            }
            if (!strcmp (line+4, "Free")) {
                metrics->swapfree = atoll (sep+1) / 1024; // kB to MB unit.
                continue;
            }
        }
        if (!strcmp (line, "Dirty")) {
            metrics->memdirty = atoll (sep+1) / 1024; // kB to MB unit.
            continue;
        }
    }
    fclose (f);
}

void houselinux_memory_background (time_t now) {

    static time_t NextMemoryCollect = 0;

    if (now < NextMemoryCollect) return;
    NextMemoryCollect = now + HOUSE_MEMORY_PERIOD;

    int index = (now / HOUSE_MEMORY_PERIOD) % HOUSE_MEMORY_SPAN;

    houselinux_memory_meminfo (HouseMemoryLatest+index);
    HouseMemoryLatest[index].timestamp = now;
}


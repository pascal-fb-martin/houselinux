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
 * houselinux_netio.c - Collect metrics on the Linux network IO performances.
 *
 * SYNOPSYS:
 *
 * void houselinux_netio_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void houselinux_netio_background (time_t now);
 *
 *    The periodic function that manages the collect of metrics.
 *
 * int houselinux_netio_status (char *buffer, int size);
 *
 *    A function that populates a status overview of the network IO in JSON.
 *
 * int houselinux_netio_details (char *buffer, int size, time_t now, time_t since);
 *
 *    A function that populates a detailed report of the network IO in JSON.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include "houselog.h"
#include "houselinux_reduce.h"
#include "houselinux_netio.h"

#define HOUSE_NETIO_PERIOD  5 // Sample CPU metrics every 5 seconds.
#define HOUSE_NETIO_SPAN   60 // MUST KEEP A 5 MINUTES HISTORY.

struct HouseNetIOMetrics {
    char device[16];
    time_t timestamps[HOUSE_NETIO_SPAN];
    long long rxrate[HOUSE_NETIO_SPAN];
    long long txrate[HOUSE_NETIO_SPAN];
    long long previous[16];
};

static struct HouseNetIOMetrics *HouseNetIOLatest = 0;
static int                        HouseNetIOLatestSize = 0;
static int                        HouseNetIOLatestCount = 0;


static int houselinux_netio_find (const char *device) {
    int i;
    for (i = 0; i < HouseNetIOLatestCount; ++i) {
        if (!strcmp (HouseNetIOLatest[i].device, device)) return i;
    }
    return -1;
}

static int houselinux_netio_add (const char *device) {

    if (HouseNetIOLatestCount >= HouseNetIOLatestSize) {
        HouseNetIOLatestSize += 16;
        HouseNetIOLatest =
            realloc (HouseNetIOLatest,
                     HouseNetIOLatestSize*sizeof(struct HouseNetIOMetrics));
    }
    snprintf (HouseNetIOLatest[HouseNetIOLatestCount].device,
              sizeof(HouseNetIOLatest[0].device), "%s", device);

    return HouseNetIOLatestCount++;
}

static char *skipspace (char *line) {
    while (*line == ' ') line += 1;
    return line;
}

static char *skipvalue (char *line) {
    while (*line > ' ') line += 1;
    return skipspace (line);
}

void houselinux_netio_initialize (int argc, const char **argv) {

    // Allocate enough space for the net devices present on this machine
    // and set the initial "previous" values.

    char buffer[1024];
    FILE *f = fopen ("/proc/net/dev", "r");
    if (!f) return;

    // Ignore the first two lines (titles)
    //
    fgets (buffer, sizeof(buffer), f);
    fgets (buffer, sizeof(buffer), f);

    while (!feof (f)) {
        char *line = fgets (buffer, sizeof(buffer), f);
        if (!line) break;

        int i;
        char *device;
        long long value[16];

        line = skipspace (line);
        device = line;
        char *sep = strchr (line, ':');
        if (!sep) continue;
        *sep = 0;
        if (!strcmp (device, "lo")) continue; // Ignore the loopback.

        line = sep + 1;
        for (i = 0; i < 16; ++i) {
            line = skipvalue (line);
            value[i] = atoll (line);
        }

        int index = houselinux_netio_add (device);
        struct HouseNetIOMetrics *metrics = HouseNetIOLatest + index;
        memcpy (metrics->previous, value, sizeof(metrics->previous));
    }
    fclose (f);
}

int houselinux_netio_status (char *buffer, int size) {

    int i;
    int cursor = 0;
    int start = 0;
    int startdev = 0;
    const char *sep = "";

    cursor = snprintf (buffer, size, ",\"net\":{");
    if (cursor >= size) return 0;
    start = cursor;

    for (i = 0; i < HouseNetIOLatestCount; ++i) {
        startdev = cursor;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s\"%s\":",
                            sep, HouseNetIOLatest[i].device);
        if (cursor >= size) break;
        int startmetrics = cursor;

        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "rxrate",
                                          HouseNetIOLatest[i].rxrate,
                                          HOUSE_NETIO_SPAN, "KB/s");
        if (cursor >= size) break;

        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "txrate",
                                          HouseNetIOLatest[i].txrate,
                                          HOUSE_NETIO_SPAN, "KB/s");
        if (cursor >= size) break;

        if (cursor == startmetrics) {
            cursor = startdev; // No data to report for this device.
            continue;
        }
        buffer[startmetrics] = '{'; // Overwrite the ','.
        cursor += snprintf (buffer+cursor, size-cursor, "}");
        sep = ",";
    }
    if (cursor == start) return 0; // No data to report for any device.
    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) return 0;
    return cursor;
}

int houselinux_netio_details (char *buffer, int size,
                              time_t now, time_t since) {

    int i;
    int cursor = 0;
    int start = 0;
    int startdev = 0;
    const char *sep = "";

    cursor = snprintf (buffer, size, ",\"net\":{");
    if (cursor >= size) return 0;
    start = cursor;

    for (i = 0; i < HouseNetIOLatestCount; ++i) {
        startdev = cursor;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s\"%s\":",
                            sep, HouseNetIOLatest[i].device);
        if (cursor >= size) break;
        int startmetrics = cursor;

        cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                         "rxrate", "KB/s", now,
                         HOUSE_NETIO_PERIOD, HOUSE_NETIO_SPAN,
                         HouseNetIOLatest[i].timestamps,
                         HouseNetIOLatest[i].rxrate);

        cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                         "txrate", "KB/s", now,
                         HOUSE_NETIO_PERIOD, HOUSE_NETIO_SPAN,
                         HouseNetIOLatest[i].timestamps,
                         HouseNetIOLatest[i].txrate);

        if (cursor == startmetrics) {
            cursor = startdev; // No data to report for this device.
            continue;
        }
        buffer[startmetrics] = '{'; // Overwrite the ','.
        cursor += snprintf (buffer+cursor, size-cursor, "}");
        sep = ",";
    }
    if (cursor == start) return 0; // No data to report for any device.
    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) return 0;
    return cursor;
}

static void houselinux_netio_stat (struct HouseNetIOMetrics *latest,
                                   int index, time_t now) {

    char buffer[1024];
    FILE *f = fopen ("/proc/net/dev", "r");
    if (!f) return;

    // Ignore the first two lines (titles)
    //
    fgets (buffer, sizeof(buffer), f);
    fgets (buffer, sizeof(buffer), f);

    while (!feof (f)) {
        char *line = fgets (buffer, sizeof(buffer), f);
        if (!line) break;

        int i;

        line = skipspace (line);
        char *device = line;
        char *sep = strchr (line, ':');
        if (!sep) continue;
        *sep = 0;

        int devindex = houselinux_netio_find (device);
        if (devindex < 0) continue;

        long long value[16];
        struct HouseNetIOMetrics *metrics = latest + devindex;

        line = sep + 1;
        for (i = 0; i < 9; ++i) { // WE DOE NOT USE ITEMS BEYOND 8 FOR NOW.
            line = skipvalue (line);
            value[i] = atoll (line);
        }

        // The values collected are:
        //  0: Received bytes
		//  1: Received packets
		//  2: Receive errors
		//  3: Dropped receive packets
		//  4: Receive fifo (receive queue?)
		//  5: Frame (?)
		//  6: Receive compressed (?)
		//  7: Multicast.
		//  8: Transmitted bytes
		//  9: Transmitted packets
        // 10: Transmit errors.
        // 11: Dropped transmit packets
        // 12: Transmit fifo (transmit queue?)
        // 13: Transmit collisions
        // 14: Carrier (failures?)
        // 15: Transmit compressed(?)

        long long count = value[0] - metrics->previous[0];
        metrics->rxrate[index] = (count / 1024) / HOUSE_NETIO_PERIOD;

        count = value[8] - metrics->previous[8];
        metrics->txrate[index] = (count / 1024) / HOUSE_NETIO_PERIOD;

        metrics->timestamps[index] = now;

        // Keep a baseline for next time.
        memcpy (metrics->previous, value, sizeof(metrics->previous));
    }
    fclose (f);
}

void houselinux_netio_background (time_t now) {

    static time_t NextNetIOCollect = 0;

    if (now < NextNetIOCollect) return;
    int firsttime = (NextNetIOCollect == 0);
    NextNetIOCollect = now + HOUSE_NETIO_PERIOD;

    if (!firsttime) {
        int index = (now / HOUSE_NETIO_PERIOD) % HOUSE_NETIO_SPAN;
        houselinux_netio_stat (HouseNetIOLatest, index, now);
    }
}


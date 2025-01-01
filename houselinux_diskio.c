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
 * houselinux_diskio.c - Collect metrics on the Linux disk IO performances.
 *
 * SYNOPSYS:
 *
 * void houselinux_diskio_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void houselinux_diskio_background (time_t now);
 *
 *    The periodic function that manages the collect of metrics.
 *
 * int houselinux_diskio_status (char *buffer, int size);
 *
 *    A function that populates a status overview of the disk IO in JSON.
 *
 * int houselinux_diskio_details (char *buffer, int size, time_t now, time_t since);
 *
 *    A function that populates a full detail report of the disk IO in JSON.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include "houselog.h"
#include "houselinux_reduce.h"
#include "houselinux_diskio.h"

#define HOUSE_DISKIO_PERIOD  5 // Sample CPU metrics every 5 seconds.
#define HOUSE_DISKIO_SPAN   60 // MUST KEEP A 5 MINUTES HISTORY.

struct HouseDiskIOMetrics {
    int major;
    int minor;
    char device[16];
    time_t timestamps[HOUSE_DISKIO_SPAN];
    long long rdrate[HOUSE_DISKIO_SPAN];
    long long wrrate[HOUSE_DISKIO_SPAN];
    long long rdwait[HOUSE_DISKIO_SPAN];
    long long wrwait[HOUSE_DISKIO_SPAN];
    long long previous[17];
};

static struct HouseDiskIOMetrics *HouseDiskIOLatest = 0;
static int                        HouseDiskIOLatestSize = 0;
static int                        HouseDiskIOLatestCount = 0;


static int houselinux_diskio_find (int minor, int major) {
    int i;
    for (i = 0; i < HouseDiskIOLatestCount; ++i) {
        if ((HouseDiskIOLatest[i].minor == minor) &&
            (HouseDiskIOLatest[i].major == major)) return i;
    }
    return -1;
}

static int houselinux_diskio_add (int minor, int major, const char *device) {

    if (HouseDiskIOLatestCount >= HouseDiskIOLatestSize) {
        HouseDiskIOLatestSize += 16;
        HouseDiskIOLatest =
            realloc (HouseDiskIOLatest,
                     HouseDiskIOLatestSize*sizeof(struct HouseDiskIOMetrics));
    }
    HouseDiskIOLatest[HouseDiskIOLatestCount].major = major;
    HouseDiskIOLatest[HouseDiskIOLatestCount].minor = minor;
    snprintf (HouseDiskIOLatest[HouseDiskIOLatestCount].device,
              sizeof(HouseDiskIOLatest[0].device), "%s", device);

    return HouseDiskIOLatestCount++;
}

static char *skipspace (char *line) {
    while (*line == ' ') line += 1;
    return line;
}

static char *skipvalue (char *line) {
    while (*line > ' ') line += 1;
    return skipspace (line);
}

void houselinux_diskio_initialize (int argc, const char **argv) {

    // Allocate enough space for the disk devices present on this machine
    // and set the initial "previous" values.

    char buffer[1024];
    FILE *f = fopen ("/proc/diskstats", "r");
    if (!f) return;

    while (!feof (f)) {
        char *line = fgets (buffer, sizeof(buffer), f);
        if (!line) break;

        int i;
        int major;
        int minor;
        char *device;
        long long value[17];

        line = skipspace (line);
        major = atoi (line);
        line = skipvalue (line);
        minor = atoi (line);
        line = skipvalue (line);
        device = line;
        for (i = 0; i < 17; ++i) {
            line = skipvalue (line);
            value[i] = atoll (line);
        }

        // Backtrack and terminate the device string.
        line = device;
        while (*(++line) > ' ') ;
        *line = 0;

        // Ignore partitions and loops.
        if (isdigit(device[strlen(device)-1])) continue;

        int index = houselinux_diskio_add (major, minor, device);
        struct HouseDiskIOMetrics *metrics = HouseDiskIOLatest + index;
        memcpy (metrics->previous, value, sizeof(metrics->previous));
    }
    fclose (f);
}

int houselinux_diskio_status (char *buffer, int size) {

    int i;
    int cursor = 0;
    int start = 0;
    int startdev = 0;
    const char *sep = "";

    cursor = snprintf (buffer, size, ",\"disk\":{");
    if (cursor >= size) return 0;
    start = cursor;

    for (i = 0; i < HouseDiskIOLatestCount; ++i) {
        startdev = cursor;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s\"%s\":",
                            sep, HouseDiskIOLatest[i].device);
        if (cursor >= size) break;
        int startmetrics = cursor;

        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "rdrate",
                                          HouseDiskIOLatest[i].rdrate,
                                          HOUSE_DISKIO_SPAN, "r/s");
        if (cursor >= size) break;

        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "rdwait",
                                          HouseDiskIOLatest[i].rdwait,
                                          HOUSE_DISKIO_SPAN, "ms");
        if (cursor >= size) break;

        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "wrrate",
                                          HouseDiskIOLatest[i].wrrate,
                                          HOUSE_DISKIO_SPAN, "w/s");
        if (cursor >= size) break;

        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "wrwait",
                                          HouseDiskIOLatest[i].wrwait,
                                          HOUSE_DISKIO_SPAN, "ms");
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

int houselinux_diskio_details (char *buffer, int size, time_t now, time_t since) {

    int i;
    int cursor = 0;
    int start = 0;
    int startdev = 0;
    const char *sep = "";

    cursor = snprintf (buffer, size, ",\"disk\":{");
    if (cursor >= size) return 0;
    start = cursor;

    for (i = 0; i < HouseDiskIOLatestCount; ++i) {
        startdev = cursor;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s\"%s\":",
                            sep, HouseDiskIOLatest[i].device);
        if (cursor >= size) break;
        int startmetrics = cursor;

        cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                                        "rdrate", "r/s", now,
                                        HOUSE_DISKIO_PERIOD, HOUSE_DISKIO_SPAN,
                                        HouseDiskIOLatest[i].timestamps,
                                        HouseDiskIOLatest[i].rdrate);
        if (cursor >= size) break;

        cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                                        "rdwait", "ms", now,
                                        HOUSE_DISKIO_PERIOD, HOUSE_DISKIO_SPAN,
                                        HouseDiskIOLatest[i].timestamps,
                                        HouseDiskIOLatest[i].rdwait);
        if (cursor >= size) break;

        cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                                        "wrrate", "w/s", now,
                                        HOUSE_DISKIO_PERIOD, HOUSE_DISKIO_SPAN,
                                        HouseDiskIOLatest[i].timestamps,
                                        HouseDiskIOLatest[i].wrrate);
        if (cursor >= size) break;

        cursor += houselinux_reduce_details_json (buffer+cursor, size-cursor, since,
                                        "wrwait", "ms", now,
                                        HOUSE_DISKIO_PERIOD, HOUSE_DISKIO_SPAN,
                                        HouseDiskIOLatest[i].timestamps,
                                        HouseDiskIOLatest[i].wrwait);
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

static void houselinux_diskio_stat (struct HouseDiskIOMetrics *latest,
                                    int index, time_t now) {

    char buffer[1024];
    FILE *f = fopen ("/proc/diskstats", "r");
    if (!f) return;

    while (!feof (f)) {
        char *line = fgets (buffer, sizeof(buffer), f);
        if (!line) break;

        int i;
        int major;
        int minor;

        line = skipspace (line);
        major = atoi (line);
        line = skipvalue (line);
        minor = atoi (line);

        int devindex = houselinux_diskio_find (major, minor);
        if (devindex < 0) continue;

        long long value[17];
        struct HouseDiskIOMetrics *metrics = latest + devindex;

        line = skipvalue (line); // ignore the device name, a constant.
        for (i = 0; i < 8; ++i) { // WE DOE NOT USE ITEMS BEYOND 7 FOR NOW.
            line = skipvalue (line);
            value[i] = atoll (line);
        }

        // The values collected are:
        //  0: reads completed successfully
		//  1: reads merged
		//  2: sectors read
		//  3: time spent reading (ms)
		//  4: writes completed
		//  5: writes merged
		//  6: sectors written
		//  7: time spent writing (ms)             -- LAST ITEM DECODED
		//  8: I/Os currently in progress
		//  9: time spent doing I/Os (ms)
        // 10: weighted time spent doing I/Os (ms)
        // 11: discards completed successfully
        // 12: discards merged
        // 13: sectors discarded
        // 14: time spent discarding
        // 15: flush requests completed successfully
        // 16: time spent flushing

        long long count = value[0] - metrics->previous[0];
        metrics->rdrate[index] = count / HOUSE_DISKIO_PERIOD;

        long long wait = value[3] - metrics->previous[3];
        if (count <= 0)
            metrics->rdwait[index] = 0;
        else
            metrics->rdwait[index] = wait / count;

        count = value[4] - metrics->previous[4];
        metrics->wrrate[index] = count / HOUSE_DISKIO_PERIOD;

        wait = value[7] - metrics->previous[7];
        if (count <= 0)
            metrics->wrwait[index] = 0;
        else
            metrics->wrwait[index] = wait / count;

        metrics->timestamps[index] = now;

        // Keep a baseline for next time.
        memcpy (metrics->previous, value, sizeof(metrics->previous));
    }
    fclose (f);
}

void houselinux_diskio_background (time_t now) {

    static time_t NextDiskIOCollect = 0;

    if (now < NextDiskIOCollect) return;
    int firsttime = (NextDiskIOCollect == 0);
    NextDiskIOCollect = now + HOUSE_DISKIO_PERIOD;

    if (!firsttime) {
        int index = (now / HOUSE_DISKIO_PERIOD) % HOUSE_DISKIO_SPAN;
        houselinux_diskio_stat (HouseDiskIOLatest, index, now);
    }
}


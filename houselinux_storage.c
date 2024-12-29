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
 * houselinux_storage.c - Collect metrics on the Linux storage resources.
 *
 * SYNOPSYS:
 *
 * void houselinux_storage_initialize (int argc, const char **argv);
 *
 *    Initialize this module.
 *
 * void houselinux_storage_background (time_t now);
 *
 *    The periodic function that manages the metrics collection.
 *
 * int houselinux_storage_status (char *buffer, int size);
 *
 *    A function that populates a status overview of the storage in JSON.
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

#include "houselog.h"
#include "houselinux_reduce.h"
#include "houselinux_storage.h"

#define DEBUG if (echttp_isdebug()) printf

#define HOUSE_MOUNT_MAX    32
#define HOUSE_MOUNT_PERIOD 60 // Collect storage metrics every minute.
#define HOUSE_MOUNT_SPAN    5 // Keep a 5 minutes history.


struct HouseMountMetrics {
    time_t timestamp[HOUSE_MOUNT_SPAN];
    long long size;
    long long free[HOUSE_MOUNT_SPAN];
};

struct HouseMountPoint {
    time_t detected;
    char *dev;
    char *mount;
    char *fs;
    struct HouseMountMetrics metrics;
};

static struct HouseMountPoint HouseMountPoints[HOUSE_MOUNT_MAX];

void houselinux_storage_initialize (int argc, const char **argv) {
    // TBD
}

// Calculate storage space information (total, free, %used).
// Using the statvfs data is tricky because there are two different units:
// fragments and blocks, which can have different sizes. This code strictly
// follows the documentation in "man statvfs".
// The problem is compounded by these sizes being the same value for ext4,
// making it difficult to notice mistakes..
// Note that this code considers f_bavail and not f_bfree: this is because
// the intent is to show how much is left for regular applications usage.
// The reserve for privileged users is not something that one expects using.
//
static long long houselinux_storage_free (const struct statvfs *fs) {
    return (long long)(fs->f_bavail) * fs->f_bsize;
}

static long long houselinux_storage_total (const struct statvfs *fs) {
    return (long long)(fs->f_blocks) * fs->f_frsize;
}

int houselinux_storage_status (char *buffer, int size) {

    int cursor;
    int saved = 0; // On buffer overflow stop at the last complete volume.
    const char *sep = "";

    cursor = snprintf (buffer, size, "%s", ",\"storage\":{");
    if (cursor >= size) return 0;

    saved = cursor;
    int v;
    for (v = 0; v < HOUSE_MOUNT_MAX; ++v) {

        if (! HouseMountPoints[v].detected) continue;

        struct HouseMountMetrics *metrics = &(HouseMountPoints[v].metrics);

        if (metrics->size == 0) continue; // Ignore pseudo FS without storage.

        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s\"%s\":{\"size\":[%lld,\"MB\"]",
                            sep, HouseMountPoints[v].mount, metrics->size);
        if (cursor >= size) break;

        cursor += houselinux_reduce_json (buffer+cursor, size-cursor,
                                          "free",
                                          metrics->free,
                                          HOUSE_MOUNT_SPAN, "MB");

        cursor += snprintf (buffer+cursor, size-cursor, "}");
        if (cursor >= size) break;
        saved = cursor;
        sep = ",";
    }

    if (saved != cursor) houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    saved += snprintf (buffer+saved, size-saved, "}");
    return saved;
}

static int houselinux_storage_path_match (const char *value, const char *ref) {
    while ((*(ref++) == *(value++)) && (*ref > 0)) ;
    if ((*ref != 0) || ((*value != 0) && (*value != '/'))) return 0;
    return 1;
}

static void houselinux_storage_register_mount
               (time_t now, const char *dev, const char *mount, const char *fs) {
   int changed = 0;

   // We want to detect existing mount points and just update them.
   int i;
   int available = -1;
   for (i = HOUSE_MOUNT_MAX - 1; i >= 0; --i) {
       if (HouseMountPoints[i].detected <= 0)
           available = i;
       else if (!strcmp (HouseMountPoints[i].mount, mount)) break;
   }
   if (i >= 0) { // Update the existing mount point.
       if (strcmp (HouseMountPoints[i].dev, dev)) {
           free (HouseMountPoints[i].dev);
           HouseMountPoints[i].dev = strdup(dev);
           changed = 1;
       }
       if (strcmp (HouseMountPoints[i].fs , fs)) {
           free (HouseMountPoints[i].fs);
           HouseMountPoints[i].fs = strdup(fs);
           changed = 1;
       }
   } else { // New mount point.
       if (available < 0) return; // No more room.
       i = available;
       HouseMountPoints[i].dev = strdup(dev);
       HouseMountPoints[i].mount = strdup(mount);
       HouseMountPoints[i].fs = strdup(fs);
       changed = 1;
   }
   HouseMountPoints[i].detected = now;
   if (changed) {
       int j;
       for (j = HOUSE_MOUNT_SPAN-1; j >= 0; --j)
           HouseMountPoints[i].metrics.timestamp[j] = 0;
   }
}

static void houselinux_storage_prune_mount (time_t now) {

    int i;
    for (i = HOUSE_MOUNT_MAX - 1; i >= 0; --i) {
        if (HouseMountPoints[i].detected <= 0) continue; // Unused.
        if (HouseMountPoints[i].detected < now) {
            HouseMountPoints[i].detected = 0;
            if (HouseMountPoints[i].dev) {
                free (HouseMountPoints[i].dev);
                HouseMountPoints[i].dev = 0;
            }
            if (HouseMountPoints[i].mount) {
                free (HouseMountPoints[i].mount);
                HouseMountPoints[i].mount = 0;
            }
            if (HouseMountPoints[i].fs) {
                free (HouseMountPoints[i].fs);
                HouseMountPoints[i].fs = 0;
            }
        }
    }
}

static void houselinux_storage_enumerate (time_t now) {

    static time_t NextStorageEnumerate = 0;

    if (now < NextStorageEnumerate) return;
    NextStorageEnumerate = now + 60;

    FILE *f = fopen ("/proc/self/mountinfo", "r");
    if (!f) return;

    while (!feof(f)) {
        char line[1024];
        if (! fgets (line, sizeof(line), f)) break;
        char *item[256]; // Handle down to 3 characters average per item.
        int i = strlen(line);
        int j = 256;
        int argc = j;
        // eliminate all spaces at the end of the line.
        while ((i >= 0) && isspace(line[i])) line[i--] = 0;

        // Split the line by scanning it in reverse.
        for (; i > 0; --i) {
            if (isspace(line[i])) {
                if (--j < 0) break; // Should never happen.
                item[j] = line + i + 1;
                // eliminate all spaces between two items.
                while (isspace(line[i])) line[i--] = 0;
            }
        }
        if ((j < 1) || (j >= 256)) continue; // Bad format? Skip this line.
        item[--j] = line; // First item.

        char **argv = item + j;
        argc -= j;

        char *mount = argv[4];

        // Ignore some items that are not really storage.
        if (houselinux_storage_path_match (mount, "/sys")) continue;
        if (houselinux_storage_path_match (mount, "/proc")) continue;
        if (houselinux_storage_path_match (mount, "/run")) continue;
        if (houselinux_storage_path_match (mount, "/dev")) {
            if (strcmp (mount, "/dev/shm")) continue; // /dev/shm is OK.
        }

        for (i = 5; i < argc; ++i) {
            if (!strcmp(argv[i], "-")) break;
        }
        if (i >= argc-2) continue; // Skip this line.
        char *fs = argv[i+1];
        char *dev = argv[i+2];

        houselinux_storage_register_mount (now, dev, mount, fs);
    }
    fclose(f);
    houselinux_storage_prune_mount (now);
}

void houselinux_storage_background (time_t now) {

    static time_t NextCollect = 0;

    if (now < NextCollect) return;
    NextCollect = now + HOUSE_MOUNT_PERIOD;

    houselinux_storage_enumerate (now);

    int index = (now / HOUSE_MOUNT_PERIOD) % HOUSE_MOUNT_SPAN;

    int i;
    for (i = HOUSE_MOUNT_MAX - 1; i >= 0; --i) {

        if (HouseMountPoints[i].detected <= 0) continue;

        struct statvfs storage;
        if (statvfs (HouseMountPoints[i].mount, &storage)) continue ;

        struct HouseMountMetrics *metrics = &(HouseMountPoints[i].metrics);
        metrics->size = houselinux_storage_total (&storage) / (1024 * 1024);
        metrics->free[index] = houselinux_storage_free (&storage) / (1024 * 1024);
        metrics->timestamp[index] = now;
    }
}


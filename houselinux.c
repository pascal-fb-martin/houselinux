/* HouseLinux - a web server to collect Linux metrics.
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
 * houselinux.c - Main loop of the HouseLinux program.
 *
 * SYNOPSYS:
 *
 * This program periodically collect metrics and send them to the House
 * log consolidation service.
 *
 * This service also implements a web UI, which is more intended for
 * troubleshooting and monitoring.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_static.h"

#include "houseportalclient.h"
#include "housediscover.h"
#include "houselog.h"
#include "houselog_storage.h"

#include "houselinux_cpu.h"
#include "houselinux_memory.h"
#include "houselinux_storage.h"
#include "houselinux_diskio.h"
#include "houselinux_netio.h"
#include "houselinux_temp.h"

static char HostName[256];

static time_t HouseStartTime = 0;

static int HouseMetricsStoreEnabled = 1;


// Return a short summary of current metrics.
// In order to reduce the size, all metrics are expressed in percentages.
// (This function is also called in the background, without a HTTP request.)
//
static const char *houselinux_summary (const char *method, const char *uri,
                                      const char *data, int length) {
    static char buffer[65537];
    int cursor;
    time_t now = time(0);

    cursor = snprintf (buffer, sizeof(buffer),
                       "{\"host\":\"%s\","
                           "\"timestamp\":%lld,\"metrics\":{\"period\":300",
                       HostName, (long long)now);

    cursor += houselinux_cpu_summary (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_memory_summary (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_storage_summary (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_diskio_summary (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_netio_summary (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_temp_summary (buffer+cursor, sizeof(buffer)-cursor);
    snprintf (buffer+cursor, sizeof(buffer)-cursor, "}}");
    if (uri) echttp_content_type_json ();
    return buffer;
}

// Return a compact report of current metrics.
// (This function is also called in the background, without a HTTP request.)
//
static const char *houselinux_status (const char *method, const char *uri,
                                      const char *data, int length) {
    static char buffer[65537];
    int cursor;
    time_t now = time(0);

    // Cache the most recent result for 10 seconds to avoid the cost
    // of recalculating when there are multiple clients.
    // This does not apply to periodic recalculation, as this is
    // the reference for recording metrics.
    static time_t generated = 0;
    if (uri && ((now - generated) < 10)) {
        echttp_content_type_json ();
        return buffer;
    }
    generated = now;

    cursor = snprintf (buffer, sizeof(buffer),
                       "{\"host\":\"%s\","
                           "\"timestamp\":%lld,\"metrics\":{\"period\":300",
                       HostName, (long long)now);

    cursor += houselinux_cpu_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_memory_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_storage_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_diskio_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_netio_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += houselinux_temp_status (buffer+cursor, sizeof(buffer)-cursor);
    snprintf (buffer+cursor, sizeof(buffer)-cursor, "}}");
    if (uri) echttp_content_type_json ();
    return buffer;
}

// Return the complete metrics, only on request.
//
static const char *houselinux_details (const char *method, const char *uri,
                                       const char *data, int length) {
    static char buffer[65537];
    const char *sincearg = echttp_parameter_get ("since");
    int c;
    time_t now = time(0);

    time_t since = 0;
    if (sincearg) {
        since = (time_t) atoll (sincearg);
        if (since <= HouseStartTime) since = 0; // Guardrail.
    }

    int sampleperiod = 300;
    time_t samplestart = now - sampleperiod;
    if (samplestart < HouseStartTime) {
        samplestart = HouseStartTime;
        sampleperiod = now - samplestart;
    }
    c = snprintf (buffer, sizeof(buffer),
                       "{\"host\":\"%s\",\"timestamp\":%lld,"
                          "\"Metrics\":{\"start\":%lld,\"period\":%d",
                       HostName, (long long)now,
                       (long long)samplestart, sampleperiod);

    c += houselinux_cpu_details (buffer+c, sizeof(buffer)-c, now, since);
    c += houselinux_memory_details (buffer+c, sizeof(buffer)-c, now, since);
    c += houselinux_storage_details (buffer+c, sizeof(buffer)-c, now, since);
    c += houselinux_diskio_details (buffer+c, sizeof(buffer)-c, now, since);
    c += houselinux_netio_details (buffer+c, sizeof(buffer)-c, now, since);
    c += houselinux_temp_details (buffer+c, sizeof(buffer)-c, now, since);
    snprintf (buffer+c, sizeof(buffer)-c, "}}");
    echttp_content_type_json ();
    return buffer;
}

static const char *houselinux_osrelease (void) {

    static char HouseOsRelease[128] = {0};
    static time_t NextRead = 0;

    // Don't read the file too frequently.
    time_t now = time(0);
    if (HouseOsRelease[0]) {
        if (now < NextRead) return HouseOsRelease;
    }
    NextRead = now + 60;

    FILE *f = fopen ("/etc/os-release", "r");
    if (!f) return HouseOsRelease;

    char buffer[512];
    while (!feof(f)) {
        char *line = fgets (buffer, sizeof(buffer), f);
        if (line[0] < ' ') continue;

        char *eq = strchr (line, '=');
        if (!eq) continue;
        *(eq++) = 0;
        if (strcmp (line, "PRETTY_NAME")) continue;

        char *eol = strchr (eq, '\n');
        if (eol) *eol = 0;

        if (*eq == '"') {
            eq += 1;
            char *end = strrchr (eq, '"');
            if (end) *end = 0;
        }
        if (strcmp (HouseOsRelease, eq)) {
            snprintf (HouseOsRelease, sizeof(HouseOsRelease), "%s", eq);
        }
        break; // Found all that was needed.
    }
    fclose (f);

    return HouseOsRelease;
}

#define MEGABYTE (1024 * 1024)
#define GIGABYTE (1024 * 1024 * 1024)

// Return more static information.
static const char *houselinux_info (const char *method, const char *uri,
                                    const char *data, int length) {

    static char buffer[65537];
    int cursor;
    char *sep = "";
    time_t now = time(0);

    cursor = snprintf (buffer, sizeof(buffer),
                       "{\"host\":\"%s\","
                           "\"timestamp\":%lld,\"info\":{",
                       HostName, (long long)now);

    struct utsname uts;
    const char *os = houselinux_osrelease();
    if (!os) os = uts.sysname;

    if (!uname (&uts)) {
        cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                            "\"arch\":\"%s\",\"os\":\"%s\",\"kernel\":\"%s\"",
                            uts.machine, os, uts.release);
        if (cursor >= sizeof(buffer)) return 0;
        sep = ",";
    }

    struct sysinfo info;
    if (!sysinfo(&info)) {
        const char *unit = "MB";
        long long totalram = ((long long)(info.totalram) * info.mem_unit);
        // Adjust the unit and round up.
        if (totalram > GIGABYTE) {
            totalram = (totalram + GIGABYTE - 1) / GIGABYTE;
            unit = "GB";
        } else {
            totalram = (totalram + MEGABYTE - 1) / MEGABYTE;
        }
        cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                            "%s\"ram\":{\"size\":%lld,\"unit\":\"%s\"},\"boot\":%lld",
                            sep, totalram, unit, (long long)now - info.uptime);
        if (cursor >= sizeof(buffer)) return 0;
        sep = ",";
    }

#ifdef _SC_NPROCESSORS_ONLN
    cursor += snprintf (buffer+cursor, sizeof(buffer)-cursor,
                        "%s\"cores\":%ld", sep, sysconf (_SC_NPROCESSORS_ONLN));
    if (cursor >= sizeof(buffer)) return 0;
    sep = ",";
#endif

    snprintf (buffer+cursor, sizeof(buffer)-cursor, "}}");
    echttp_content_type_json ();
    return buffer;
}

static void houselinux_background (int fd, int mode) {

    time_t now = time(0);

    houseportal_background (now);

    // Store metrics data as part of the historical log.
    // There is an option to not store because some systems are not
    // performance critical: we still want to monitor them live, but
    // adding more data to the metrics log would waste storage.
    //
    static time_t NextMetricsStore = 0;
    if (HouseMetricsStoreEnabled && (now >= NextMetricsStore)) {
        if (NextMetricsStore == 0) {
            // Synchronize recordings at the 5 minutes mark, so that
            // all machines submit their metrics in a synchronized fashion.
            // (Don't send a recording before having collected a full set.)
            NextMetricsStore = now - (now % 300) + 600;
        } else {
            NextMetricsStore += 300;
            const char *data = houselinux_status (0, 0, 0, 0);
            if (data) houselog_storage_flush ("metrics", data);
        }
    }

    houselinux_cpu_background(now);
    houselinux_memory_background(now);
    houselinux_storage_background(now);
    houselinux_diskio_background(now);
    houselinux_netio_background(now);
    houselinux_temp_background(now);

    housediscover (now);
    houselog_background (now);
}

static void houselinux_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    gethostname (HostName, sizeof(HostName));

    echttp_default ("-http-service=dynamic");
    echttp_static_default ("-http-root=/usr/local/share/house/public");

    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_present("-metrics-no-store", argv[i]))
            HouseMetricsStoreEnabled = 0;
    }
    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        static const char *path[] = {"metrics:/metrics"};
        houseportal_initialize (argc, argv);
        houseportal_declare (echttp_port(4), path, 1);
    }
    echttp_static_initialize (argc, argv);

    housediscover_initialize (argc, argv);
    houselog_initialize ("metrics", argc, argv);

    echttp_cors_allow_method("GET");
    echttp_protect (0, houselinux_protect);

    houselinux_cpu_initialize (argc, argv);
    houselinux_memory_initialize (argc, argv);
    houselinux_storage_initialize (argc, argv);
    houselinux_diskio_initialize (argc, argv);
    houselinux_netio_initialize (argc, argv);
    houselinux_temp_initialize (argc, argv);

    echttp_route_uri ("/metrics/summary", houselinux_summary);
    echttp_route_uri ("/metrics/status", houselinux_status);
    echttp_route_uri ("/metrics/info", houselinux_info);
    echttp_route_uri ("/metrics/details", houselinux_details);

    echttp_background (&houselinux_background);

    houselog_event ("SERVICE", "metrics", "START", "ON %s", HostName);
    HouseStartTime = time(0);
    echttp_loop();
}


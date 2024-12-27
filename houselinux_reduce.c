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
 * int houselinux_reduce_json (char *buffer, int size,
 *                             const char *name,
 *                             long long *values, int count, const char *unit);
 *
 *    Generate a compact JSON representation of the time series. Depending
 *    on the time series, the format of the output may be one of:
 *    ""                       (if all values are null)
 *    ",[value,unit]"          (if all values are equal)
 *    ",[min,max,unit]"        (if there are less than 10 values)
 *    ",[min,median,max,unit]" (if there are 10 or more values)
 *
 *    This returns the number of character stored in buffer.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "houselinux_reduce.h"

static long long *SortedMetrics = 0;
static int SortedMetricsSize = 0;

static int houselinux_reduce_compare (const void *p1, const void *p2) {
    long long diff = *(long long *)p1 - *(long long *)p2;
    if (diff != 0) return (diff < 0)? -1 : 1;
    return 0;
}

int houselinux_reduce_json (char *buffer, int size,
                            const char *name,
                            long long *values, int count, const char *unit) {

    int last = count - 1;
    int cursor;

    // Make a copy to avoid corrupting the current metrics.
    if (count > SortedMetricsSize) {
        SortedMetrics = realloc (SortedMetrics, sizeof(long long) * count);
        SortedMetricsSize = count;
    }
    int i;
    for (i = last; i >= 0; --i) SortedMetrics[i] = values[i];

    qsort (SortedMetrics, count, sizeof(long long), houselinux_reduce_compare);

    if (SortedMetrics[0] == SortedMetrics[last]) {

        if (SortedMetrics[0] == 0) {
            buffer[0] = 0;
            cursor = 0;
        } else {
            cursor = snprintf (buffer, size, ",\"%s\":[%lld,\"%s\"]",
                               name, SortedMetrics[0], unit);
        }

    } else if (count >= 10) {

        long long median;
        if (count & 1) {
            median = SortedMetrics[count/2];
        } else {
            int half = count / 2;
            median = (SortedMetrics[half-1] + SortedMetrics[half]) / 2;
        }
        cursor = snprintf (buffer, size, ",\"%s\":[%lld,%lld,%lld,\"%s\"]",
                           name,
                           SortedMetrics[0],    // min.
                           median,
                           SortedMetrics[last], // max.
                           unit);

    } else {

        cursor = snprintf (buffer, size, ",\"%s\":[%lld,%lld,\"%s\"]",
                           name,
                           SortedMetrics[0],    // min.
                           SortedMetrics[last], // max.
                           unit);
    }
    // The SortedMetrics array will reused later, don't free.
    if (cursor >= size) return 0;
    return cursor;
}


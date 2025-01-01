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
 * houselinux_netio.h - Collect metrics on the Linux network IO performances.
 */
void houselinux_netio_initialize (int argc, const char **argv);
void houselinux_netio_background (time_t now);
int houselinux_netio_status (char *buffer, int size);
int houselinux_netio_details (char *buffer, int size, time_t now, time_t since);


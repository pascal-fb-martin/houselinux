# HouseLinux - A web server to collect Linux metrics
#
# Copyright 2024, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=houselinux

# Application build. --------------------------------------------

OBJS= houselinux_storage.o \
      houselinux_memory.o \
      houselinux_cpu.o \
      houselinux_diskio.o \
      houselinux_netio.o \
      houselinux_temp.o \
      houselinux_reduce.o \
      houselinux.o
LIBOJS=

all: houselinux

clean:
	rm -f *.o *.a houselinux

rebuild: clean all

%.o: %.c
	gcc -c -Wall -g -O -o $@ $<

houselinux: $(OBJS)
	gcc -g -O -o houselinux $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

# Distribution agnostic file installation -----------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/metrics
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/metrics

install-app: install-ui
	$(INSTALL) -m 0755 -s houselinux $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/houselinux

uninstall-app:
	rm -f $(DESTDIR)$(prefix)/bin/houselinux
	rm -f $(DESTDIR)$(SHARE)/public/metrics

purge-app:

purge-config:
	rm -rf $(DESTDIR)/etc/house/linux.config
	rm -rf $(DESTDIR)/etc/default/houselinux

# System installation. ------------------------------------------

include $(SHARE)/install.mak


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

HAPP=houselinux
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Application build. --------------------------------------------

OBJS= houselinux_storage.o houselinux_memory.o houselinux.o
LIBOJS=

all: houselinux

clean:
	rm -f *.o *.a houselinux

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

houselinux: $(OBJS)
	gcc -g -O -o houselinux $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

# Distribution agnostic file installation -----------------------

install-ui:
	mkdir -p $(SHARE)/public/metrics
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/metrics
	cp public/* $(SHARE)/public/metrics
	chown root:root $(SHARE)/public/metrics/*
	chmod 644 $(SHARE)/public/metrics/*

install-app: install-ui
	mkdir -p $(HROOT)/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/houselinux
	cp houselinux $(HROOT)/bin
	chown root:root $(HROOT)/bin/houselinux
	chmod 755 $(HROOT)/bin/houselinux
	touch /etc/default/houselinux

uninstall-app:
	rm -f $(HROOT)/bin/houselinux
	rm -f $(SHARE)/public/metrics

purge-app:

purge-config:
	rm -rf /etc/house/linux.config /etc/default/houselinux

# System installation. ------------------------------------------

include $(SHARE)/install.mak


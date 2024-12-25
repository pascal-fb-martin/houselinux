# HouseMotion
The house Motion sidekick service.

## Overview

This service aggregates activity and performance metrics from the local Linux system: RAM, storage and CPU usage. It implement the House metrics web API.

After trying multiple monitoring systems, it seems that there is no general metric recording solution compatible with small systems:
* Command line tools typically specialize in one area (CPU or IO). These typically are not optimized for constant recording. Running then continuously may tax the local system (CPU and storage), especially on small computers with microSD cards or EMMC storage.
* "Traditional" open source monitoring tools tend to focus on raising up/down alerts rather than collecting data for performance analysis.
* "Cloud generation" open source monitoring tools tend to be memory hogs (Prometheus).
* Proprietary tools target large accounts, and price accordingly.

The goal of Houselinux is to gather an extensive set of OS metrics, and report them to external monitoring and logging systems. The objective is to:
* Use little CPU.
* Have a small memory footprint.
* Generate compact data.

The later is reached by reporting percentile data at low frequency. The most basic percentile data is the set min and max. Slightly more sophisticated is the set min, median, max.

This service is not intended to have a user interface. The existing web pages are for maintenance and troubleshooting only.

## Installation

This service depends on the House series environment:
* Install git, icoutils, openssl (libssl-dev), motion.
* configure the motion software (see later).
* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* Clone this repository.
* make rebuild
* sudo make install

## Web API

```
GET /metrics/status
```
This endpoint returns a complete set of metrics, as a JSON object defined as follows:
* host: the name of the server running this service.
* proxy: the name of the server used for redirection (typically the same as host).
* timestamp: the time of the request/response.
* metrics.period: the sampling period used by this service. The client should poll periodically using this value.
* metrics.memory: all RAM-related metrics (see below)
* metrics.memory.ram: total amount of RAM the system can use.
* metrics.memory.available: amount of RAM currently available (i.e. not "used").
* metrics.memory.used: amount of RAM currently used.
* metrics.memory.dirty: amount of RAM currently dirty (queued to be saved to disk).
* metrics.memory.swap: size of the swap area (in storage).
* metrics.memory.swapped: amount of swap current used.
* metrics.storage: all storage related metrics. This is a JSON object where each item describe a volume (see below).
* metrics.storage._volume_.size: total size of the volume.
* metrics.storage._volume_.free: free space in this volume.
* metrics.cpu: all CPU related metrics (TBD).

an individual metric is an array of 2, 3 or 4 elements:
* If the array has 2 elements, the format is: value, unit ("GB", "MB", "%", etc.).
* If the array has 3 elements, the format is: min, max, unit.
* If the array has 4 elements, the format is: min, median, max, unit.

A metric normally reported with 3 or 4 elements may be reported with 2 elements when the min and max values are equal.

This status information is visible in the Status web page.

## Implementation

/proc/self/mountinfo is used to retrieve the mounted volumes, and statvfs() is used to get the usage information for each volume.

/proc/meminfo is used to retrieve the RAM usage.

/proc/stat and /proc/loadavg will be used to retrieve the CPU usage.


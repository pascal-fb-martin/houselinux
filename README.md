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

The later goal leads to a choice of reporting quantile data at low frequency. The most basic quantile data is the set min and max. The min and max values give you and idea of the value fluctuations, but no idea about how the sampled values are spread within the interval. A slightly less basic set is min, median, max. The addition of a median value provides an indication of the values being centered or overall biased toward the min or max. Adding quartiles would give an indication of the values being either concentrated around the median, the min or max, or else evenly spread. Each addition add to the quality of the information, but exponentially increases both the computational and storage cost. The plan is to adjust based on experience. A different quality level might be needed metric by metric.

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
* If the array has 2 elements, the format is: value, unit.
* If the array has 3 elements, the format is: min, max, unit.
* If the array has 4 elements, the format is: min, median, max, unit.

The unit is typically "GB", "MB", "TB", "%", etc. It can be null if the metrics has no unit, e.g. a counter. The null unit must be present but integer 0 is accepted as equivalent to null: "[12345,null]" and [12345,0]" are equivalent.

A metric normally reported with 3 or 4 elements may be reported with 2 elements when the min and max values are equal, or not reported at all if the min and max are both 0 (any missing metrics must be considered 0).

The unit used for a given metrics may change from machine to machine, and from time to time. It is legal to change the unit to make the data more compact. The precision of most metrics must be at least 3 digits (2 digits for percentages). Adjusting the unit may make the data more compact. For example, 10240 MB can be reported as 10.0 GB.

This status information is visible in the Status web page.

```
GET /metrics/info
```
This endpoint returns general information about the server hardware, OS, uptime, etc. They are separate from the metrics because they seldom change and are not performance related. The data is a JSON object, TBD.

## Implementation

The following is currently implemented:

* /proc/self/mountinfo is used to retrieve the mounted volumes, and statvfs() is used to get the usage information for each volume.

* /proc/meminfo is used to retrieve the RAM usage.

* Metrics are periodically pushed to all detected log services for permanent storage, in the same JSON format as returned by the /metrics/status endpoint.

The following is planned in the near future:

* /proc/stat and /proc/loadavg will be used to retrieve the CPU usage.

* /proc/diskstats will be used to retrieve disk IO metrics, especially latency.

* /proc/net/dev will be used to retrieve network IO traffic number, including error counts.

* /proc/version (kernel version), /etc/os-release (Linux distribution) and /proc/uptime will be used to collect general information about the server (see endpoint /metrics/info).


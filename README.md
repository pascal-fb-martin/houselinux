# HouseLinux
The House service to log Linux performance metrics.

## Overview

This service aggregates activity and performance metrics from the local Linux system: RAM, storage and CPU usage. It implement the House metrics web API.

The author was using Cockpit as a convenient way to access an overview of his 10 to 15 small computers (cheap Raspberyy Pi and used-market Mini PC systems) and was looking for a way to continuously record some performance metrics.

The Cockpit project apparently pivoted to a management-only focus and removed the overview part. (This monitoring portion was good for a home lab, but continuous monitoring would have been costly..).

After trying multiple monitoring systems, it seems that there is no general metric recording solution compatible with small systems:
* Command line tools typically specialize in one area (CPU or IO). These are usually not optimized for constant recording. Running them continuously may tax the local system (CPU and storage), especially on small computers with micro SD cards or EMMC storage.
* "Traditional" open source monitoring tools tend to focus on raising up/down alerts rather than collecting data for performance analysis. When they collect performance data, the advice is to limit the amount for performance reasons (see Nagios).
* "Cloud generation" open source monitoring tools tend to be memory hogs (see Prometheus).
* Proprietary tools target large accounts, and price accordingly.

It was time to get some fun with a simple more home-grown monitoring solution.

The goal of Houselinux is to gather an extensive set of OS metrics, and report them to external monitoring and logging systems. The objective is to:
* Use little CPU.
* Have a small memory footprint.
* Generate compact data.
* Log data 24/7 for later incident troubleshooting. The storage itself is handled by [housesaga](https://github.com/pascal-fb-martin/housesaga).

This service is not intended to have a user interface. The existing web pages are for maintenance and troubleshooting only. This is meant to collect metrics, and the visualization part will be done elsewhere, mostly.

The web API implemented by HouseLinux is meant to be mostly independent from Linux or Unix, even while some data (e.g. load average) might not exist on other OSes.

Visualization of real time data is done through [houseportal](https://github.com/pascal-fb-martin/houseportal). Visualization of historical data is done through [housesaga](https://github.com/pascal-fb-martin/housesaga).

## Warnings

HouseLinux is still fairly new. The list of metrics recorded may change significantly in the near future.

There is no access security. This tool is meant to be used on a local, secure, network only. That is why there is no mechanism for system administration, like Cockpit does.

## Installation

This service depends on the House series environment:
* Install git, icoutils, openssl (libssl-dev).
* configure the motion software (see later).
* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* It is recommended to install [housesaga](https://github.com/pascal-fb-martin/housesaga) somewhere on the local network, preferably on a file server (logs may become large, and constant write access might not be good for SD cards).
* Clone this repository.
* make rebuild
* sudo make install

## Data Design

The "compact" goal is meants to generate logs of a reasonable size for a home system. That did lead to a choice of reporting quantile data at low frequency.

The most basic quantile data is the set min and max. The min and max values give you and idea of the value fluctuations, but no idea about how the sampled values are spread within the interval.

A slightly less basic set is min, median, max. The addition of a median value provides an indication of the values being centered or overall biased toward the min or max.

Adding quartiles (min, 25% percentile, median, 75% percentile, max) would give an indication of the values being either concentrated around the median, the min or max, or else evenly spread. Each addition add to the quality of the information, but exponentially increases both the computational and storage cost. The plan is to adjust based on experience. A different quality level might be used metric by metric, depending on needs.

## Web API

```
GET /metrics/status
```
This endpoint returns a complete set of metrics, as a JSON object defined as follows:
* host: the name of the server running this service.
* timestamp: the time of the request/response.
* metrics.period: the sampling period used by this service. The client should poll periodically using this value.
* metrics.memory: all RAM-related metrics (see below)
* metrics.memory.size: total amount of RAM the system can use.
* metrics.memory.available: amount of RAM currently available (i.e. not "used").
* metrics.memory.dirty: amount of RAM currently dirty (queued to be saved to disk).
* metrics.memory.swap: size of the swap area (in storage).
* metrics.memory.swapped: amount of swap current used.
* metrics.storage: all storage related metrics. This is a JSON object where each item describe a volume (see below).
* metrics.storage._volume_.size: total size of the volume.
* metrics.storage._volume_.free: free space in this volume.
* metrics.cpu: all CPU related metrics (see below).
* metrics.cpu.busy: the total CPU busy time (user mode, system mode, interrupt, etc.)
* metrics.cpu.iowait: the idle time while waiting for an I/O, if available.
* metrics.cpu.steal: time slices stolen when running as a VM guest, if available.
* metrics.cpu.load: the 3 Unix load average values (1mn, 5mn, 15mn) multiplied by 100, with a null unit. Each load value is the latest value sampled (and can be up to a minute old). Not present if not available.
* metrics.disk: all disk I/O related metrics (see below).
* metrics.disk._device_.rdrate: read operation rate. (Might be replaced by a byte rate later.)
* metrics.disk._device_.rdwait: read operation latency.
* metrics.disk._device_.wrrate: write operation rate. (Might be replaced by a byte rate later.)
* metrics.disk._device_.wrwait: write operation latency.
* metrics.net: all network I/O related metrics (see below).
* metrics.net._device_.rxrate: receive traffic in KByte per second.
* metrics.net._device_.txrate: transmit traffic in KByte per second.

An individual metric is an array of 2, 3 or 4 elements, typically:
* If the array has 2 elements, the format is: value, unit.
* If the array has 3 elements, the format is: min, max, unit.
* If the array has 4 elements, the format is: min, median, max, unit.

(The load average is an exception, see description of metrics.cpu.load above.)

The unit is typically "GB", "MB", "TB", "%", etc. It can be null or empty if the metrics has no unit, e.g. the load average or a counter. The null unit must be present but integer 0 is accepted as equivalent to null: "[12345,null]" and [12345,0]" are equivalent.

A metric normally reported with 3 or 4 elements may be reported with 2 elements when the min and max values are equal, or not reported at all if the min and max are both 0 (any missing metrics must be considered 0).

The unit used for a given metrics may change from machine to machine, and from time to time. It is legal to change the unit to make the data more compact, for example report 200 GB instead of 204856 MB. The precision of most metrics should be at least 3 digits (2 digits is OK for percentages).

This status information is visible in the Status web page.

```
GET /metrics/details
GET /metrics/details?since=TIMESTAMP
```

This endpoint returns the same list of metrics as /metrics/status, but with full details, i.e. in time series instead of quantile summary form. The JSON object is named "Metrics" instead of "metrics", and contains two more items:
* Metrics.start: the time of the first recorded metrics in each series.
* Metrics.period: the time span covered by the metrics.

If the since parameter is included, any value collected before that time will be excluded from the report. The timestamp value is in UNIX system time format (an integer).

```
GET /metrics/info
```
This endpoint returns general information about the server hardware, OS, uptime, etc. They are separate from the metrics because they seldom change and are not performance related. The data is a JSON object, as follows:
* host: the name of the server running this service.
* timestamp: the time of the request/response.
* info.arch: the name of the processor architecture (not the processor model).
* info.os: the name of the OS (e.g. "Linux").
* info.version: the version of the OS kernel.
* info.cores: the number of active cores.
* info.boot: the time of the last boot.

This status information is visible in the Status web page.

## Implementation

The following is currently implemented:

* /proc/self/mountinfo is used to retrieve the mounted volumes, and statvfs() is used to get the usage information for each volume.

* /proc/meminfo is used to retrieve the RAM usage.

* /proc/stat is used to retrieve the CPU usage and /proc/loadavr is used to retrieve load averages.

* /proc/diskstats is used to retrieve disk IO metrics, especially latency (experimental).

* /proc/net/dev is used to retrieve network IO traffic number.

* Metrics are periodically pushed to all detected log services for permanent storage, in the same JSON format as returned by the /metrics/status endpoint.

* uname(2), sysinfo(2) and sysconf(2) are used to retrieve system information.


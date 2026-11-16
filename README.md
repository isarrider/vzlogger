vzlogger
=========
  * is a part of the volkszaehler.org smart meter
  * is a tool to read and log measurements of a wide variety of smart meters and sensors to the volkszaehler.org middleware
  * can run as a daemon or via cron
  * includes a tiny onboard http daemon to serve realtime readings
  * is written in C++ and should run on most embedded devices which conform to the POSIX standard

Feel free to implement support your own hardware ;)

About this fork
---------------

This is [isarrider/vzlogger](https://github.com/isarrider/vzlogger), a fork of [volkszaehler/vzlogger](https://github.com/volkszaehler/vzlogger). It follows upstream `master` and adds the changes below. This fork's `master` contains all of them, and the [prebuilt Docker image](#prebuilt-image) is built from it. Each change goes through a PR into this fork first, and fixes are then proposed upstream from a branch of their own that is based on upstream `master`. Once upstream merges a change, it moves to the list of merged changes below the table.

| Change | Configuration | In this fork | Upstream |
|---|---|---|---|
| **InfluxDB: no data loss during database outages.** Previously, readings were lost while InfluxDB was unreachable, and the data had a gap for the outage. Readings are now kept in a send queue and only removed once InfluxDB has accepted them, so the gap is filled in once the database is back. The oldest readings are dropped first when the queue is full. | `max_buffer_size` on the InfluxDB channel limits the queue | [#1](https://github.com/isarrider/vzlogger/pull/1) | pending: [#716](https://github.com/volkszaehler/vzlogger/pull/716), branch [`upstream/influxdb-keep-readings-on-outage`](https://github.com/isarrider/vzlogger/tree/upstream/influxdb-keep-readings-on-outage) |
| **InfluxDB: outages are visible in the log.** A failed insert logs how many readings are still queued, and the first successful insert afterwards logs `InfluxDB insert succeeded again, sent N queued readings, M still queued`. Both are warnings, so the start and end of an outage show up at `verbosity` 3 or higher. | – | [#2](https://github.com/isarrider/vzlogger/pull/2) | pending: part of [#716](https://github.com/volkszaehler/vzlogger/pull/716) |
| **SML: raw frames with a bad CRC can be logged.** libsml drops SML messages with a CRC mismatch and only logs `libsml: sml_message_parse(): crc mismatch, dropping message`, so there's nothing left to analyse. With this option, vzlogger checks the CRC of each whole frame and, on a mismatch, logs the raw frame as hex (32 bytes per line) at warning level. A temporary workaround until libsml can report CRC errors itself ([libsml#147](https://github.com/volkszaehler/libsml/issues/147)), see [below](#sml-dumping-frames-with-a-bad-crc). | `"dump_crc_errors": true` on an SML meter (default `false`), needs `verbosity` 3 or higher | [#6](https://github.com/isarrider/vzlogger/pull/6), [#9](https://github.com/isarrider/vzlogger/pull/9) | pending: [#719](https://github.com/volkszaehler/vzlogger/pull/719), branch [`upstream/sml-dump-crc-errors`](https://github.com/isarrider/vzlogger/tree/upstream/sml-dump-crc-errors) |
| **Docker: builder on `alpine:latest` again.** The builder stage was pinned to `alpine:3.20` to get the build working. With current Alpine (GCC 15.2, CMake 4.2) it builds again on all five platforms, and the builder and runtime stage use the same Alpine release again, so the binary runs against the library versions it was built with. | – | [#10](https://github.com/isarrider/vzlogger/pull/10) | pending: [#718](https://github.com/volkszaehler/vzlogger/pull/718), branch [`fix/dockerfile-alpine-latest`](https://github.com/isarrider/vzlogger/tree/fix/dockerfile-alpine-latest) |
| **Prebuilt multi-arch Docker image.** GitHub Actions builds the image for amd64, arm64, arm/v7, arm/v6 and riscv64 and publishes it as `ghcr.io/isarrider/vzlogger`, see [Docker](#docker) below. | – | [#4](https://github.com/isarrider/vzlogger/pull/4) | fork only |

Merged upstream:

- **OBIS identifiers in the local HTTP server's JSON output.** Each channel in the JSON of the built-in HTTP server (`local`) now has an `obis` field with its identifier, e.g. `"obis": "1-0:1.8.0"`, so clients don't have to map UUIDs to meter values themselves. [#713](https://github.com/volkszaehler/vzlogger/pull/713) (June 2026, in 0.8.10).
- **Docker: parallel build.** The Docker build runs `make` with one job per CPU core, which makes building the image much faster on multi-core machines. [#574](https://github.com/volkszaehler/vzlogger/pull/574) (February 2023).
- **Docker: smaller image.** The Dockerfile used a `buster-slim` base image and no longer declared a volume. [#517](https://github.com/volkszaehler/vzlogger/pull/517) (April 2022). The image has since moved to Alpine.

Superseded upstream PRs, closed without being merged:

- [#711](https://github.com/volkszaehler/vzlogger/pull/711) and [#712](https://github.com/volkszaehler/vzlogger/pull/712): earlier versions of the OBIS identifiers change, replaced by [#713](https://github.com/volkszaehler/vzlogger/pull/713).
- [#703](https://github.com/volkszaehler/vzlogger/pull/703): logged how many readings were still undelivered in the InfluxDB buffer, replaced by [#716](https://github.com/volkszaehler/vzlogger/pull/716), which keeps the readings queued during an outage and logs when the outage starts and ends.

### SML: dumping frames with a bad CRC

    "meters": [{
        "protocol": "sml",
        "device": "/dev/ttyUSB0",
        "dump_crc_errors": true,
        ...
    }]

The dump shows what was corrupted: random single bits point to the optical link (head alignment, ambient light), missing or repeated bytes point to the serial side or a second process reading the same device. To pull the dumps out of the Docker log:

    docker logs vzlogger 2>&1 | grep -A20 "transport CRC mismatch"

A few CRC errors are normal on an optical head. At one telegram per second, a rate around 0.1–0.2 % means one dropped telegram every few minutes, which doesn't affect the meter counters.

Installation
---------------
To install, follow the detailed installation instructions at http://wiki.volkszaehler.org/software/controller/vzlogger/installation_cpp-version

If you're impatient you can quickstart using (Debian Bookworm or Ubuntu 24.04 LTS):

    sudo apt-get install extrace git-core cmake libtool dh-autoreconf uuid-dev libmicrohttpd-dev \
        build-essential pkg-config libgnutls28-dev libssl-dev libmosquitto-dev libgcrypt20-dev libcurl3-dev \
    	libunistring-dev libssl-dev libsasl2-dev libcurl4-openssl-dev uuid-dev libtool libltdl-dev

Then run the installation:

    wget https://raw.github.com/volkszaehler/vzlogger/master/install.sh
    sudo bash install.sh
    
Docker
------

### Prebuilt image

GitHub Actions builds the image for amd64, arm64, arm/v7, arm/v6 (e.g. Raspberry Pi with a 64-bit or 32-bit OS) and riscv64 and publishes it to the GitHub Container Registry, so you don't have to build it yourself. Docker picks the right architecture automatically:

     docker pull ghcr.io/isarrider/vzlogger:latest

| Tag | Built from |
|---|---|
| `latest` | the newest commit on `master` (also rebuilt weekly for base image updates) |
| `sha-<commit>` | a specific commit on `master` |
| `1.2.3`, `1.2`, `1` | release tags `v1.2.3` |

To use it, replace `vzlogger` at the end of the `docker run` command below with `ghcr.io/isarrider/vzlogger:latest`.

### Building it yourself

You can also build a docker image:

     docker build -t vzlogger .
     
Note, that this will use the newest vzlogger from volkszaehler github (not your local clone).
You can start it:

     docker run --restart=always -v /home/pi/projects/vzlogger-docker:/etc \
     --device=/dev/serial/by-id/usb-FTDI_FT230X_Basic_UART_D30A9U5N-if00-port0 \
     --name vzlogger -d vzlogger

where /home/pi/projects/vzlogger-docker is the path to the directory containing the vzlogger.conf file and
/dev/serial/by-id/usb-FTDI_FT230X_Basic_UART_D30A8U6N-if00-port0 is your device. You can pass several devices if you have them.

Debian and Raspberry Pi OS Packages
-------------

[![Hosted By: Cloudsmith](https://img.shields.io/badge/OSS%20hosting%20by-cloudsmith-blue?logo=cloudsmith&style=flat-square)](https://cloudsmith.com)

We now build Debian packages for amd64, armhf and arm64. Armel, which would be
needed for Debian on RPi 1 hardware, is not supported.

[Raspberry Pi OS packages for armhf](
https://cloudsmith.io/~volkszaehler/repos/volkszaehler-org-project/packages/?q=distribution%3Araspbian+AND+architecture%3Aarmhf) 
are also part of our releases. Unfortunately 
Debian armhf packages do not run on Raspberry Pi 1 although the architecture 
has been named armhf in Raspbian. Using "Raspbian armhf" packages fixes that.
For RPi 2 and above Debian packages run on Raspberry Pi OS. 

Our packages are built with MQTT support. The package that is in Debian is 
built without OMS support while starting with 0.8.7 ours is. 

The packages attached to the release are meant for Debian forky. The full set
of packages is provided through a repository graciously provided by 
[Cloudsmith](https://cloudsmith.com).

The setup of the repository is also 
[explained by Cloudsmith](https://cloudsmith.io/~volkszaehler/repos/volkszaehler-org-project/setup/#formats-deb).
The easy way to do it is running 
```
curl -1sLf \
  'https://dl.cloudsmith.io/public/volkszaehler/volkszaehler-org-project/setup.deb.sh' \
  | sudo -E bash
```
If you do it the easy way you should however be aware of the high amount of 
trust you put into cloudsmith not beeing compromised. As an alternative there 
is the manual way to achive the same result. That starts with adding a file to
/etc/apt/sources.list.d/ containing
```
deb [signed-by=/usr/share/keyrings/volkszaehler-volkszaehler-org-project-archive-keyring.gpg] https://dl.cloudsmith.io/public/volkszaehler/volkszaehler-org-project/deb/debian trixie main
deb-src [signed-by=/usr/share/keyrings/volkszaehler-volkszaehler-org-project-archive-keyring.gpg] https://dl.cloudsmith.io/public/volkszaehler/volkszaehler-org-project/deb/debian trixie main
```
You need to replace trixie with your distro and debian with raspbian in case
you are using an RPi 1. You also need to retrieve our repository key as a 
trusted one. 
```
curl -1sLf "https://dl.cloudsmith.io/public/volkszaehler/volkszaehler-org-project/gpg.21DBDAC56DF44DA1.key" | \
	gpg --dearmor > /usr/share/keyrings/volkszaehler-volkszaehler-org-project-archive-keyring.gpg
```

After that you can do the usual
```
apt update
apt install vzlogger
```

In addition an official Debian vzlogger package is in trixie and unstable.

OMS Support
-------------

We have packaged libmbus and are building our package with OMS support since 
0.8.7. Since we have no idea about usage and usability of our OMS support we
wold appreciate a short note from anyone using it or trying to use it in our 
[Future of OMS support](https://github.com/volkszaehler/vzlogger/issues/650) 
issue.


Mailing List
-------------
If you have questions, contact the volkszaehler mailing lists:

  * Users mailing list: https://demo.volkszaehler.org/mailman/listinfo/volkszaehler-users
  * Developers mailing list: https://demo.volkszaehler.org/mailman/listinfo/volkszaehler-dev

More information is available in our wiki:
http://wiki.volkszaehler.org/software/controller/vzlogger

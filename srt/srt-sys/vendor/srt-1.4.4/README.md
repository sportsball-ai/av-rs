# Secure Reliable Transport (SRT) Protocol

<p align="left">
  <a href="http://srtalliance.org/">
    <img alt="SRT" src="http://www.srtalliance.org/wp-content/uploads/SRT_text_hor_logo_grey.png" width="500"/>
  </a>
</p>

[![License: MPLv2.0][license-badge]](./LICENSE)
[![Latest release][release-badge]][github releases]
[![Debian Badge][debian-badge]][debian-package]  
[![LGTM Code Quality][lgtm-quality-badge]][lgtm-project]
[![LGTM Alerts][lgtm-alerts-badge]][lgtm-project]
[![codecov][codecov-badge]][codecov-project]  
[![Build Status Linux and macOS][travis-badge]][travis]
[![Build Status Windows][appveyor-badge]][appveyor]

## Introduction

Secure Reliable Transport (SRT) is an open source transport technology that optimizes streaming performance across unpredictable networks, such as the Internet.

|    |    |
| --- | --- |
| **S**ecure | Encrypts video streams |
| **R**eliable | Recovers from severe packet loss |
| **T**ransport | Dynamically adapts to changing network conditions |

SRT is applied to contribution and distribution endpoints as part of a video stream workflow to deliver the best quality and lowest latency video at all times.

As audio/video packets are streamed from a source to a destination device, SRT detects and adapts to the real-time network conditions between the two endpoints. SRT helps compensate for jitter and bandwidth fluctuations due to congestion over noisy networks, such as the Internet. Its error recovery mechanism minimizes the packet loss typical of Internet connections. And SRT supports AES encryption for end-to-end security, keeping your streams safe from prying eyes.

[Join the conversation](https://slackin-srtalliance.azurewebsites.net/) in the `#development` channel on [Slack](https://srtalliance.slack.com).

### Guides

* [SRT API Documents](docs/API/)
* [Using the `srt-live-transmit` App](docs/apps/srt-live-transmit.md)
* [SRT Developer's Guide](docs/dev/developers-guide.md)
* [Contributing](CONTRIBUTING.md)
* [Reporting Issues](docs/dev/making-srt-better.md)
* SRT RFC: [Latest IETF Draft](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00), [Latest Working Copy](https://haivision.github.io/srt-rfc/draft-sharabayko-srt.html), [GitHub Repo](https://github.com/Haivision/srt-rfc)
* SRT CookBook: [Website](https://srtlab.github.io/srt-cookbook), [GitHub Repo](https://github.com/SRTLab/srt-cookbook)
* [SRT Protocol Technical Overview](https://github.com/Haivision/srt/files/2489142/SRT_Protocol_TechnicalOverview_DRAFT_2018-10-17.pdf)
* [Why SRT Was Created](docs/misc/why-srt-was-created.md)

## Requirements

* C++03 (or above) compliant compiler.
* CMake 2.8.12 or above (as build system).
* OpenSSL 1.1 (to enable encryption, or build with `-DENABLE_ENCRYPTION=OFF`).
* Multithreading is provided by either of the following:
  * C++11: standard library (`std` by `-DENABLE_STDCXX_SYNC=ON` CMake option);
  * C++03: Pthreads (for POSIX systems it's built in, for Windows there is a ported library).
* Tcl 8.5 (optional, used by `./configure` script or use CMake directly).

For a detailed description of the build system and options, please refer to [SRT Build Options](docs/build/build-options.md).

### Build on Linux

Install cmake and openssl-devel (or similar name) package. For pthreads
there should be -lpthreads linker flag added.

Default installation path prefix of `make install` is `/usr/local`.

To define a different installation path prefix, use the `--prefix` option with `configure`
or [`-DCMAKE_INSTALL_PREFIX`](https://cmake.org/cmake/help/v3.0/variable/CMAKE_INSTALL_PREFIX.html) CMake option.

To uninstall, call `make -n install` to list all the dependencies, and then pass the list to `rm`.

#### Ubuntu 14

```shell
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install tclsh pkg-config cmake libssl-dev build-essential
./configure
make
```

#### CentOS 7

```shell
sudo yum update
sudo yum install tcl pkgconfig openssl-devel cmake gcc gcc-c++ make automake
./configure
make
```

#### CentOS 6

```shell
sudo yum update
sudo yum install tcl pkgconfig openssl-devel cmake gcc gcc-c++ make automake
sudo yum install centos-release-scl-rh devtoolset-3-gcc devtoolset-3-gcc-c++
scl enable devtoolset-3 bash
./configure --use-static-libstdc++ --with-compiler-prefix=/opt/rh/devtoolset-3/root/usr/bin/
make
```

### Build on Mac (Darwin, iOS)

[Homebrew](https://brew.sh/) supports "srt" formula.

```shell
brew update
brew install srt
```

If you prefer using a head commit of `master` branch, you should add `--HEAD` option
to `brew` command.

```shell
brew install --HEAD srt
```

Also, SRT can be built with `cmake` and `make` on Mac.
Install cmake and openssl with development files from "brew". Note that the
system version of OpenSSL is inappropriate, although you should be able to
use any newer version compiled from sources, if you prefer.

```shell
brew install cmake
brew install openssl
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
export OPENSSL_LIB_DIR=$(brew --prefix openssl)"/lib"
export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)"/include"
./configure
make
```

### Build on Windows

Follow the [Building SRT for Windows](docs/build/build-win.md) instructions.

[appveyor-badge]: https://img.shields.io/appveyor/ci/Haivision/srt/master.svg?label=Windows
[appveyor]: https://ci.appveyor.com/project/Haivision/srt
[travis-badge]: https://img.shields.io/travis/Haivision/srt/master.svg?label=Linux/macOS
[travis]: https://travis-ci.org/Haivision/srt
[license-badge]: https://img.shields.io/badge/License-MPLv2.0-blue

[lgtm-alerts-badge]: https://img.shields.io/lgtm/alerts/github/Haivision/srt
[lgtm-quality-badge]: https://img.shields.io/lgtm/grade/cpp/github/Haivision/srt
[lgtm-project]: https://lgtm.com/projects/g/Haivision/srt/

[codecov-project]: https://codecov.io/gh/haivision/srt
[codecov-badge]: https://codecov.io/gh/haivision/srt/branch/master/graph/badge.svg

[github releases]: https://github.com/Haivision/srt/releases
[release-badge]: https://img.shields.io/github/release/Haivision/srt.svg

[debian-badge]: https://badges.debian.net/badges/debian/testing/libsrt1/version.svg
[debian-package]: https://packages.debian.org/testing/libsrt1

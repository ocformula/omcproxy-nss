# omcproxy-nss

This is [omcproxy](https://github.com/openwrt/omcproxy) with support for
the multicast offload of Qualcomm NSS (IPQ806x) OpenWrt builds.

The NSS ECM accelerates a multicast route only when it can get the ports of
each bridged downlink from the `qca-mcs` snooping module, and `qca-mcs`
answers from flood tables that user space has to provide. omcproxy-nss
enables `qca-mcs` on the bridges it proxies to, reads the groups that
`qca-mcs` snoops and keeps one flood entry per group with the ports that
have members. Interfaces are taken from the omcproxy configuration, nothing
is tied to particular names.

It also fixes two problems of omcproxy itself:
- the routes of a group are re-evaluated when its membership changes, so a
  stream that arrives before its first receiver (e.g. after a restart) is
  forwarded right away instead of after the route expires
- more than one proxy (e.g. one for IPv4 and one for IPv6) can be set

Requirements:
- `kmod-qca-mcs`
- an ECM that does not hold multicast flows back with `accel_delay_pkts`,
  see the `qca-nss-ecm` patch in
  https://github.com/ocformula/nss-packages (branch NSS-11.2-K5.15)

Building for OpenWrt: add this repository as a feed and select
`omcproxy-nss`, which replaces the `omcproxy` package and uses the same
configuration (`/etc/config/omcproxy`):

    src-git omcproxy_nss https://github.com/ocformula/omcproxy-nss.git

Host unit tests: `LIBUBOX_DIR=<dir containing libubox/> sh tests/run.sh`

The original README follows.

# omcproxy - Embedded IGMPv3 and MLDv2 proxy

omcproxy is an IGMPv3 and MLDv2 multicast proxy for use in embedded Linux
devices like routers. It is small in size and can be compiled to <40 KB.

It is partly based on code of https://github.com/Oryon/pimbd

## Specifications and Features

1. Source-Specific Multicast Querier
	- MLDv2 querier (based on RFC 3810)
	- IGMPv3 querier (based on RFC 3376)

2. Multicast Proxying (based on RFC 4605)
	- Kernel-space multicast routing
	- Multiple instances support
	- Address-scope specific proxying
	

## Compiling

omcproxy uses libubox as submodule, be sure to clone this git repository
with --recursive or run: "git submodule init; git submodule update"
after cloning. If you are already using libubox as a shared library
just pass -DWITH_LIBUBOX=1 to cmake.

omcproxy uses cmake:
- To prepare a Makefile use:  "cmake ." 
- To build / install use: "make" / "make install" afterwards.
- To build DEB or RPM packages use: "make package" afterwards.

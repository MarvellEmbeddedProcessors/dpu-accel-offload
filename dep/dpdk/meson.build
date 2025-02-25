# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2023 Marvell.

libdpdk_dep = dependency('libdpdk', method : 'pkg-config', required:false)
if libdpdk_dep.found()
	if libdpdk_dep.version().version_compare('<' + dpdk_version)
		error('DPDK version ' + libdpdk_dep.version() +  ' not supported need > ' + dpdk_version)
	endif
	dpdk_version = libdpdk_dep.version()

	DAO_BUILD_CONF.set('DAO_LIBDPDK_DEP', '1')
	DAO_EXT_DEPS_LIBDPDK += libdpdk_dep
else
	message('DPDK pkgconfig not exported')
	subdir_done()
endif

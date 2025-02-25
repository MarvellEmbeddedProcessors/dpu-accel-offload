# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2023 Marvell.

libnl_dep = dependency('libnl-3.0', method : 'pkg-config', version: '>=3.5.0', required:false, static:true)
libnlroute_dep = dependency('libnl-route-3.0', method : 'pkg-config', version: '>=3.5.0', required:false, static:true)
libnlxfrm_dep = dependency('libnl-xfrm-3.0', method : 'pkg-config', version: '>=3.5.0', required:false, static:true)

if not libnlroute_dep.found()
	message('libnlroute pkgconfig not exported')
	subdir_done()
endif

if not libnlxfrm_dep.found()
	message('libnlxfrm pkgconfig not exported')
	subdir_done()
endif

if not libnl_dep.found()
	message('libnl pkgconfig not exported')
	subdir_done()
endif

DAO_BUILD_CONF.set('DAO_LIBNL_DEP', '1')
libnl_deps = [libnl_dep, libnlroute_dep, libnlxfrm_dep]
DAO_EXT_DEPS_LIBNL += libnl_deps

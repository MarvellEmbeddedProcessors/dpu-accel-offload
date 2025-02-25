# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2023 Marvell.

if not DAO_BUILD_CONF.has('DAO_LIBNL_DEP')
  message('libnl not found. skipping netlink library')
  skip_lib = true
  subdir_done()
endif

if host_build
	skip_lib = true
endif

deps += ['common']

sources = files(
	'dao_netlink.c',
	'dao_netlink_xfrm.c',
	'dao_netlink_route.c',
)

headers = files(
	'dao_netlink.h',
	'dao_netlink_crypto.h',
	'dao_netlink_xfrm.h',
	'dao_netlink_route.h'
)
lib_deps += DAO_EXT_DEPS_LIBNL

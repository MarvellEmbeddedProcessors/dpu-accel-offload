# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2024 Marvell.

if host_build
	skip_lib = true
endif

sources = files(
	'dao_flow.c',
	'flow_acl.c',
	'flow_hw_offload.c',
        'flow_parser.c',
        'flow_dbg.c',
        'ovs_profile.c',
        'default_profile.c',
)

headers = files(
	'dao_flow.h',
)

deps += ['common']

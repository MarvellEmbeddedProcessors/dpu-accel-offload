# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2023 Marvell.

project(
	'Data Accelerator Offload',
	'C', 'CPP',
	meson_version: '>= 0.61.0',
	version: run_command(find_program('cat', 'more'),
							files('VERSION'), check: true).stdout().strip(),
	default_options: ['warning_level=2', 'werror=true', 'buildtype=release','cpp_std=c++17']
)

# Build Configuration Data
DAO_BUILD_CONF = configuration_data()
# DAO sources
DAO_SOURCES = []
# DAO include directories
DAO_INCLUDES = []
# External library dependencies
# DPDK
DAO_EXT_DEPS_LIBDPDK = []
# LIBNL
DAO_EXT_DEPS_LIBNL = []
# GRPC
DAO_EXT_DEPS_GRPC = []
# DAO static libraries
DAO_STATIC_LIBS = []
# Enabled/disabled static dao applicaitons/libraries/tests
DAO_MODULES_ENABLED = []
DAO_MODULES_DISABLED = []

host_build = false
if (get_option('enable_host_build'))
	host_build = true
endif

# If CPU is not ARM, treat as host build
if host_machine.cpu_family() != 'aarch64'
	host_build = true
endif

if not host_build
	if not meson.version().version_compare('>=0.63.0')
		error('Meson version 0.63 or higher is required for DAO EP build.')
	endif
endif

dpdk_version = run_command(find_program('cat', 'more'),
							files('DPDK_VERSION'), check: true).stdout().strip()
message('Supported DPDK version: ' + dpdk_version)
subdir('buildtools')
subdir('dep')
subdir('config')

# Mandatory dependency
if DAO_BUILD_CONF.has('DAO_LIBDPDK_DEP')
	subdir('lib')
	subdir('app')
	subdir('tests')
endif

doxygen = find_program('doxygen', required: false)
if doxygen.found()
    subdir('doc')
else
    warning('Doxygen not found. Skipping the documentation build!')
endif

if get_option('enable_kmods')
    subdir('kmod')
endif

# Dump the enable modules status
msg = '\n===============\nEnabled Modules\n===============\n'
last = ''
foreach comp : DAO_MODULES_ENABLED
	if last != comp[0]
		msg += '\n' + comp[0] + ':'
	endif
	msg += '\n\t'
	foreach elem : comp[1]
		msg += elem + ', '
	endforeach
	last = comp[0]
endforeach
msg += '\n\n================\nDisabled Modules\n================\n'
last = ''
foreach comp : DAO_MODULES_DISABLED
	if last != comp[0]
		msg += '\n' + comp[0] + ':'
	endif
	msg += '\n\t'
	foreach elem : comp[1]
		msg += elem + ', '
	endforeach
	last = comp[0]
endforeach
msg += '\n\n================\n'
message(msg)

# DAO tests
subdir('ci/test/dao-test')

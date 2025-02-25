# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2023 Marvell.

tests = [
	'bar-rw',
	'dpi_test',
	'virtio-extbuf',
	'flow-offload',
]

# Mandatory dependency
if not DAO_BUILD_CONF.has('DAO_LIBDPDK_DEP')
	error('Missing mandatory dependency "libdpdk"')
endif

default_cflags = machine_args
default_cflags += ['-DALLOW_EXPERIMENTAL_API']
default_cflags += ['-DALLOW_INTERNAL_API']
default_cflags += ['-g']
default_cflags += ['-pthread']
default_cflags += ['-D_GNU_SOURCE']

foreach test: tests
    name = test
    build = true

    sources = []
    includes = []
    deps = []
    cflags = default_cflags
    ldflags = []

    subdir(test)

    dep_objs = DAO_EXT_DEPS_LIBDPDK
    # Defining the static library dependencies
    foreach d:deps
        var_name = 'static' + '_dao_' + d
        if not is_variable(var_name)
            build = false
            message('Missing dependency "@0@" for example "@1@"'.format(d, name))
            break
        endif
        dep_objs += [get_variable(var_name)]
    endforeach

    if not build
        message('Skipping test "' + name + '"')
	disabled += [name]
        continue
    endif

    enabled += [name]
    executable('dao-' + name, sources,
            include_directories: DAO_INCLUDES,
	    link_whole: DAO_STATIC_LIBS,
            link_args: ldflags,
            c_args: cflags,
            dependencies: dep_objs)
endforeach

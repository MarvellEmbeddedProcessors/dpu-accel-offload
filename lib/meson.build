# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2023 Marvell.

libraries = [
	'common',
	'portgroup',
	'vfio',
	'pem',
	'virtio',
	'virtio_net',
	'workers',
	'netlink',
	'pal',
	'featurearc',
	'flow'
]

enabled = []
disabled = []
default_cflags = machine_args
default_cflags += ['-DALLOW_EXPERIMENTAL_API']
default_cflags += ['-DALLOW_INTERNAL_API', '-flax-vector-conversions' ]

foreach l:libraries
    build = true
    name = l
    sources = []
    headers = []
    includes = []
    deps = []
    cflags = default_cflags
    cxxflags = default_cflags
    skip_lib = false
    lib_cflags = []
    lib_cxxflags = []
    lib_deps = []
    subdir(l)

    # Skip library compilation if marked
    if skip_lib
	disabled += [name]
        continue
    endif

    cflags += lib_cflags
    cxxflags += lib_cxxflags

    libname = 'dao_' + name
    includes += include_directories(l)
    includes += DAO_INCLUDES

    shared_deps = lib_deps + DAO_EXT_DEPS_LIBDPDK
    static_deps = lib_deps + DAO_EXT_DEPS_LIBDPDK
    foreach d:deps
	if not is_variable('static_dao_' + d)
            build = false
            reason = 'missing internal dependency, "@0@"'.format(d)
            message('Disabling @1@ [@2@]: missing internal dependency "@0@"'
                    .format(d, name, 'lib/' + l))
        else
            shared_deps += [get_variable('shared_dao_' + d)]
            static_deps += [get_variable('static_dao_' + d)]
        endif
    endforeach

    if not build
        set_variable(name.underscorify() + '_disable_reason', reason)
	disabled += [libname]
        continue
    endif

    enabled += [libname]
    install_headers(headers)

    #build static lib
    static_lib = static_library(libname,
            sources,
            c_args: cflags,
            cpp_args: cxxflags,
            include_directories: includes,
            dependencies: static_deps,
            install: true)
    static_dep = declare_dependency(
            include_directories: includes,
            dependencies: static_deps)

    shared_lib = shared_library(libname,
            sources,
            c_args: cflags,
            cpp_args: cxxflags,
            dependencies: shared_deps,
            include_directories: includes,
            version: DAO_BUILD_CONF.get('DAO_LIB_VERSION'),
            soversion: DAO_BUILD_CONF.get('DAO_VERSION_MAJOR'),
            install: true)
    shared_dep = declare_dependency(link_with: shared_lib,
            include_directories: includes,
            dependencies: shared_deps)

    set_variable('shared_dao_' + name, shared_dep)
    set_variable('static_dao_' + name, static_dep)

    DAO_STATIC_LIBS += static_lib

endforeach

# Update global configuration
if enabled.length() > 0
    DAO_MODULES_ENABLED += [['Libraries', enabled]]
endif

if disabled.length() > 0
    DAO_MODULES_DISABLED += [['Libraries', disabled]]
endif

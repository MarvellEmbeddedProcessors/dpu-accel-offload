# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2023 Marvell.

apps = [
        'ovs-offload',
        'secgw-graph',
        'smart-nic',
        'virtio-l2fwd',
]

enabled = []
disabled = []
skip_app = false
default_cflags = machine_args
default_cflags += ['-DALLOW_EXPERIMENTAL_API']
default_cflags += ['-g']
default_cflags += ['-DALLOW_INTERNAL_API']
default_cflags += ['-D_GNU_SOURCE']

foreach app: apps
    name = app
    build = true

    sources = []
    includes = []
    deps = []
    cflags = default_cflags
    cxxflags = default_cflags
    ldflags = []

    skip_app = false
    app_deps = []
    app_cflags = []
    app_cxxflags = []

    subdir(app)

    # Skip app compilation if marked
    if skip_app
        disabled += [name]
        continue
    endif

    dep_objs = app_deps
    cflags += app_cflags
    cxxflags += app_cxxflags
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
        message('Skipping app "' + name + '"')
        disabled += [name]
        continue
    endif

    enabled += [name]
    executable('dao-' + name, sources,
            include_directories: DAO_INCLUDES + includes,
            link_whole: DAO_STATIC_LIBS,
            link_args: ldflags,
            c_args: cflags,
            cpp_args: cxxflags,
            dependencies: dep_objs,
            install: true)
endforeach

# Update global configuration
if enabled.length() > 0
    DAO_MODULES_ENABLED += [['Applications', enabled]]
endif

if disabled.length() > 0
    DAO_MODULES_DISABLED += [['Applications', disabled]]
endif

# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2025 Marvell.

protoc = find_program('protoc', required:false)
grpc_cpp_plugin = find_program('grpc_cpp_plugin', required:false)
re2_dep = dependency('re2', method : 'pkg-config', version: '>=11.0.0', required:false)
grpcplus_dep = dependency('grpc++', method : 'pkg-config', version: '>=1.65.0', required:false)
protobuf_dep = dependency('protobuf', method : 'pkg-config', version:'>=27.2.0', required:false)

if not protoc.found()
	message('protobuf compiler (protoc) not found')
	subdir_done()
endif

if not grpc_cpp_plugin.found()
	message('grpc_cpp_plugin not found')
	subdir_done()
endif

if not re2_dep.found()
	message('re2 not found')
	subdir_done()
endif

if not protobuf_dep.found()
	message('protobuf pkgconfig not exported')
	subdir_done()
endif

if not grpcplus_dep.found()
	message('grpcplus pkgconfig not exported')
	subdir_done()
endif

DAO_BUILD_CONF.set('DAO_GRPC_DEP', '1')
DAO_EXT_DEPS_GRPC += grpcplus_dep
DAO_EXT_DEPS_GRPC += protobuf_dep

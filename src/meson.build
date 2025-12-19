# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2025 Project AERO.

# src/meson.build - Build the hft-app executable

# Collect sources from subdirectories
subdir('modules')

core_sources = files(
    'core/init.c',
    'core/logging.cpp',
    'core/forwarding.cpp',
)

config_sources = files(
    'config/config.c',
)

# Main entry point
main_sources = files('main.cpp')

# All application sources
app_sources = main_sources + core_sources + config_sources

# Include directories for the source tree
src_inc = include_directories(
    '.',
    'core',
    'config',
    'modules',
    'modules/parser', # Add parser include directory
)

# Build the executable
executable('hft-app',
    app_sources,
    include_directories: [src_inc, root_inc],
    dependencies: [dpdk_dep, openssl_dep, simdjson_dep, boost_dep, thread_dep],
    link_with: modules_libs,
    install: true,
)

test_eal_sources = files('test_eal.c')
executable('test_eal',
    test_eal_sources,
    include_directories: [root_inc], # Just root_inc for minimal test
    dependencies: dpdk_dep,
    install: false,
)
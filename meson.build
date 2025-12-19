# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2025 Project AERO.
#
# Project AERO: HFT Gateway for Crypto Exchanges
# Root meson.build - Orchestrates the build process

project('hft-gateway', 'c', 'cpp',
    default_options: [
        'c_std=c11',
        'cpp_std=c++23',
        'warning_level=2',
        'b_lto=true',
    ],
    version: '0.1.0',
    license: 'BSD-3-Clause',
    meson_version: '>= 0.60.0',
)

# ============================================================================
# Dependencies
# ============================================================================

dpdk_dep = dependency('libdpdk', version: '>=23.11')
openssl_dep = dependency('openssl', version: '>=1.1.1')
# simdjson is configured later to respect add_project_arguments
gtest_dep = dependency('gtest')
boost_dep = dependency('boost', modules: ['system', 'thread'])
thread_dep = dependency('threads')

# ============================================================================
# Compiler Configuration
# ============================================================================

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

# Core compiler flags for HFT performance and security hardening
common_flags = [
    '-O3',
    '-march=' + get_option('cpu_instruction_set'),
    '-D_GNU_SOURCE',
    '-DENABLE_LATENCY_BENCHMARK=1',
    '-fstack-protector-strong',
    '-D_FORTIFY_SOURCE=2',
    '-fPIE',
]

# Linker hardening flags
add_project_link_arguments('-Wl,-z,relro,-z,now', language: ['c', 'cpp'])
add_project_link_arguments('-pie', language: ['c', 'cpp'])


# AVX2 is x86 specific
if cc.has_argument('-mavx2')
    common_flags += '-mavx2'
endif

# Suppress some warnings for DPDK compatibility
warn_flags = [
    '-Wno-deprecated-declarations',
    '-Wno-unused-parameter',
    '-Wno-unused-function',
    '-Wno-pedantic', # DPDK headers trigger pedantic warnings
    '-Wno-volatile', # C++20/23 deprecated volatile operations in DPDK
]

# Apply flags to both C and C++
foreach flag : common_flags + warn_flags
    if cc.has_argument(flag)
        add_project_arguments(flag, language: 'c')
    endif
    if cpp.has_argument(flag)
        add_project_arguments(flag, language: 'cpp')
    endif
endforeach

# Sanitizers (optional)
if get_option('enable_sanitizers')
    sanitizer_flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    foreach flag : sanitizer_flags
        if cc.has_argument(flag)
            add_project_arguments(flag, language: 'c')
            add_project_link_arguments(flag, language: 'c')
        endif
        if cpp.has_argument(flag)
            add_project_arguments(flag, language: 'cpp')
            add_project_link_arguments(flag, language: 'cpp')
        endif
    endforeach
endif

# simdjson from subprojects
simdjson_dep = dependency('simdjson', fallback: ['simdjson', 'simdjson_dep'])

# ============================================================================
# Include Directories
# ============================================================================

root_inc = include_directories('include')

# All application source includes
app_inc = include_directories(
    'src',
    'src/core',
    'src/config',
    'src/modules',
    'src/modules/parser',
    'src/modules/network',
    'src/modules/telemetry', # Added telemetry include
)

# ============================================================================
# Subdirectories
# ============================================================================

subdir('src')
subdir('examples')

if get_option('enable_tests')
    subdir('tests')
endif

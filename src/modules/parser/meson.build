# src/modules/parser/meson.build

parser_sources = files(
    'parser.cpp',
    'json_parser.cpp',
)

lib_parser = static_library(
    'parser',
    parser_sources,
    include_directories: app_inc,
    dependencies: [dpdk_dep, simdjson_dep],
)
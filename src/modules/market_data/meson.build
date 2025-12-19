# src/modules/market_data/meson.build

market_data_sources = files(
    'order_book.cpp',
)

lib_market_data = static_library('market_data',
    market_data_sources,
    include_directories: app_inc,
    dependencies: [dpdk_dep, simdjson_dep],
)

market_data_lib = lib_market_data
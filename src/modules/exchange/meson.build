# Exchange adapters module

exchange_sources = files(
    'okx_adapter.cpp',
    'bybit_adapter.cpp',
    'okx_connection.cpp',
    'bybit_connection.cpp',
)

lib_exchange = static_library(
    'exchange',
    exchange_sources,
    include_directories: app_inc,
    dependencies: [simdjson_dep, boost_dep, thread_dep],
    link_with: [lib_network],
)

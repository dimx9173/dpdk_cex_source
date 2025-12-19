# src/modules/network/meson.build

network_sources = files(
    'micro_tcp.cpp',
    'network_utils.cpp',
    'tls_socket.cpp',
    'websocket_client.cpp',
    'boost_websocket_client.cpp',
    'udp_publisher.cpp',
)

lib_network = static_library('network',
    network_sources,
    include_directories: app_inc,
    dependencies: [dpdk_dep, openssl_dep, simdjson_dep, boost_dep, thread_dep],
)

# Export the library for linking
network_lib = lib_network
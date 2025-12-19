# Modules source files

subdir('parser')
subdir('network')
subdir('market_data')
# # subdir('execution')
subdir('exchange')

classifier_sources = files(
    'classifier/classifier.cpp'
)

lib_classifier = static_library(
    'classifier',
    classifier_sources,
    include_directories: app_inc,
    dependencies: [dpdk_dep],
)

strategy_sources = files(
    'strategy/strategy.cpp'
)

lib_strategy = static_library(
    'strategy',
    strategy_sources,
    include_directories: app_inc,
    dependencies: [dpdk_dep, simdjson_dep, boost_dep, thread_dep],
    link_with: [lib_parser, market_data_lib, lib_exchange],
)

# Collect all module libraries
modules_libs = [lib_parser, lib_classifier, lib_network, market_data_lib, lib_exchange]
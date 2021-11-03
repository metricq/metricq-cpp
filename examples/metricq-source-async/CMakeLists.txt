cmake_minimum_required(VERSION 3.8)

project(metricq-source-async VERSION 1.0)

add_executable(metricq-source-async src/main.cpp src/async_source.cpp)
target_compile_features(metricq-source-async PRIVATE cxx_std_17)
target_compile_warnings(metricq-source-async PRIVATE)

target_link_libraries(metricq-source-async
    PRIVATE
        metricq::source
        metricq::logger-nitro
        Nitro::options
)

install(TARGETS metricq-source-async RUNTIME DESTINATION bin)

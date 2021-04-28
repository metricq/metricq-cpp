cmake_minimum_required(VERSION 3.8)

project(metricq-db-dummy VERSION 1.0)

add_executable(metricq-db-dummy src/main.cpp src/dummy_db.cpp)
target_compile_features(metricq-db-dummy PRIVATE cxx_std_17)
target_compile_options(metricq-db-dummy PRIVATE -Wall -Wextra -pedantic)

target_link_libraries(metricq-db-dummy
    PRIVATE
        metricq::db
        metricq::logger-nitro
        Nitro::options
)

install(TARGETS metricq-db-dummy RUNTIME DESTINATION bin)

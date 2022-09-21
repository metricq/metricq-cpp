find_package(benchmark QUIET)

add_executable(metricq.test_data_sizes test_data_sizes.cpp)
add_test(metricq.test_data_sizes metricq.test_data_sizes)
target_link_libraries(metricq.test_data_sizes PRIVATE metricq::core)

if (benchmark_FOUND)
    message(STATUS "Looking for google benchmark - found, will build benchmark tests.")

    add_executable(metricq.bench_data_chunk bench_data_chunk.cpp)
    add_test(metricq.bench_data_chunk metricq.bench_data_chunk)
    target_link_libraries(metricq.bench_data_chunk PRIVATE metricq::core benchmark::benchmark)
else()
    message(STATUS "Looking for google benchmark - not found, skipping benchmark tests.")
endif()

add_executable(asio_signal_handler asio_signal_handler.cpp)
target_link_libraries(asio_signal_handler PUBLIC asio::asio amqpcpp::amqpcpp)

Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.0.1 # or a later release
)

FetchContent_MakeAvailable(Catch2)

add_executable(test_async_promise test_async_promise.cpp)
target_link_libraries(test_async_promise PRIVATE Catch2::Catch2WithMain metricq::core)
add_test(test_async_promise test_async_promise)

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <metricq/awaitable.hpp>
#include <metricq/history.pb.h>

#include <asio.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>

using namespace metricq;

using namespace std::literals::string_literals;

TEST_CASE("AsyncPromise<void> can be awaited")
{

    asio::io_service io_service;

    metricq::AsyncPromise<void> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     co_await future.get();
                     REQUIRE(true);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_done();
                     co_return;
                 }());
    }

    SECTION("Can be awaited from one future with timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     co_await future.get(std::chrono::milliseconds(100));
                     REQUIRE(true);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_done();
                     co_return;
                 }());
    }

    SECTION("awaiting can timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get(std::chrono::milliseconds(1)));
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     co_await future.get();
                     REQUIRE(true);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     co_await future.get();
                     REQUIRE(true);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_done();
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to the future context")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get());
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to both future contexts")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    io_service.run();
}

TEST_CASE("AsyncPromise<int> can be awaited")
{

    asio::io_service io_service;

    metricq::AsyncPromise<int> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE(co_await future.get() == 42);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(42);
                     co_return;
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE(co_await future.get() == 42);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE(co_await future.get() == 42);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(42);
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to the future context")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get());
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to both future contexts")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    io_service.run();
}

TEST_CASE("AsyncPromise<std::string> can be awaited")
{

    asio::io_service io_service;

    metricq::AsyncPromise<std::string> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(result == "42");
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result("42");
                     co_return;
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(result == "42");
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(result == "42");
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result("42");
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to the future context")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get());
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to both future contexts")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    io_service.run();
}

TEST_CASE("AsyncPromise<std::unique_ptr<std::string>> can be awaited")
{
    asio::io_service io_service;

    metricq::AsyncPromise<std::unique_ptr<std::string>> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(*result == "42"s);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(std::make_unique<std::string>("42"));
                     co_return;
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(*result == "42"s);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(*result == "42"s);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(std::make_unique<std::string>("42"));
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to the future context")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get());
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to both future contexts")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    io_service.run();
}

class MyPolymorphicClass
{
public:
    MyPolymorphicClass() = default;

    virtual ~MyPolymorphicClass() = default;

    virtual std::string foo()
    {
        return data_;
    }

    std::string data_;
};

TEMPLATE_TEST_CASE("AsyncPromise<TestType> can time out", "[AsyncPromise][template]", int,
                   std::string, (std::tuple<int, float>), std::unique_ptr<std::string>,
                   MyPolymorphicClass)
// I'm not sure why, but when trying to use a Protobuf-generated class
// in the AsyncPromise, I get an error message, which I wasn't able to decipher.
{
    asio::io_service io_service;
    metricq::AsyncPromise<TestType> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(std::is_same_v<TestType, decltype(result)>);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(TestType{});
                     co_return;
                 }());
    }

    SECTION("Can be awaited with timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get(std::chrono::milliseconds(10));
                     REQUIRE(std::is_same_v<TestType, decltype(result)>);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(TestType{});
                     co_return;
                 }());
    }

    SECTION("awaiting can timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get(std::chrono::milliseconds(10)));
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(std::is_same_v<TestType, decltype(result)>);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(std::is_same_v<TestType, decltype(result)>);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(TestType{});
                     co_return;
                 }());
    }

    io_service.run();
}

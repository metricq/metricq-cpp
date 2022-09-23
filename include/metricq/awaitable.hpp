// Copyright (c) 2021, ZIH,
// Technische Universitaet Dresden,
// Federal Republic of Germany
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright notice,
//       this list of conditions and the following disclaimer in the documentation
//       and/or other materials provided with the distribution.
//     * Neither the name of metricq nor the names of its contributors
//       may be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <metricq/chrono.hpp>
#include <metricq/exception.hpp>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/promise.hpp>
#include <asio/io_context.hpp>
#include <asio/system_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <exception>
#include <future>
#include <iostream>
#include <optional>
#include <type_traits>

namespace metricq
{
template <typename T>
using Awaitable = asio::awaitable<T>;

static auto use_awaitable = asio::use_awaitable;

template <typename Executor, typename T>
inline void co_spawn(Executor& e, Awaitable<T> a)
{
    asio::co_spawn(e, std::move(a), asio::detached);
}

template <typename Executor, typename F>
inline void co_spawn(Executor& e, F&& f)
{
    asio::co_spawn(e, std::forward<F>(f), asio::detached);
}

namespace detail
{
    template <typename ExceptionHandler>
    class CompletionHandler
    {
    public:
        CompletionHandler(ExceptionHandler& handler) : handler(handler)
        {
        }

        void operator()(std::exception_ptr eptr)
        {
            try
            {
                if (eptr)
                {
                    std::rethrow_exception(eptr);
                }
            }
            catch (const std::exception& e)
            {
                handler.on_unhandled_exception(e);
            }
        }

        template <typename T>
        void operator()(std::exception_ptr eptr, T&&)
        {
            (*this)(eptr);
        }

    private:
        ExceptionHandler& handler;
    };
} // namespace detail

template <typename Executor, typename T, typename ExceptionHandler>
inline void co_spawn(Executor& e, Awaitable<T> a, ExceptionHandler& handler)
{
    asio::co_spawn(e, std::move(a), detail::CompletionHandler<ExceptionHandler>(handler));
}

template <typename Executor, typename F, typename ExceptionHandler>
inline void co_spawn(Executor& e, F&& f, ExceptionHandler& handler)
{
    asio::co_spawn(e, std::forward<F>(f), detail::CompletionHandler<ExceptionHandler>(handler));
}

inline Awaitable<void> wait_until(TimePoint timeout)
{
    asio::system_timer timer(co_await asio::this_coro::executor);
    timer.expires_at(timeout);
    co_await timer.async_wait(asio::use_awaitable);
}

inline Awaitable<void> wait_for(Duration timeout)
{
    asio::system_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(timeout);
    co_await timer.async_wait(asio::use_awaitable);
}

template <typename T>
class AsyncPromise;

template <typename T>
class AsyncFuture
{
    explicit AsyncFuture(
        asio::experimental::promise<void(std::exception_ptr, std::optional<T>)>&& promise)
    : promise_(std::move(promise))
    {
    }

public:
    Awaitable<T> get()
    {
        co_return *(co_await promise_.async_wait(use_awaitable));
    }

    Awaitable<T> get(Duration timeout)
    {
        using asio::experimental::awaitable_operators::operator||;

        auto result = co_await (promise_.async_wait(use_awaitable) || wait_for(timeout));

        if (result.index() == 0)
        {
            co_return std::forward<T>(*std::get<0>(result));
        }
        else
        {
            throw TimeoutError("Promise didn't finish within the timeout");
        }
    }

private:
    friend class AsyncPromise<T>;

    asio::experimental::promise<void(std::exception_ptr, std::optional<T>)> promise_;
};

template <>
class AsyncFuture<void>
{
    explicit AsyncFuture(asio::experimental::promise<void(std::exception_ptr)>&& promise)
    : promise_(std::move(promise))
    {
    }

public:
    Awaitable<void> get()
    {
        co_await promise_.async_wait(use_awaitable);
    }

    Awaitable<void> get(Duration timeout)
    {
        using asio::experimental::awaitable_operators::operator||;

        auto result = co_await (get() || wait_for(timeout));

        if (result.index() == 0)
        {
            co_return;
        }
        else
        {
            throw TimeoutError("Promise didn't finish within the timeout");
        }
    }

    void cancel()
    {
        promise_.cancel();
    }

private:
    friend class AsyncPromise<void>;

    asio::experimental::promise<void(std::exception_ptr)> promise_;
};

template <typename T>
class AsyncPromise
{
    using AsioPromiseHandler =
        asio::experimental::detail::promise_handler<void(std::exception_ptr, std::optional<T>)>;

    struct PromiseCancellationHandler
    {
        AsioPromiseHandler& self_;

        void operator()(asio::cancellation_type)
        {
            self_.impl_->completion({}, {});
        }
    };

public:
    explicit AsyncPromise(asio::io_context& io_context) : handler_(io_context.get_executor())
    {
        handler_.get_cancellation_slot().template emplace<PromiseCancellationHandler>(handler_);
    }

    void set_exception(std::exception_ptr eptr)
    {
        handler_(eptr, {});
    }

    template <typename Exception>
    void set_exception(Exception&& e)
    {
        set_exception(std::make_exception_ptr(std::move(e)));
    }

    void set_result(T&& value)
    {
        handler_({}, std::move(value));
    }

    template <typename U, typename = std::enable_if_t<std::is_copy_constructible<T>::value &&
                                                      std::is_same_v<T, U>>>
    void set_result(const U& value)
    {
        handler_({}, value);
    }

    AsyncFuture<T> get_future()
    {
        return AsyncFuture<T>{ handler_.make_promise() };
    }

private:
    AsioPromiseHandler handler_;
};

template <>
class AsyncPromise<void>
{
    using AsioPromiseHandler =
        asio::experimental::detail::promise_handler<void(std::exception_ptr)>;
    struct PromiseCancellationHandler
    {
        AsioPromiseHandler& self_;

        void operator()(asio::cancellation_type)
        {
            self_.impl_->completion({});
        }
    };

public:
    explicit AsyncPromise(asio::io_context& io_context) : handler_(io_context.get_executor())
    {
        handler_.get_cancellation_slot().emplace<PromiseCancellationHandler>(handler_);
    }

    void set_exception(std::exception_ptr eptr)
    {
        handler_(eptr);
    }

    template <typename Exception>
    void set_exception(Exception&& e)
    {
        set_exception(std::make_exception_ptr(std::move(e)));
    }

    void set_done()
    {
        handler_({});
    }

    AsyncFuture<void> get_future()
    {
        return AsyncFuture<void>{ handler_.make_promise() };
    }

private:
    AsioPromiseHandler handler_;
};

} // namespace metricq

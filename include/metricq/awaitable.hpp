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
#include <asio/io_context.hpp>
#include <asio/system_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <asio/defer.hpp>
#include <future>
#include <iostream>

namespace metricq
{
template <typename T>
using awaitable = asio::awaitable<T>;

using use_awaitable_t = asio::use_awaitable_t<>;

static auto use_awaitable = asio::use_awaitable;

template <typename Executor, typename T>
inline void co_spawn(Executor& e, awaitable<T> a)
{
    asio::co_spawn(e, std::move(a), asio::detached);
}

template <typename Executor, typename T, typename CompletionToken>
inline auto co_spawn(Executor& e, awaitable<T> a, CompletionToken&& token)
{
    return asio::co_spawn(e, std::move(a), std::move(token));
}

template <typename T>
class FutureAwaiter : public asio::detail::awaitable_thread<T>
{
public:
    FutureAwaiter(asio::io_context& ctx, std::future<T>& fut) : ctx_(ctx), timer_(ctx), fut_(fut)
    {
    }
    //
    //    FutureAwaiter(std::future<T>& fut, TimePoint wait_until)
    //    : fut_(fut), has_deadline_(true), deadline_(wait_until)
    //    {
    //    }
    //
    //    FutureAwaiter(std::future<T>& fut, Duration wait_for)
    //    : fut_(fut), has_deadline_(true), deadline_(Clock::now() + wait_for)
    //    {
    //    }

    bool await_ready()
    {
        return fut_.wait_for(Duration(0)) == std::future_status::ready;
    }

    bool await_suspend(std::coroutine_handle<> ch)
    {
        auto status = fut_.wait_for(Duration(0));
        if (status == std::future_status::ready)
        {
            return false;
        }

        // HACK
        // asio::defer(ctx_,
        //            [ch, this](auto error)
        //            {
        //                std::this_thread::sleep_for(std::chrono::seconds(10));

        //                auto status = fut_.wait_for(Duration(0));
        //                if (status == std::future_status::ready)
        //                {
        //                    ch.resume();
        //                }
        //            });

        timer_.expires_from_now(std::chrono::seconds(1));
        timer_.async_wait(
            [this, ch]()
            {
                assert(fut_.wait_for(Duration(0)) == std::future_status::ready);
                ch.resume();
            });

        return true;
    }

    auto await_resume()
    {
        return fut_.get();
    }

private:
    asio::io_context& ctx_;
    asio::system_timer timer_;
    std::future<T>& fut_;
    bool has_deadline_ = false;
    TimePoint deadline_;
};

// template <typename T>
// auto async_await(asio::system_timer& t, std::future<T>& fut, Duration d)
//{
//    struct Awaiter
//    {
//        asio::system_timer& t;
//        Duration d;
//        TimePoint start;
//        std::error_code ec;
//
//        bool await_ready()
//        {
//            return fut_.wait_for(Duration(0)) == std::future_status::ready;
//        }
//
//        auto await_resume()
//        {
//            if (ec)
//                throw std::system_error(ec);
//
//        }
//        void await_suspend(std::experimental::coroutine_handle<> coro)
//        {
//            start = Clock::now();
//            t.expires_from_now(std::chrono::milliseconds(10));
//            t.async_wait([this, coro](auto ec) {
//
//                });
//        }
//
//        void time_callback(std::experimental::coroutine_handle<> coro, std::error_code ec)
//        {
//
//            t.async_wait(
//                [this, coro](auto ec)
//                {
//                    this->ec = ec;
//                    coro.resume();
//                });
//        }
//    };
//    return Awaiter{ t, d };
//}

template <typename T>
FutureAwaiter<T> await_future(asio::io_context& ctx, std::future<T>& fut)
{
    return FutureAwaiter<T>(ctx, fut);
}

template <typename T>
awaitable<T> wait_for(asio::io_context& ctx, std::future<T>& fut, Duration timeout)
{
    asio::system_timer test_timer(ctx);

    //(void)timeout;
    // co_return co_await await_future(ctx, fut);

    auto now = Clock::now();

    auto status = fut.wait_for(Duration(0));

    while (status != std::future_status::ready || Clock::now() - now > timeout)
    {
        test_timer.expires_from_now(std::chrono::milliseconds(100));
        co_await test_timer.async_wait(asio::use_awaitable);
        status = fut.wait_for(Duration(0));
    }

    if (status == std::future_status::ready)
    {
        co_return fut.get();
    }
    else
    {
        throw TimeoutError("future timed out");
    }
}

} // namespace metricq
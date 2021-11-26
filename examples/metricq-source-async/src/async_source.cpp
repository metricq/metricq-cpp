// Copyright (c) 2018, ZIH, Technische Universitaet Dresden, Federal Republic of Germany
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
#include "async_source.hpp"

#include <metricq/logger/nitro.hpp>

#include <metricq/types.hpp>

#include <chrono>

#include <cmath>

using Log = metricq::logger::nitro::Log;

AsyncSource::AsyncSource(const std::string& server, const std::string& token,
                         metricq::Duration interval, const std::string& metric,
                         int messages_per_chunk)
: metricq::Source(token), signals_(io_service, SIGINT, SIGTERM), interval(interval),
  metric_(metric), messages_per_chunk_(messages_per_chunk)
{
    Log::debug() << "AsyncSource::AsyncSource() called";

    // Register signal handlers so that the daemon may be shut down.
    signals_.async_wait([this](auto, auto signal) {
        if (!signal)
        {
            return;
        }
        Log::info() << "Caught signal " << signal << ". Shutdown.";

        stop_requested_ = true;
    });

    metricq::co_spawn(io_service, connect(server), *this);
}

AsyncSource::~AsyncSource()
{
}

metricq::Awaitable<void> AsyncSource::on_source_config(const metricq::json&)
{
    Log::debug() << "AsyncSource::on_source_config() called";
    (*this)[metric_];

    co_return;
}

metricq::Awaitable<void> AsyncSource::on_source_ready()
{
    Log::debug() << "AsyncSource::on_source_ready() called";
    (*this)[metric_].metadata.unit("kittens");
    (*this)[metric_].metadata.rate(messages_per_chunk_ / (interval.count() * 1e-9));
    (*this)[metric_].metadata["color"] = "pink";
    (*this)[metric_].metadata["paws"] = 4;

    metricq::co_spawn(io_service, task(), *this);

    co_return;
}

void AsyncSource::on_error(const std::string& message)
{
    Log::debug() << "AsyncSource::on_error() called";
    Log::error() << "Shit hits the fan: " << message;
    signals_.cancel();
}

void AsyncSource::on_closed()
{
    Log::debug() << "AsyncSource::on_closed() called";
    signals_.cancel();
}

metricq::Awaitable<void> AsyncSource::task()
{
    auto& metric = (*this)[metric_];

    Log::info() << "dummy async source task started...";

    int sends_till_throw = 4;

    while (!stop_requested_)
    {
        if (!sends_till_throw--)
            throw std::runtime_error("Hello there!");

        co_await metricq::wait_for(interval);
        Log::debug() << "sending metrics...";
        auto current_time = metricq::Clock::now();
        metric.send({ current_time, 42 });
        metric.flush();
    }

    close();
}

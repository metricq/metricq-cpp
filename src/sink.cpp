// Copyright (c) 2018, ZIH,
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

#include <metricq/datachunk.pb.h>
#include <metricq/sink.hpp>
#include <metricq/types.hpp>

#include "log.hpp"
#include "util.hpp"

#include <amqpcpp.h>

#include <exception>
#include <iostream>

namespace metricq
{
Sink::Sink(const std::string& token, bool add_uuid) : DataClient(token, add_uuid)
{
}

Sink::~Sink()
{
}

void Sink::subscribe(const std::vector<std::string>& metrics)
{
    rpc("sink.subscribe",
        [this](const json& response) {
            this->sink_config(response);

            if (this->data_queue() != response.at("dataQueue"))
            {
                throw std::runtime_error("inconsistent sink dataQueue setting after subscription");
            }
        },
        { { "metrics", metrics }, { "metadata", true } });
}

void Sink::subscribe(const std::vector<std::string>& metrics, Duration expires)
{
    if (expires.count() <= 0)
    {
        throw std::runtime_error("Expires must be >0");
    }

    rpc("sink.subscribe",
        [this](const json& response) {
            this->sink_config(response);

            if (this->data_queue() != response.at("dataQueue"))
            {
                throw std::runtime_error("inconsistent sink dataQueue setting after subscription");
            }
        },
        { { "metrics", metrics },
          { "expires", std::chrono::duration_cast<std::chrono::duration<double>>(expires).count() },
          { "metadata", true } });
}

void Sink::sink_config(const json& config)
{
    data_config(config);

    data_queue(config.at("dataQueue").get<std::string>());

    update_metadata(config);

    setup_data_queue();
}

void Sink::setup_data_queue()
{
    if (is_data_queue_set_up_)
    {
        return;
    }

    // Ensure that we are not flooded by requests and forget to send out heartbeat
    // TODO configurable!
    data_channel_->setQos(400);
    assert(!data_queue().empty());

    data_channel_->declareQueue(data_queue(), AMQP::passive)
        .onSuccess(std::bind(&Sink::setup_data_consumer, this, std::placeholders::_1,
                             std::placeholders::_2, std::placeholders::_3));
}

void Sink::setup_data_consumer(const std::string& name, int message_count, int consumer_count)
{
    log::notice("setting up data queue, messages {}, consumers {}", message_count, consumer_count);

    // we do not tolerate other consumers
    if (consumer_count != 0)
    {
        log::warn("unexpected consumer count {} - are we not alone in the queue?", consumer_count);
    }

    auto message_cb = [this](const AMQP::Message& message, uint64_t delivery_tag,
                             bool redelivered) { on_data(message, delivery_tag, redelivered); };

    data_channel_->consume(name)
        .onReceived(message_cb)
        .onSuccess([this]() {
            this->is_data_queue_set_up_ = true;
            log::debug("sink data queue consume success");
        })
        .onError(debug_error_cb("sink data queue consume error"))
        .onFinalize([]() { log::info("sink data queue consume finalize"); });
}

void Sink::update_metadata(const json& config)
{
    if (config.count("metrics"))
    {
        const auto& metrics_metadata = config.at("metrics");
        for (auto it = metrics_metadata.begin(); it != metrics_metadata.end(); ++it)
        {
            if (it.value().is_object())
            {
                metadata_.emplace(it.key(), it.value());
            }
            else
            {
                log::warn("missing metadata for metric {}", it.key());
            }
        }
    }
}

void Sink::on_data(const AMQP::Message& message, uint64_t delivery_tag, bool redelivered)
{
    (void)redelivered;
    const auto& metric_name = message.routingkey();
    auto message_body = std::string(message.body(), message.bodySize());
    data_chunk_.Clear();
    data_chunk_.ParseFromString(message_body);
    try
    {
        on_data(metric_name, data_chunk_);
        data_channel_->ack(delivery_tag);
    }
    catch (std::exception& ex)
    {
        log::fatal("sink data callback failed for metric {}: {}", metric_name, ex.what());
        throw;
    }
}

void Sink::on_data(const std::string& id, const DataChunk& data_chunk)
{
    for (auto tv : data_chunk)
    {
        on_data(id, tv);
    }
}

void Sink::on_data(const std::string&, TimeValue)
{
    log::fatal("unhandled TimeValue data, implementation error.");
    std::abort();
}

void Sink::data_queue(const std::string& name)
{
    if (name.empty())
    {
        throw std::runtime_error("Trying to set data_queue name to empty string.");
    }

    if (data_queue_.empty())
    {
        assert(!is_data_queue_set_up_);
        data_queue_ = name;
        return;
    }

    if (data_queue_ == name)
    {
        return;
    }

    if (is_data_queue_set_up_)
    {
        log::error("Requested to change data_queue to '{}', but the data_queue '{}' is already "
                   "setup for consumption.",
                   name, data_queue_);
        throw std::runtime_error(
            "Trying to change data_queue, but data consumption has already started.");
    }

    log::warn("Requested to change data_queue to '{}', but the data_queue '{}' is already set"
              " (but not consuming).",
              name, data_queue_);

    data_queue_ = name;
}

} // namespace metricq

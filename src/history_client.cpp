// Copyright (c) 2019, ZIH,
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

#include <metricq/chrono.hpp>
#include <metricq/history_client.hpp>
#include <metricq/types.hpp>
#include <metricq/utils.hpp>

#include "connection_handler.hpp"
#include "log.hpp"
#include "util.hpp"

#include <functional>

using namespace std::placeholders;

namespace metricq
{
HistoryClient::HistoryClient(const std::string& token, bool add_uuid) : Connection(token, add_uuid)
{
    register_rpc_callback("discover",
                          [this](auto& request) -> Awaitable<json>
                          { co_return handle_discover_rpc(request); });
}

HistoryClient::~HistoryClient() = default;

void HistoryClient::setup_history_queue()
{
    assert(history_channel_);
    history_channel_->declareQueue(history_queue_, AMQP::passive)
        .onSuccess([this](const auto& name, auto messages, auto consumers)
                   { this->setup_history_consumer(name, messages, consumers); });
}

void HistoryClient::setup_history_consumer(const std::string& name, int message_count,
                                           int consumer_count)
{
    log::notice("setting up history queue, messages {}, consumers {}", message_count,
                consumer_count);

    auto message_cb = [this](const AMQP::Message& message, uint64_t deliveryTag, bool redelivered)
    {
        (void)redelivered;

        on_history_response(message);
        history_channel_->ack(deliveryTag);
    };

    history_channel_->consume(name)
        .onReceived(message_cb)
        .onSuccess(debug_success_cb("history queue consume success"))
        .onError(debug_error_cb("history queue consume error"))
        .onFinalize([]() { log::info("history queue consume finalize"); });
}

Awaitable<std::variant<HistoryResponseValueView, HistoryResponseAggregateView>>
HistoryClient::history_data_request(const std::string& metric, TimePoint begin, TimePoint end,
                                    Duration interval, HistoryRequest type, Duration timeout)
{
    HistoryRequest request;
    request.set_start_time(begin.time_since_epoch().count());
    request.set_end_time(end.time_since_epoch().count());
    request.set_interval_max(interval.count());
    // TODO expose the request type to the user. For now it's fine.
    request.set_type(HistoryRequest::FLEX_TIMELINE);

    auto correlation_id = std::string("metricq-history-") + token() + "-" + uuid();

    std::string message = request.SerializeAsString();
    AMQP::Envelope envelope(message.data(), message.size());
    envelope.setCorrelationID(correlation_id);
    envelope.setContentType("application/json");
    envelope.setReplyTo(history_queue_);

    response_promises_.emplace(correlation_id, io_service);
    auto future = response_promises_.at(correlation_id).get_future();

    auto cleanup_promise =
        finally([this, &correlation_id]() { response_promises_.erase(correlation_id); });

    history_channel_->publish(history_exchange_, metric, envelope);

    auto response_data = co_await future.get(timeout);

    HistoryResponse response;
    response.ParseFromString(response_data);

    if (!response.error().empty())
    {
        throw HistoryRequestError(response.error());
    }
    else
    {
        if (response.value_size() > 0)
        {
            co_return HistoryResponseValueView(response);
        }
        else
        {
            co_return HistoryResponseAggregateView(response);
        }
    }
}

std::string HistoryClient::history_request(const std::string& id, TimePoint begin, TimePoint end,
                                           Duration interval)
{
    HistoryRequest request;
    request.set_start_time(begin.time_since_epoch().count());
    request.set_end_time(end.time_since_epoch().count());
    request.set_interval_max(interval.count());
    // TODO expose the request type to the user. For now it's fine.
    request.set_type(HistoryRequest::FLEX_TIMELINE);

    auto correlation_id = std::string("metricq-history-") + token() + "-" + uuid();

    std::string message = request.SerializeAsString();
    AMQP::Envelope envelope(message.data(), message.size());
    envelope.setCorrelationID(correlation_id);
    envelope.setContentType("application/json");
    envelope.setReplyTo(history_queue_);

    history_channel_->publish(history_exchange_, id, envelope);

    return correlation_id;
}

void HistoryClient::on_history_response(const AMQP::Message& incoming_message)
{
    if (auto it = response_promises_.find(incoming_message.correlationID());
        it != response_promises_.end())
    {
        it->second.set_result(std::string(incoming_message.body(), incoming_message.bodySize()));
    }
    else
    {
        history_response_.Clear();
        history_response_.ParseFromArray(incoming_message.body(), incoming_message.bodySize());

        on_history_response(incoming_message.correlationID(), history_response_);
    }
}

Awaitable<void> HistoryClient::on_connected()
{
    auto response = co_await rpc("history.register");

    co_await config(response);
}

Awaitable<void> HistoryClient::config(const metricq::json& config)
{
    co_await history_config(config);

    if (!history_exchange_.empty() &&
        config["historyExchange"].get<std::string>() != history_exchange_)
    {
        log::fatal("changing historyExchange on the fly is not currently supported");
        std::abort();
    }

    history_exchange_ = config["historyExchange"].get<std::string>();
    history_queue_ = config["historyQueue"].get<std::string>();

    on_history_config(config["config"]);

    setup_history_queue();

    on_history_ready();
}

Awaitable<void> HistoryClient::on_history_channel_ready()
{
    co_return;
}

Awaitable<void> HistoryClient::history_config(const json& config)
{
    AMQP::Address new_data_server_address =
        derive_address(config["dataServerAddress"].get<std::string>());
    log::debug("start parsing history config");
    if (history_connection_)
    {
        log::debug("history connection already exists");
        if (new_data_server_address != data_server_address_)
        {
            log::fatal("changing dataServerAddress on the fly is not currently supported");
            std::abort();
        }
        // We should be fine, connection and channel is already setup and the same
        co_return;
    }

    data_server_address_ = new_data_server_address;

    log::debug("opening history connection to {}", *data_server_address_);
    if (data_server_address_->secure())
    {
        history_connection_ =
            std::make_unique<SSLConnectionHandler>(io_service, "Hist connection", token());
    }
    else
    {
        history_connection_ =
            std::make_unique<PlainConnectionHandler>(io_service, "Hist connection", token());
    }

    co_await history_connection_->connect(*data_server_address_);
    history_channel_ = history_connection_->make_channel();
    history_channel_->onReady(
        [this]()
        {
            log::debug("history_channel ready");
            co_spawn(io_service, on_history_channel_ready(), *this);
        });
    history_channel_->onError(debug_error_cb("history channel error"));
}

Awaitable<void> HistoryClient::close()
{
    // Close data connection first, then close the management connection
    if (!history_connection_)
    {
        log::debug("closing HistoryClient, no history_connection up yet");
        co_await Connection::close();
        co_return;
    }

    // don't let the data_connection::close() call the on_closed() of this class, the close of the
    // management connection shall call on_closed().
    history_connection_->close(
        [this]()
        {
            log::info("closed history_connection");
            co_spawn(io_service, Connection::close(), *this);
        });
}

void HistoryClient::on_history_response(const std::string& id, const HistoryResponse& response)
{
    if (!response.error().empty())
    {
        on_history_response(id, response.error());
    }
    else
    {
        if (response.value_size() > 0)
        {
            on_history_response(id, HistoryResponseValueView(response));
        }
        else
        {
            on_history_response(id, HistoryResponseAggregateView(response));
        }
    }
}

} // namespace metricq

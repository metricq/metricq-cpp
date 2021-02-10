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

#include <metricq/connection.hpp>
#include <metricq/exception.hpp>
#include <metricq/json.hpp>
#include <metricq/version.hpp>

#include "connection_handler.hpp"
#include "log.hpp"
#include "util.hpp"

#include <metricq/logger.hpp>
#include <metricq/utils.hpp>

#include <amqpcpp.h>

#include <iostream>
#include <memory>
#include <string>

namespace metricq
{
static std::string make_token(const std::string& token, bool add_uuid)
{
    if (add_uuid)
    {
        return token + "." + uuid();
    }
    return token;
}

Connection::Connection(const std::string& connection_token, bool add_uuid,
                       std::size_t concurrency_hint)
: io_service(concurrency_hint), connection_token_(make_token(connection_token, add_uuid))
{
}

Connection::~Connection()
{
}

void Connection::main_loop()
{
    io_service.run();
}

void Connection::connect(const std::string& server_address)
{
    management_address_ = server_address;

    log::info("connecting to management server: {}", *management_address_);

    if (server_address.substr(0, 5) == "amqps")
    {
        management_connection_ =
            std::make_unique<SSLConnectionHandler>(io_service, "Mgmt connection");
    }
    else
    {
        management_connection_ =
            std::make_unique<PlainConnectionHandler>(io_service, "Mgmt connection");
    }
    management_connection_->set_error_callback(
        [this](const auto& message) { this->on_error(message); });
    management_connection_->set_close_callback([this]() { this->on_closed(); });

    management_connection_->connect(*management_address_);
    management_channel_ = management_connection_->make_channel();
    management_channel_->onReady(debug_success_cb("management channel ready"));
    management_channel_->onError(debug_error_cb("management channel error"));

    management_client_queue_ = connection_token_ + "-rpc";

    management_channel_->declareQueue(management_client_queue_, AMQP::exclusive)
        .onSuccess([this](const std::string& name, [[maybe_unused]] int msgcount,
                          [[maybe_unused]] int consumercount) {
            management_channel_
                ->bindQueue(management_broadcast_exchange_, management_client_queue_, "#")
                .onError(debug_error_cb("error binding management queue to broadcast exchange"))
                .onSuccess([this, name]() {
                    management_channel_->consume(name)
                        .onReceived([this](const AMQP::Message& message, uint64_t delivery_tag,
                                           bool redelivered) {
                            handle_management_message(message, delivery_tag, redelivered);
                        })
                        .onSuccess([this]() { on_connected(); })
                        .onError(debug_error_cb("management consume error"));
                });
        });
}

void Connection::register_rpc_callback(const std::string& function, RPCCallback cb)
{
    auto ret = rpc_callbacks_.emplace(function, std::move(cb));
    if (!ret.second)
    {
        log::error("trying to register RPC callback that is already registered: {}", function);
        throw std::invalid_argument("trying to register RPC callback that is already registered");
    }
}

void Connection::register_rpc_response_callback(const std::string& correlation_id,
                                                RPCResponseCallback callback, Duration timeout)
{
    auto ret = rpc_response_callbacks_.emplace(
        std::piecewise_construct, std::make_tuple(correlation_id),
        std::make_tuple(std::ref(io_service), std::move(callback), timeout));
    if (!ret.second)
    {
        log::error("trying to register RPC response callback that is already registered: {}",
                   correlation_id);
        throw std::invalid_argument(
            "trying to register RPC response callback that is already registered");
    }
}

std::string Connection::prepare_message(const std::string& function, json payload)
{
    if (payload.count("function"))
    {
        throw std::invalid_argument("Function was already set in payload.");
    }

    payload["function"] = function;
    return payload.dump();
}

std::unique_ptr<AMQP::Envelope> Connection::prepare_rpc_envelope(const std::string& message)
{
    auto envelope = std::make_unique<AMQP::Envelope>(message.data(), message.size());

    auto correlation_id = std::string("metricq-rpc-") + connection_token_ + "-" + uuid();

    envelope->setCorrelationID(std::move(correlation_id));
    envelope->setAppID(connection_token_);
    envelope->setContentType("application/json");

    assert(!management_client_queue_.empty());
    envelope->setReplyTo(management_client_queue_);

    return envelope;
}

void Connection::rpc(const std::string& function, RPCResponseCallback callback, json payload,
                     Duration timeout)
{
    log::debug("sending rpc: {}", function);

    auto message = prepare_message(function, std::move(payload));
    auto envelope = prepare_rpc_envelope(message);

    register_rpc_response_callback(envelope->correlationID(), std::move(callback), timeout);

    management_channel_->publish(management_exchange_, function, *envelope);
}

void Connection::handle_management_message(const AMQP::Message& incoming_message,
                                           uint64_t deliveryTag, [[maybe_unused]] bool redelivered)
{
    const std::string content_str(incoming_message.body(),
                                  static_cast<size_t>(incoming_message.bodySize()));
    log::debug("Management message received: {}", truncate_string(content_str, 100));

    auto content = json::parse(content_str);

    auto acknowledge = finally([this, deliveryTag]() { management_channel_->ack(deliveryTag); });

    if (auto it = rpc_response_callbacks_.find(incoming_message.correlationID());
        it != rpc_response_callbacks_.end())
    {
        // Incoming message is a RPC-response, call the response handler
        if (content.count("error"))
        {
            log::error("rpc failed: {}. stopping", content["error"].get<std::string>());
            acknowledge.invoke();
            rpc_response_callbacks_.clear();
            stop();
            return;
        }

        try
        {
            // This may be debatable... but right now we don't want to bother the caller
            // with error handling. We may need it in the future.
            it->second(content);
        }
        catch (json::parse_error& e)
        {
            log::error("error in rpc response handling {}: parsing message: {}\n{}",
                       incoming_message.correlationID(), e.what(), content_str);
            throw RPCError();
        }
        catch (json::type_error& e)
        {
            log::error("error in rpc response handling {}: accessing parameter: {}\n{}",
                       incoming_message.correlationID(), e.what(), content_str);
            throw RPCError();
        }
        catch (std::exception& e)
        {
            log::error("error in rpc response handling {}: {}\n{}",
                       incoming_message.correlationID(), e.what(), content_str);
            throw RPCError();
        }

        // we must search again because the handler might have invalidated the iterator by starting
        // another RPC using the rpc() method. That would insert a new entry into the
        // rpc_response_callbacks_ map and, thus, potentially invalidating the iterator.
        it = rpc_response_callbacks_.find(incoming_message.correlationID());
        if (it == rpc_response_callbacks_.end())
        {
            log::error("error in rpc response handling {}: response callback vanished",
                       incoming_message.correlationID());
            throw RPCError();
        }
        rpc_response_callbacks_.erase(it);
        return;
    }

    if (content.count("function") != 1)
    {
        log::error("error in rpc: no function but also no response callback: {}\n{}",
                   incoming_message.correlationID(), content_str);
        throw RPCError();
    }

    auto function = content.at("function").get<std::string>();

    if (auto it = rpc_callbacks_.find(function); it != rpc_callbacks_.end())
    {
        log::debug("management rpc call received: {}", truncate_string(content_str,100));
        // incoming message is a RPC-call

        try
        {
            auto response = it->second(content);
            std::string reply_message = response.dump();
            AMQP::Envelope envelope(reply_message.data(), reply_message.size());
            envelope.setCorrelationID(incoming_message.correlationID());
            envelope.setAppID(connection_token_);
            envelope.setContentType("application/json");

            log::debug("sending reply '{}' to {} / {}", reply_message, incoming_message.replyTo(),
                       incoming_message.correlationID());
            management_channel_->publish("", incoming_message.replyTo(), envelope);
        }
        catch (json::parse_error& e)
        {
            log::error("error in rpc handling {}: parsing message: {}\n{}", function, e.what(),
                       content_str);
            throw RPCError();
        }
        catch (json::type_error& e)
        {
            log::error("error in rpc handling {}: accessing parameter: {}\n{}", function, e.what(),
                       content_str);
            throw RPCError();
        }
        catch (std::exception& e)
        {
            log::error("error in rpc handling {}: {}\n{}", function, e.what(), content_str);
            throw RPCError();
        }
        return;
    }

    log::warn("no rpc callback registered for function: {}\n{}", function, content_str);
}

AMQP::Address Connection::derive_address(const std::string& address_str)
{
    std::string vhost_prefix = "vhost:";
    if (address_str.rfind(vhost_prefix, 0) == 0) // .startswith, cries in C++20
    {
        auto vhost = address_str.substr(vhost_prefix.length());
        return AMQP::Address(management_address_->hostname(), management_address_->port(),
                             management_address_->login(), vhost, management_address_->secure());
    }
    else
    {
        AMQP::Address address(address_str);
        return AMQP::Address(address.hostname(), address.port(), management_address_->login(),
                             address.vhost(), address.secure());
    }
}

void Connection::close()
{
    if (!management_connection_)
    {
        log::debug("closing connection, no management_connection up yet");
        on_closed();
        return;
    }
    management_connection_->close();
}

void Connection::stop()
{
    log::debug("Stop requested. Closing connection.");
    close();
    // the io_service will stop itself once all connections are closed
}

json Connection::handle_discover_rpc(const json&)
{
    auto current_time = Clock::now();
    auto uptime =
        std::chrono::duration_cast<std::chrono::duration<double>>(current_time - starting_time_)
            .count();

    return { { "alive", true },
             { "currentTime", Clock::format_iso(current_time) },
             { "startingTime", Clock::format_iso(starting_time_) },
             { "uptime", uptime },
             { "metricqVersion", metricq::version() } };
}

} // namespace metricq

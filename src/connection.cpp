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
#include "raise.hpp"
#include "util.hpp"

#include <metricq/logger.hpp>
#include <metricq/utils.hpp>

#include <asio/ip/host_name.hpp>

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

Awaitable<void> Connection::connect(const std::string& server_address)
{
    management_address_ = server_address;

    log::info("connecting to management server: {}", redact_address_login(*management_address_));

    if (server_address.substr(0, 5) == "amqps")
    {
        management_connection_ = std::make_unique<SSLConnectionHandler>(
            io_service, "Mgmt connection", connection_token_);
    }
    else
    {
        management_connection_ = std::make_unique<PlainConnectionHandler>(
            io_service, "Mgmt connection", connection_token_);
    }

    management_connection_->set_error_callback(
        [this](const auto& message) { this->on_error(message); });
    management_connection_->set_close_callback([this]() { this->on_closed(); });

    co_await management_connection_->connect(*management_address_);

    management_channel_ = management_connection_->make_channel();
    management_channel_->onReady(debug_success_cb("management channel ready"));
    management_channel_->onError([](auto message) {
        log::error("management channel error: {}", message);
        throw std::runtime_error(message);
    });

    management_client_queue_ = connection_token_ + "-rpc";

    management_channel_->declareQueue(management_client_queue_, AMQP::exclusive)
        .onSuccess([this](const std::string& name, [[maybe_unused]] int msgcount,
                          [[maybe_unused]] int consumercount) {
            management_channel_
                ->bindQueue(management_broadcast_exchange_, management_client_queue_, "#")
                .onError([name](auto message) {
                    log::error("error binding management queue to broadcast exchange: {}", message);
                    throw std::runtime_error(
                        "Couldn't bind the management queue to the broadcast exchange");
                })
                .onSuccess([this, name]() {
                    management_channel_->consume(name)
                        .onReceived([this](const AMQP::Message& message, uint64_t delivery_tag,
                                           bool redelivered) {
                            handle_management_message(message, delivery_tag, redelivered);
                        })
                        .onSuccess(
                            [this]() { metricq::co_spawn(io_service, on_connected(), *this); })
                        .onError(debug_error_cb("management consume error"));
                });
        });
}

void Connection::register_rpc_callback(const std::string& function, AwaitableRPCCallback cb)
{
    auto ret = rpc_callbacks_.emplace(function, std::move(cb));
    if (!ret.second)
    {
        log::error("trying to register RPC callback that is already registered: {}", function);
        throw std::invalid_argument("trying to register RPC callback that is already registered");
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

Awaitable<json> Connection::rpc(const std::string& function, json payload, Duration timeout)
{

    auto message = prepare_message(function, std::move(payload));
    auto envelope = prepare_rpc_envelope(message);

    log::debug("sending rpc: {} : {}", envelope->correlationID(), function);

    rpc_promises_.emplace(envelope->correlationID(), io_service);

    auto future = rpc_promises_.at(envelope->correlationID()).get_future();
    auto cleanup_promise =
        finally([this, &envelope]() { rpc_promises_.erase(envelope->correlationID()); });

    management_channel_->publish(management_exchange_, function, *envelope);

    try
    {
        co_return co_await future.get(timeout);
    }
    catch (TimeoutError&)
    {
        raise<RPCError>("{} rpc timed out.", function);
    }
}

void Connection::handle_management_message(const AMQP::Message& incoming_message,
                                           uint64_t deliveryTag, [[maybe_unused]] bool redelivered)
{
    const std::string content_str(incoming_message.body(),
                                  static_cast<size_t>(incoming_message.bodySize()));

    // log::debug("Management message received: {}", truncate_string(content_str, 100));
    log::debug("Management message received: {} : {}", incoming_message.correlationID(),
               content_str);

    auto content = json::parse(content_str);

    //    auto acknowledge = finally([this, deliveryTag]() { management_channel_->ack(deliveryTag);
    //    });

    if (auto it = rpc_promises_.find(incoming_message.correlationID()); it != rpc_promises_.end())
    {
        // Incoming message is an RPC-response, set the future
        if (content.count("error"))
        {
            log::error("rpc failed: {}.", content["error"].get<std::string>());
            it->second.set_exception(std::make_exception_ptr(RPCError(content["error"])));
        }
        else
        {
            it->second.set_value(content);
        }

        management_channel_->ack(deliveryTag);

        return;
    }

    if (content.count("function") != 1)
    {
        log::error("error in rpc: ignoring message that is neither a request nor an expected "
                   "response: {}\n{}",
                   incoming_message.correlationID(), content_str);

        // TODO Why not reject such messages?
        management_channel_->ack(deliveryTag);

        return;
    }

    auto function = content.at("function").get<std::string>();

    if (auto it = rpc_callbacks_.find(function); it != rpc_callbacks_.end())
    {
        log::debug("management rpc call received: {}", truncate_string(content_str, 100));
        // incoming message is a RPC-call

        management_channel_->ack(deliveryTag);

        co_spawn(
            io_service,
            [this, correlation_id = incoming_message.correlationID(),
             reply_to = incoming_message.replyTo(), content, content_str, function,
             callback = it->second]() -> Awaitable<void> {
                try
                {
                    auto response = co_await callback(content);
                    std::string reply_message = response.dump();
                    AMQP::Envelope envelope(reply_message.data(), reply_message.size());
                    envelope.setCorrelationID(correlation_id);
                    envelope.setAppID(connection_token_);
                    envelope.setContentType("application/json");

                    log::debug("sending reply '{}' to {} / {}", reply_message, reply_to,
                               correlation_id);
                    management_channel_->publish("", reply_to, envelope);
                }
                catch (json::parse_error& e)
                {
                    log::error("error in rpc handling {}: parsing message: {}\n{}", function,
                               e.what(), content_str);
                    throw RPCError();
                }
                catch (json::type_error& e)
                {
                    log::error("error in rpc handling {}: accessing parameter: {}\n{}", function,
                               e.what(), content_str);
                    throw RPCError();
                }
                catch (std::exception& e)
                {
                    log::error("error in rpc handling {}: {}\n{}", function, e.what(), content_str);
                    throw RPCError();
                }
            },
            *this);
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

void Connection::on_unhandled_exception(const std::exception& e)
{
    log::fatal(e.what());

    // As we are in the error handler, we shouldn't register ourself as
    // error handler again.
    co_spawn(io_service, close());
}

Awaitable<void> Connection::close()
{
    if (!management_connection_)
    {
        log::debug("closing connection, no management_connection up yet");
        on_closed();
        co_return;
    }

    // for (auto& promise : this->rpc_promises_)
    // {
    //     log::debug("crippling remaining promise");
    //     promise.second.set_exception(std::make_exception_ptr(ConnectionClosedError("WHAAAAAA")));
    // }

    // co_await wait_for(std::chrono::milliseconds(10));

    AsyncPromise<void> closed(io_service);

    management_connection_->close([this, &closed]() {
        // for (auto& promise : this->rpc_promises_)
        // {
        //     log::debug("crippling remaining promise");
        //     // promise.second.set_exception(
        //     //     std::make_exception_ptr(ConnectionClosedError("WHAAAAAA")));

        //     promise.second.cancel();
        // }
        log::info("closed management connection");
        on_closed();
        closed.set_done();
    });

    auto future = closed.get_future();
    co_await future.get();
}

void Connection::stop()
{
    log::debug("Stop requested. Closing connection.");
    co_spawn(io_service, close(), *this);
    // the io_service will stop itself once all connections are closed
}

std::string Connection::version() const
{
    return {};
}

json Connection::handle_discover_rpc(const json&)
{
    auto current_time = Clock::now();
    auto uptime =
        std::chrono::duration_cast<std::chrono::duration<double>>(current_time - starting_time_)
            .count();

    json response = { { "alive", true },
                      { "currentTime", Clock::format_iso(current_time) },
                      { "startingTime", Clock::format_iso(starting_time_) },
                      { "uptime", uptime },
                      { "metricqVersion", metricq::version() } };

    if (auto version = this->version(); !version.empty())
    {
        response["version"] = version;
    }

    try
    {
        auto hostname = asio::ip::host_name();
        response["hostname"] = hostname;
    }
    catch (asio::system_error& e)
    {
        log::error("Couldn't get hostname for system: {}", e.what());
    }

    return response;
}

} // namespace metricq

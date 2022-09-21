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

#include "connection_handler.hpp"
#include "log.hpp"

#ifdef METRICQ_SSL_SKIP_VERIFY
extern "C"
{
#include <openssl/x509.h>
}
#endif

namespace metricq
{

void QueuedBuffer::emplace(const char* ptr, std::size_t size)
{
    log::trace("Added new buffer ({:x}) of size {}.", *reinterpret_cast<std::size_t*>(&ptr), size);

    // check if this fits at the back of the last queued buffer
    buffers_.emplace(ptr, ptr + size);
}

void QueuedBuffer::consume(std::size_t consumed_bytes)
{
    assert(!empty());

    if (buffers_.front().size() == offset_ + consumed_bytes)
    {
        log::trace("Current buffer is empty, poping it of.");
        offset_ = 0;
        buffers_.pop();
    }
    else
    {
        offset_ += consumed_bytes;
        log::trace("Current buffer isn't empty yet, remaining: {}",
                   buffers_.front().size() - offset_);
    }
}

AsioConnectionHandler::AsioConnectionHandler(asio::io_service& io_service, const std::string& name,
                                             const std::string& token)
: io_context_(io_service), heartbeat_timer_(io_service),
  heartbeat_interval_(std::chrono::seconds(0)), resolver_(io_service), name_(name), token_(token)
{
}

PlainConnectionHandler::PlainConnectionHandler(asio::io_service& io_service,
                                               const std::string& name, const std::string& token)
: AsioConnectionHandler(io_service, name, token), socket_(io_service)
{
    log::debug("[{}] Using plaintext connection.", name_);
}

SSLConnectionHandler::SSLConnectionHandler(asio::io_service& io_service, const std::string& name,
                                           const std::string& token)
: AsioConnectionHandler(io_service, name, token), ssl_context_(asio::ssl::context::tls),
  socket_(io_service, ssl_context_)
{
    log::debug("[{}] Using SSL-secured connection.", name_);

    // Create a context that uses the default paths for finding CA certificates.
    ssl_context_.set_default_verify_paths();
    socket_.set_verify_mode(asio::ssl::verify_peer);
    ssl_context_.set_options(asio::ssl::context::default_workarounds |
                             asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3 |
                             asio::ssl::context::tlsv12_client);
}

Awaitable<void> AsioConnectionHandler::connect(const AMQP::Address& address)
{
    assert(!underlying_socket().is_open());
    assert(!connection_);

    address_ = address;
    connection_ = std::make_unique<AMQP::Connection>(this, address.login(), address.vhost());

    asio::ip::tcp::resolver::query query(address.hostname(), std::to_string(address.port()));
    asio::ip::tcp::resolver::results_type endpoint_iterator;

    try
    {
        endpoint_iterator = co_await resolver_.async_resolve(query, use_awaitable);
    }
    catch (std::exception& e)
    {
        log::error("[{}] failed to resolve hostname {}: {}", name_, address.hostname(), e.what());
        this->onError("resolve failed");
        co_return;
    }

    for (auto it = endpoint_iterator; it != decltype(endpoint_iterator)(); ++it)
    {
        log::debug("[{}] resolved {} to {}", name_, address.hostname(), it->endpoint());
    }

    try
    {
        auto successful_endpoint =
            co_await asio::async_connect(this->underlying_socket(), endpoint_iterator,
                                         decltype(endpoint_iterator)(), use_awaitable);

        log::debug("[{}] Established connection to {} at {}", name_,
                   successful_endpoint->host_name(), successful_endpoint->endpoint());

        co_await this->handshake(successful_endpoint->host_name());
    }
    catch (std::exception& e)
    {
        log::error("[{}] Failed to connect to: {}", name_, e.what());
        this->onError("Connect failed");
        co_return;
    }
}

metricq::Awaitable<void> PlainConnectionHandler::handshake(const std::string& hostname)
{
    (void)hostname;

    using asio::experimental::awaitable_operators::operator||;

    this->flush();
    this->read();

    co_return;
}

metricq::Awaitable<void> SSLConnectionHandler::handshake(const std::string& hostname)
{
#ifdef METRICQ_SSL_SKIP_VERIFY
    // Building without SSL verification; DO NOT USE THIS IN PRODUCTION!
    // This code will skip ANY SSL certificate verification.
    socket_.set_verify_callback(
        [](bool, asio::ssl::verify_context& ctx)
        {
            char subject_name[256];
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);

            log::debug("[{}] Visiting certificate for subject: {}", name_, subject_name);

            log::warn("[{}] Skipping certificate verification.", name_);
            return true;
        });
#else
    // This code will do proper SSL certification as described in RFC2818
    socket_.set_verify_callback(asio::ssl::rfc2818_verification(hostname));
#endif

    try
    {
        co_await socket_.async_handshake(asio::ssl::stream_base::client, use_awaitable);
    }
    catch (std::exception& e)
    {
        log::error("[{}] Failed to SSL handshake to: {}", name_, e.what());
        this->onError("SSL handshake failed");
        co_return;
    }

    log::debug("[{}] SSL handshake was successful.", name_);

    using asio::experimental::awaitable_operators::operator||;

    this->flush();
    this->read();
}

uint16_t AsioConnectionHandler::onNegotiate(AMQP::Connection* connection, uint16_t timeout)
{
    (void)connection;

    log::debug("[{}] Negotiated heartbeat interval to {} seconds", name_, timeout);

    // According to https://www.rabbitmq.com/heartbeats.html we actually get the timeout here
    // and we should send a heartbeat every timeout/2. I guess this is an issue in AMQP-CPP
    heartbeat_interval_ = std::chrono::seconds(timeout / 2);

    // We don't need to setup the heartbeat timer here. The library will report back our
    // returned timeout value to the server, which inevitable will trigger a send operation,
    // which will setup the heartbeat timer once it was completed. So we could setup the timer
    // here, but it would be canceled anyway.

    return timeout;
}

void AsioConnectionHandler::onHeartbeat(AMQP::Connection* connection)
{
    (void)connection;

    log::trace("[{}] Received heartbeat from server", name_);
}

void AsioConnectionHandler::onProperties(AMQP::Connection* connection, const AMQP::Table& server,
                                         AMQP::Table& client)
{
    // make sure compilers dont complaint about unused parameters
    (void)connection;
    (void)server;

    client.set("connection_name", name_ + std::string(" ") + token_);
}

/**
 *  Method that is called by the AMQP library every time it has data
 *  available that should be sent to RabbitMQ.
 *  @param  connection  pointer to the main connection object
 *  @param  data        memory buffer with the data that should be sent to RabbitMQ
 *  @param  size        size of the buffer
 */
void AsioConnectionHandler::onData(AMQP::Connection* connection, const char* data, size_t size)
{
    (void)connection;

    send_buffers_.emplace(data, size);
    flush();
}

/**
 *  Method that is called by the AMQP library when the login attempt
 *  succeeded. After this method has been called, the connection is ready
 *  to use.
 *  @param  connection      The connection that can now be used
 */
void AsioConnectionHandler::onReady(AMQP::Connection* connection)
{
    (void)connection;
    log::debug("[{}] ConnectionHandler::onReady", name_);
}

void AsioConnectionHandler::on_unhandled_exception(const std::exception& e)
{
    onError(e.what());
}

/**
 *  Method that is called by the AMQP library when a fatal error occurs
 *  on the connection, for example because data received from RabbitMQ
 *  could not be recognized.
 *  @param  connection      The connection on which the error occurred
 *  @param  message         A human readable error message
 */
void AsioConnectionHandler::onError(AMQP::Connection* connection, const char* message)
{
    (void)connection;
    log::debug("[{}] ConnectionHandler::onError: {}", name_, message);
    if (error_callback_)
    {
        error_callback_(message);
    }

    underlying_socket().close();

    // We make a hard cut: Just throw the shit out of here.
    throw std::runtime_error(std::string("ConnectionHandler::onError: ") + message);

    // TODO actually implement reconnect
    /* NOTE to the poor soul, who will implement a robust connection:
     * Please be aware that there exists the possible situation that a close is requested, but
     * after that someone tries to still send a message over the soon to be closed connection
     * (Think of a async handler on another thread or some shit like that). This means that in
     * the onClosed() member function, there wil be the situation that send_buffers_ aren't
     * empty. As the send task is asynchronously dispatched, I can't do shit about it. But
     * because the socket will be closed the flush handler will error out and thus is going to
     * come here. In the end, you could trigger a reconnect AFTER the connection was asked to
     * close itself! *Wierd* இ௰இ)
     *
     * ps: My condolences that you ended up implementing this.
     */

    // reconnect_timer_.expires_from_now(std::chrono::seconds(3));
    // reconnect_timer_.async_wait([this](const auto& error) {
    //     if (error)
    //     {
    //         log::error("reconnect timer failed: {}", error.message());
    //         return;
    //     }
    //
    //     this->send_buffers_.clear();
    //     this->recv_buffer_.consume(this->recv_buffer_.size());
    //     this->flush_in_progress_ = false;
    //     this->connection_.reset();
    //     this->heartbeat_timer_.cancel();
    //
    //     this->connect(*this->address_);
    // });
}

/**
 *  Method that is called when the connection was closed. This is the
 *  counter part of a call to Connection::close() and it confirms that the
 *  AMQP connection was correctly closed.
 *
 *  @param  connection      The connection that was closed and that is now unusable
 */
void AsioConnectionHandler::onClosed(AMQP::Connection* connection)
{
    (void)connection;
    log::debug("[{}] ConnectionHandler::onClosed", name_);

    // Technically, there is a ssl_shutdown method for the stream.
    // But, it seems that we don't have to do that (⊙.☉)7
    // If we try, the shutdown errors with a "connection reset by peer"
    // ... instead we're just closing the socket ¯\_(ツ)_/¯
    underlying_socket().close();

    // cancel the heartbeat timer
    heartbeat_timer_.cancel();

    // we co_spawned the read as its own thread of execution. Time to shoot it.

    // check if send_buffers are empty, this would be strange, because that means that AMQP-CPP
    // tried to sent something, after it sent the close frame.
    if (!send_buffers_.empty())
    {
        // Coming here means, we still have dispatched tasks somewhere, which will soon try to
        // write something on the socket. However, that socket was closed from us a few lines
        // above.

        // we can't clear that buffer now, someone else will try to send it over the closed
        // socket. (With someone else I mean future-me). But this will end up in the error
        // handler, which will throw. Good luck with that. Better try not to create this
        // situation in the first place

        // I can't do shit here:
        log::warn("[{}] During the close of the connection, there is still data to send in the "
                  "buffers. This will blow up soon. I told ya so.",
                  name_);
    }

    // this technically invalidates all existing channel objects. Those objects are the hard
    // part for a robust connection
    connection_.reset();

    if (close_callback_)
    {
        close_callback_();
    }
}

bool AsioConnectionHandler::close()
{
    if (!connection_)
    {
        return false;
    }
    return connection_->close();
}

void AsioConnectionHandler::read()
{
    co_spawn(io_context_, do_read(), *this);
}

Awaitable<void> AsioConnectionHandler::do_read()
{
    while (this->underlying_socket().is_open())
    {
        try
        {
            assert(this->connection_);

            auto received_bytes = co_await this->async_read_some();

            log::trace(
                "[{}] Successfully received {} bytes through the socket. Waiting for at least "
                "{} bytes.",
                name_, received_bytes, connection_->expected());

            this->recv_buffer_.commit(received_bytes);

            if (this->recv_buffer_.size() >= connection_->expected())
            {
                auto bufs = this->recv_buffer_.data();
                auto i = bufs.begin();
                auto buf(*i);

                // This should not happen™
                assert(i + 1 == bufs.end());

                auto begin = asio::buffer_cast<const char*>(buf);
                auto size = asio::buffer_size(buf);

                auto consumed = connection_->parse(begin, size);
                this->recv_buffer_.consume(consumed);

                log::trace("[{}] Consumed {} of {} bytes.", name_, consumed, received_bytes);
            }
        }
        catch (std::exception& e)
        {
            log::error("[{}] read failed: {}", name_, e.what());
            if (this->connection_)
            {
                this->connection_->fail(e.what());
            }
            this->onError("read failed");
            co_return;
        }
    }

    log::trace("[{}] Stopped listening for incoming data", name_);
}

void AsioConnectionHandler::flush()
{
    if (!underlying_socket().is_open() || send_buffers_.empty() || flush_in_progress_)
    {
        return;
    }

    flush_in_progress_ = true;

    co_spawn(io_context_, do_flush(), *this);
}

Awaitable<void> AsioConnectionHandler::do_flush()
{
    if (send_buffers_.empty())
        co_return;

    try
    {
        auto transferred = co_await this->async_write_some();

        log::trace("[{}] Completed to send {} bytes through the socket", name_, transferred);

        this->send_buffers_.consume(transferred);

        if (heartbeat_interval_.count() != 0 && this->underlying_socket().is_open())
        {
            // we completed a send operation. Now it's time to set up the heartbeat timer.
            this->heartbeat_timer_.expires_after(this->heartbeat_interval_);
            this->heartbeat_timer_.async_wait([this](const auto& error) { this->beat(error); });

            log::trace("[{}] Schedule heartbeat timer in flush callback", name_);
        }

        this->flush_in_progress_ = false;
        this->flush();
    }
    catch (std::exception& e)
    {
        log::error("[{}] write failed: {}", name_, e.what());
        if (this->connection_)
        {
            this->connection_->fail(e.what());
        }
        this->onError("write failed");
    }
}

Awaitable<std::size_t> PlainConnectionHandler::async_write_some()
{
    assert(!send_buffers_.empty());
    co_return co_await socket_.async_write_some(send_buffers_.front(), use_awaitable);
}

Awaitable<std::size_t> PlainConnectionHandler::async_read_some()
{
    assert(this->connection_);
    auto bytes_read = co_await socket_.async_read_some(
        recv_buffer_.prepare(connection_->maxFrame() * 32), use_awaitable);

    co_return bytes_read;
}

Awaitable<std::size_t> SSLConnectionHandler::async_write_some()
{
    assert(!send_buffers_.empty());
    co_return co_await socket_.async_write_some(send_buffers_.front(), use_awaitable);
}

Awaitable<std::size_t> SSLConnectionHandler::async_read_some()
{
    assert(this->connection_);
    auto bytes_read = co_await socket_.async_read_some(
        recv_buffer_.prepare(connection_->maxFrame() * 32), use_awaitable);

    co_return bytes_read;
}

void AsioConnectionHandler::beat(const asio::error_code& error)
{
    if (error && error == asio::error::operation_aborted)
    {
        // timer was canceled, this is probably fine
        return;
    }

    if (error && error != asio::error::operation_aborted)
    {
        // something weird did happen
        log::error("[{}] heartbeat timer failed: {}", name_, error.message());
        if (this->connection_)
        {
            connection_->fail(error.message().c_str());
        }
        onError("heartbeat timer failed");

        return;
    }

    log::trace("[{}] Sending heartbeat to server", name_);
    assert(this->connection_);
    if (!connection_->heartbeat())
    {
        log::error("[{}] Failed to send heartbeat. Retry", name_);

        this->heartbeat_timer_.expires_after(std::chrono::seconds(1));
        this->heartbeat_timer_.async_wait([this](const auto& error) { this->beat(error); });
    }

    // We don't need to setup the timer again, this will be done by the flush() handler triggerd
    // by the heartbeat() call itself
}

std::unique_ptr<AMQP::Channel> AsioConnectionHandler::make_channel()
{
    assert(this->connection_);
    return std::make_unique<AMQP::Channel>(connection_.get());
}

} // namespace metricq

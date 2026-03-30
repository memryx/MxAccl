// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_COMM_SOCKETS_H
#define MX_COMM_SOCKETS_H

#pragma once
#include <memory>
#include <variant>
#include <string>
#include <functional>

#include "mxasio.hpp"
#include "spdlog/spdlog.h"

namespace MX
{
namespace RPC
{

// Supported protocols
enum class Protocol { TCP, Unix };

// Detect protocol based on address string
// If it starts with '/', treat as Unix domain socket; otherwise TCP
inline Protocol detect_protocol(const std::string &address)
{
    if (!address.empty() && address.front() == '/') { return Protocol::Unix; }
    return Protocol::TCP;
}


// Socket for TCP and UDS communication
class Socket
{
  public:
    const bool is_tcp_;

    explicit Socket(mxasio::ip::tcp::socket* tcp_socket, mxasio::local::stream_protocol::socket* uds_socket,
                    bool is_tcp) : is_tcp_(is_tcp)
    {
        tcp_socket_ = tcp_socket;
        uds_socket_ = uds_socket;
        // sanity checks
        if(tcp_socket_ == nullptr && uds_socket_ == nullptr) {
            throw std::runtime_error("Socket is null");
        }
        if(tcp_socket_ != nullptr && uds_socket_ != nullptr) {
            throw std::runtime_error("Socket is both TCP and UDS");
        }
        if(is_tcp_) {
            if(tcp_socket_ == nullptr) {
                throw std::runtime_error("TCP socket is null");
            }
        }
        else {
            if(uds_socket_ == nullptr) {
                throw std::runtime_error("UDS socket is null");
            }
        }
    }

    ~Socket()
    {
        if (tcp_socket_) {
            tcp_socket_->close();
            delete tcp_socket_;
        }
        if (uds_socket_) {
            uds_socket_->close();
            delete uds_socket_;
        }
    }

    size_t write(const mxasio::const_buffer &buffer, mxasio::error_code &error)
    {
        if (is_tcp_) {
            return mxasio::write(*tcp_socket_, buffer, error);
        }
        else {
            return mxasio::write(*uds_socket_, buffer, error);
        }
    }

    size_t write(const mxasio::const_buffer &buffer)
    {
        if(is_tcp_) {
            return mxasio::write(*tcp_socket_, buffer);
        }
        else {
            return mxasio::write(*uds_socket_, buffer);
        }
    }

    size_t read(const mxasio::mutable_buffer &buffer, mxasio::error_code &error)
    {
        if (is_tcp_) {
            return mxasio::read(*tcp_socket_, buffer, error);
        }
        else {
            return mxasio::read(*uds_socket_, buffer, error);
        }
    }

    size_t read(const mxasio::mutable_buffer &buffer)
    {
        if (is_tcp_) {
            return mxasio::read(*tcp_socket_, buffer);
        }
        else {
            return mxasio::read(*uds_socket_, buffer);
        }
    }

    std::string remote_endpoint()
    {
        if (is_tcp_) {
            return tcp_socket_->remote_endpoint().address().to_string();
        }
        else {
            return uds_socket_->remote_endpoint().path();
        }
    }

    bool is_open()
    {
        if (is_tcp_) {
            return tcp_socket_->is_open();
        }
        else {
            return uds_socket_->is_open();
        }
    }

  private:
    mxasio::ip::tcp::socket* tcp_socket_;
    mxasio::local::stream_protocol::socket* uds_socket_;
};

// Connector: blocking client-side connect
class Connector
{
  public:

    explicit Connector(mxasio::io_context &io) : io_context_(io) {}

    ~Connector() {}

    Socket* connect(const std::string &address, unsigned short port)
    {
        Protocol proto = detect_protocol(address);
        if (proto == Protocol::TCP) {
            spdlog::debug("[Connector<TCP>] Connecting to {}:{}", address, port);
            auto tcp_socket_ = new mxasio::ip::tcp::socket(io_context_);
            spdlog::debug("[Connector<TCP>] TCP socket created");
            mxasio::ip::tcp::resolver resolver(io_context_);
            mxasio::ip::tcp::resolver::results_type endpoints = resolver.resolve(address, std::to_string(port));
            if(endpoints.empty()) {
                spdlog::error("[Connector<TCP>] No endpoints found for {}:{}", address, port);
                return nullptr;
            }

            // pick first IPv4 endpoint
            std::string ipv4_address;
            for (const auto &endpoint : endpoints) {
                if (endpoint.endpoint().address().is_v4()) {
                    ipv4_address = endpoint.endpoint().address().to_string();
                    break;
                }
            }

            tcp_socket_->connect( mxasio::ip::tcp::endpoint(mxasio::ip::make_address(ipv4_address), port) );

            spdlog::debug("[Connector<TCP>] TCP socket connected");
            tcp_socket_->set_option(mxasio::ip::tcp::no_delay(true));
            tcp_socket_->set_option(mxasio::socket_base::keep_alive(true));
            spdlog::debug("[Connector<TCP>] TCP socket options set");
            return new Socket(tcp_socket_, nullptr, true);
        }
        else {
            std::string full_address = address + std::to_string(port) + ".sock";
            auto uds_socket_ = new mxasio::local::stream_protocol::socket(io_context_);
            uds_socket_->connect(mxasio::local::stream_protocol::endpoint(full_address));
            return new Socket(nullptr, uds_socket_, false);
        }
    }


  private:
    mxasio::io_context &io_context_;
};


// Listener: blocking server-side accept
class Listener
{
  public:
    Listener(mxasio::io_context &io, const std::string &address, unsigned short port)
        : io_context_(io)
    {
        std::string addr_ = address;
        unsigned short port_ = port;
        Protocol proto = detect_protocol(addr_);

        if (proto == Protocol::TCP) {
            uds_acceptor_ = nullptr;
            try {
                auto ip_addr = mxasio::ip::make_address(addr_);
                spdlog::debug("[Listener] parsed IP address: {}", ip_addr.to_string());
                tcp_acceptor_ = new mxasio::ip::tcp::acceptor(
                    io_context_,
                    mxasio::ip::tcp::endpoint(ip_addr, port_)
                );
                spdlog::debug("[Listener] TCP acceptor listening on {}:{}", addr_, port_);
            }
            catch (const std::exception &e) {
                spdlog::error("[Listener] failed to bind TCP {}:{} -> {}", addr_, port_, e.what());
                throw;  // rethrow so upstream catch() can handle shutdown
            }
        }
        else {
            tcp_acceptor_ = nullptr;
            std::string full_address = address + std::to_string(port) + ".sock";

            // unlink if the socket already exists
            if (std::filesystem::exists(full_address)) {
                std::filesystem::remove(full_address);
            }

            uds_acceptor_ = new mxasio::local::stream_protocol::acceptor(io_context_,
                    mxasio::local::stream_protocol::endpoint(full_address));

            std::filesystem::permissions(full_address,
                                         std::filesystem::perms::all,
                                         std::filesystem::perm_options::replace);
        }
    }

    ~Listener()
    {
        if (tcp_acceptor_ != nullptr) {
            tcp_acceptor_->close();
            delete tcp_acceptor_;
            tcp_acceptor_ = nullptr;
        }
        else if (uds_acceptor_ != nullptr) {
            uds_acceptor_->close();
            delete uds_acceptor_;
            uds_acceptor_ = nullptr;
        }
    }

    void kill()
    {
        if (tcp_acceptor_ != nullptr) {
            tcp_acceptor_->close();
            delete tcp_acceptor_;
            tcp_acceptor_ = nullptr;
        }
        else if (uds_acceptor_ != nullptr) {
            uds_acceptor_->close();
            delete uds_acceptor_;
            uds_acceptor_ = nullptr;
        }
    }

    Socket* accept()
    {
        if (tcp_acceptor_ != nullptr) {
            mxasio::ip::tcp::socket* socket = new mxasio::ip::tcp::socket(io_context_);
            tcp_acceptor_->accept(*socket);
            socket->set_option(mxasio::ip::tcp::no_delay(true));
            socket->set_option(mxasio::socket_base::keep_alive(true));
            return new Socket(socket, nullptr, true);
        }
        else {
            mxasio::local::stream_protocol::socket* socket = new mxasio::local::stream_protocol::socket(io_context_);
            uds_acceptor_->accept(*socket);
            return new Socket(nullptr, socket, false);
        }
    }

  private:
    mxasio::io_context &io_context_;
    mxasio::ip::tcp::acceptor* tcp_acceptor_;
    mxasio::local::stream_protocol::acceptor* uds_acceptor_;
};

} // namespace MX
} // namespace RPC

#endif // MX_COMM_SOCKETS_H

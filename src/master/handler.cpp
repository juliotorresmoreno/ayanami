#include "server/handler.hpp"

#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include "utils/protocol.hpp"
#include "utils/strings.hpp"

using boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::use_awaitable;

constexpr std::size_t FIXED_ID_SIZE = 8;

std::string format_fixed_id(const std::string& id) {
    return id.size() >= FIXED_ID_SIZE ? id.substr(0, FIXED_ID_SIZE)
                                      : id + std::string(FIXED_ID_SIZE - id.size(), ' ');
}

std::string process_command(const std::string& id, const std::string& content) {
    std::ostringstream response;
    std::string cleaned_content = trim(clean_null_terminated(content));
    if (cleaned_content == "PING") {
        response << "PONG";
    } else {
        response << "UNKNOWN COMMAND";
    }
    return response.str();
}

awaitable<void> handle_client(tcp::socket socket) {
    try {
        std::cout << "New connection from " << socket.remote_endpoint() << std::endl;

        for (;;) {
            char length_buffer[4] = {0};
            std::size_t bytes_read = co_await boost::asio::async_read(
                socket, boost::asio::buffer(length_buffer), use_awaitable);
            if (bytes_read != sizeof(length_buffer)) {
                throw std::runtime_error("Incomplete length header");
            }

            uint32_t message_length = 0;
            std::memcpy(&message_length, length_buffer, sizeof(message_length));
            message_length = ntohl(message_length);

            if (message_length <= FIXED_ID_SIZE + 1) {
                throw std::runtime_error("Invalid message length");
            }

            char id_buffer[FIXED_ID_SIZE] = {0};
            co_await boost::asio::async_read(socket, boost::asio::buffer(id_buffer), use_awaitable);

            char delimiter = 0;
            co_await boost::asio::async_read(socket, boost::asio::buffer(&delimiter, 1), use_awaitable);
            if (delimiter != ':') {
                throw std::runtime_error("Invalid message format: Missing ':' delimiter");
            }

            uint32_t content_length = message_length - FIXED_ID_SIZE - 1;
            if (content_length == 0) {
                throw std::runtime_error("Invalid content length");
            }

            std::vector<char> content_buffer(content_length);
            co_await boost::asio::async_read(socket, boost::asio::buffer(content_buffer), use_awaitable);

            std::string id(id_buffer, FIXED_ID_SIZE);
            std::string content(content_buffer.begin(), content_buffer.end());
            std::string fixed_id = format_fixed_id(id);
            std::string response_content = process_command(fixed_id, content);
            auto message = build_message(fixed_id, response_content);

            co_await boost::asio::async_write(socket, boost::asio::buffer(message), use_awaitable);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error handling client: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error occurred while handling client" << std::endl;
    }
    co_return;
}
//
// Created by HYPERBOOK on 08.05.2022.
//

#ifndef BOMBERMAN_MESSAGES_H
#define BOMBERMAN_MESSAGES_H

#include "common.h"

#include <boost/asio.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <boost/bind/bind.hpp>
//boost::placeholders
#include <queue>
#include <memory>

namespace bomberman {

    class InputMessage {
    public:
        static const input_message_t message_code;
    };

    class PlaceBomb : public InputMessage {
    public:
        static const input_message_t message_code = input_message_t::PlaceBomb;
    };

    class PlaceBlock : public InputMessage {
    public:
        static const input_message_t message_code = input_message_t::PlaceBlock;
    };

    class Move : public InputMessage {
    public:
        explicit Move(direction_t _direction)
            : direction(_direction) {}
        direction_t direction;
        static const input_message_t message_code = input_message_t::Move;
    };

    class ServerMessage {
    };

    class ClientMessage {
    };

    class DrawMessage{
    };

    using input_msg_ptr_t = std::shared_ptr<InputMessage>;
    using server_msg_ptr_t = std::shared_ptr<ServerMessage>;
    using client_msg_ptr_t = std::shared_ptr<ClientMessage>;
    using draw_msg_ptr_t = std::shared_ptr<DrawMessage>;

    template<class S> requires (std::same_as<S, boost::asio::ip::tcp::socket> ||
                                std::same_as<S, boost::asio::ip::udp::socket>)
    class NetDeserializer {
    public:
        explicit NetDeserializer(S &socket)
                : socket_(socket), read_idx(0), write_idx(0) {}

    protected:

//        bool is_next_data_chunk_available(std::size_t, std::function<void(NetDeserializer &)>)
//        {
//            BOOST_LOG_TRIVIAL(error) << "Error, unimplemented!\n";
//            return false;
//        }

//        template<class T>
//        void is_next_data_chunk_available(std::size_t n, std::function<T(void)> next_task) {
//            if (read_idx + n <= write_idx)
//                return true;
//
//            if (read_idx + n > buffer.size())
//                buffer.resize(read_idx + n);
//
//            socket_.async_read(boost::asio::buffer(buffer.data() + n + read_idx - write_idx, buffer.size()),
//                               [this, &next_task](boost::system::error_code ec, std::size_t read_length) {
//                                   if (!ec) {
//                                       this->write_idx += read_length
//                                       this->next_task();
//                                   } else {
//                                       socket_.close();
//                                   }
//                               });
//          return false;
//        }

        template<typename T>
        T decode_number() {
            T result;
            if constexpr (sizeof(T) == 1)
                result = std::bit_cast<T>((uint8_t) *(buffer.data() + read_idx));
            else if constexpr (sizeof(T) == 2)
                result = std::bit_cast<T>(ntohs(std::bit_cast<uint16_t>((uint16_t)*(buffer.data() + read_idx))));
            else if constexpr (sizeof(T) == 4)
                result = std::bit_cast<T>(ntohl(std::bit_cast<uint32_t>((uint32_t)*(buffer.data() + read_idx))));
            else
                assert(false);

            read_idx += sizeof(T);
            return result;
        }

        S &socket_;
        buffer_t buffer;
        std::size_t read_idx;
        std::size_t write_idx;

    };

    class UdpDeserializer : public NetDeserializer<boost::asio::ip::udp::socket> {
    public:
        explicit UdpDeserializer(boost::asio::ip::udp::socket &socket)
                : NetDeserializer(socket) {}


        void get_message(auto message_handle_callback) {
            // if buffer is empty we asynchronously listen on incoming messages and then call this function again
            if (buffer.empty()) {
                buffer.resize(MAX_GUI_TO_CLIENT_MESSAGE_SIZE);
                // receive up to maximum message length + 1 to check if upd datagram is not too long
                socket_.async_receive(boost::asio::buffer(buffer.data(), buffer.size() + 1),
                                      [=, this](boost::system::error_code ec, std::size_t read_length) {
                                          if (!ec) {
                                              this->write_idx += read_length;
                                              get_message(message_handle_callback);
                                          } else {
                                              socket_.close();
                                              BOOST_LOG_TRIVIAL(debug) << "Error in UdpDeserializer::get_message while async_receive\n";
                                              throw ReceiveError("GUI");
                                          }
                                      });
                return;
            }

            input_msg_ptr_t input_msg_ptr;
            auto message_code = decode_number<input_message_t>();
            switch (message_code) {
                case input_message_t::PlaceBomb:
                    if (write_idx != 1)
                    {
                        BOOST_LOG_TRIVIAL(debug) << "invalid message length for input_message_t::PlaceBomb, write_idx =" << write_idx << "\n";
                        throw InvalidMessage("GUI");
                    }
                    input_msg_ptr = std::make_shared<PlaceBomb>();
                    break;
                case input_message_t::PlaceBlock:
                    if (write_idx != 1)
                    {
                        BOOST_LOG_TRIVIAL(debug) << "invalid message length for input_message_t::PlaceBlock, write_idx =" << write_idx << "\n";
                        throw InvalidMessage("GUI");
                    }
                    input_msg_ptr = std::make_shared<PlaceBlock>();
                    break;
                case input_message_t::Move:
                    auto direction = decode_number<direction_t>();
                    if (write_idx != 2 || direction > direction_t::Left)
                    {
                        BOOST_LOG_TRIVIAL(debug) << "invalid message for input_message_t::Move, write_idx =" << write_idx << ", direction = " << (uint8_t)direction << "\n";
                        throw InvalidMessage("GUI");
                    }
                    input_msg_ptr = std::make_shared<Move>(direction);
                    break;
            }

            message_handle_callback(std::move(input_msg_ptr));
        }
    };

} // namespace bomberman

#endif //BOMBERMAN_MESSAGES_H

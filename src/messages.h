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
#include <functional>

namespace bomberman {

    class InputMessage {
    };

    class PlaceBomb : public InputMessage {
    };

    class PlaceBlock : public InputMessage {
    };

    class Move : public InputMessage {
    public:
        direction_t direction;
    };


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

        template<class T>
        T decode_number() {
            T result;
            switch (sizeof(T)) {
                case 2:
                    result = std::bit_cast<T>(ntohs(std::bit_cast<uint16_t>((uint16_t)*(buffer.data() + read_idx))));
                    break;
                case 4:
                    result = std::bit_cast<T>(ntohl(std::bit_cast<uint32_t>((uint32_t)*(buffer.data() + read_idx))));
                    break;
                default:
                    assert(false);
            }
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


        void get_message() {
            // if buffer is empty we asynchronously listen on incoming messages and then call this function again
            if (buffer.empty()) {
                buffer.resize(MAX_GUI_TO_CLIENT_MESSAGE_SIZE);
                // receive up to maximum message length + 1 to check if upd datagram is not too long
                socket_.async_receive(boost::asio::buffer(buffer.data(), buffer.size() + 1),
                                      [this](boost::system::error_code ec, std::size_t read_length) {
                                          if (!ec) {
                                              this->write_idx += read_length;
                                              get_message();
                                          } else {
                                              socket_.close();
                                              throw ReceiveError("GUI");
                                          }
                                      });
                return;
            }


        }
    };

} // namespace bomberman

#endif //BOMBERMAN_MESSAGES_H

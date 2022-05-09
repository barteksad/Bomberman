//
// Created by HYPERBOOK on 08.05.2022.
//

#ifndef BOMBERMAN_MESSAGES_H
#define BOMBERMAN_MESSAGES_H

#include "common.h"

#include <boost/asio.hpp>

#include <functional>

namespace bomberman {

    template<class S> requires (std::same_as<S, boost::asio::ip::tcp::socket> ||
                                std::same_as<S, boost::asio::ip::tcp::socket>)
    class NetDeserializer {
    public:
        NetDeserializer(S &socket)
                : socket_(socket), read_idx(0), write_idx(0) {}

    private:

        template<class T>
        void is_next_data_chunk_available(std::size_t n, std::function<T(void)> next_task) {
            if (read_idx + n <= write_idx)
                return;

            if (read_idx + n > buffer.size())
                buffer.resize(read_idx + n);

            socket_.async_read(boost::asio::buffer(buffer.data() + n + read_idx - write_idx, buffer.size()),
                               [this, &next_task](boost::system::error_code ec, std::size_t read_length) {
                                   if (!ec) {
                                       this->next_task();
                                   } else {
                                       socket_.close();
                                   }
                               });
        }

        template<class T>
        T read_number()
        {
            if(!is_next_data_chunk_available(sizeof(T)))
                return;

            T result;
            switch(sizeof(T))
            {
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

        S & socket_;
        buffer_t buffer;
        std::size_t read_idx;
        std::size_t write_idx;

    };
}

#endif //BOMBERMAN_MESSAGES_H

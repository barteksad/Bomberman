#ifndef BOMBERMAN_MESSAGES_H
#define BOMBERMAN_MESSAGES_H

#include "common.h"

#include <boost/asio.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <boost/bind/bind.hpp>
#include <boost/asio/spawn.hpp>

// #include <boost/asio/co_spawn.hpp>
// #include <boost/asio/detached.hpp>
// #include <boost/asio/io_context.hpp>
// #include <boost/asio/ip/tcp.hpp>
// #include <boost/asio/signal_set.hpp>
// #include <boost/asio/write.hpp>
// #include <cstdio>

// using boost::asio::awaitable;
// using boost::asio::co_spawn;
// using boost::asio::detached;
// using boost::asio::use_awaitable;

// boost::placeholders
#include <queue>
#include <variant>
#include <bit>

namespace bomberman
{

    struct Join
    {
        explicit Join(std::string &_name)
            : name(_name) {}
        std::string name;
    };

    struct PlaceBomb
    {
    };

    struct PlaceBlock
    {
    };

    struct Move
    {
        explicit Move(direction_t _direction)
            : direction(_direction) {}
        direction_t direction;
    };

    struct Hello
    {
        Hello(std::string _server_name,
              players_count_t _players_count,
              size_x_t _size_x,
              size_y_t _size_y,
              game_length_t _game_length,
              explosion_radius_t _esplosion_radius,
              bomb_timer_t _bomb_timer)
            : server_name(_server_name), players_count(_players_count), size_x(_size_x), size_y(_size_y), game_length(_game_length), esplosion_radius(_esplosion_radius), bomb_timer(_bomb_timer) {}
        const std::string server_name;
        players_count_t players_count;
        size_x_t size_x;
        size_y_t size_y;
        game_length_t game_length;
        explosion_radius_t esplosion_radius;
        bomb_timer_t bomb_timer;
    };

    struct AcceptedPlayer
    {
        AcceptedPlayer(player_id_t _player_id, player_t _player)
            : player_id(_player_id), player(_player) {}
        player_id_t player_id;
        player_t player;
    };

    struct GameStarted
    {
        GameStarted(players_t &_players)
            : players(_players) {}
        players_t players;
    };

    struct Turn
    {
        Turn(turn_t _turn, events_t &_events)
            : turn(_turn), events(_events) {}
        turn_t turn;
        events_t events;
    };

    struct GameEnded
    {
        GameEnded(scores_t &_scores)
            : scores(_scores) {}
        scores_t scores;
    };

    struct Lobby
    {
        Lobby(std::string &_server_name,
              players_count_t _players_count,
              size_x_t _size_x,
              size_y_t _size_y,
              game_length_t _game_length,
              explosion_radius_t _esplosion_radius,
              bomb_timer_t _bomb_timer,
              players_t &_players)
            : server_name(_server_name), players_count(_players_count), size_x(_size_x), size_y(_size_y), game_length(_game_length), esplosion_radius(_esplosion_radius), bomb_timer(_bomb_timer), players(_players) {}
        std::string server_name;
        players_count_t players_count;
        size_x_t size_x;
        size_y_t size_y;
        game_length_t game_length;
        explosion_radius_t esplosion_radius;
        bomb_timer_t bomb_timer;
        players_t players;
    };

    struct Game
    {
        std::string server_name;
        size_x_t size_x;
        size_y_t size_y;
        game_length_t game_length;
        turn_t turn;
        players_t players;
        player_positions_t players_positions;
        blocks_t blocks;
        bombs_t bombs;
        explosion_radius_t explosion_radius;
        scores_t scores;
    };

    using input_message_t = std::variant<PlaceBomb, PlaceBlock, Move>;
    using client_message_t = std::variant<Join, PlaceBomb, PlaceBlock, Move>;
    using server_message_t = std::variant<Hello, AcceptedPlayer, GameStarted, Turn, GameEnded>;
    using draw_message_t = std::variant<Lobby, Game>;

    template <class S>
    requires(std::same_as<S, boost::asio::ip::tcp::socket> ||
             std::same_as<S, boost::asio::ip::udp::socket>) class NetDeserializer
    {
    public:
        explicit NetDeserializer(S &socket)
            : socket_(socket), read_idx(0), write_idx(0) {}

    protected:

        template <typename T>
        T decode_number()
        {
            T result;
            if constexpr (sizeof(T) == 1)
                result = std::bit_cast<T>((uint8_t) * (buffer.data() + read_idx));
            else if constexpr (sizeof(T) == 2)
                result = std::bit_cast<T>(ntohs(std::bit_cast<uint16_t>((uint16_t) * (buffer.data() + read_idx))));
            else if constexpr (sizeof(T) == 4)
                result = std::bit_cast<T>(ntohl(std::bit_cast<uint32_t>((uint32_t) * (buffer.data() + read_idx))));
            else
                assert(false);

            read_idx += sizeof(T);
            return result;
        }

        S &socket_;
        buffer_t buffer;
        std::size_t read_idx;
        std::size_t write_idx;

    protected:
        void reset_state()
        {
            buffer.resize(0);
            read_idx = 0;
            write_idx = 0;
        }
    };

    class NetSerializer
    {
    public:
        NetSerializer()
            : write_idx(0) {}

        // #TODO!
        buffer_t &serialize(client_message_t &client_message)
        {
            return buffer;
        }
        buffer_t &serialize(server_message_t &server_message)
        {
            return buffer;
        }
        buffer_t &serialize(draw_message_t &draw_message)
        {
            return buffer;
        }

    private:
        buffer_t buffer;
        std::size_t write_idx;
    };

    class TcpDeserializer : public NetDeserializer<boost::asio::ip::tcp::socket>
    {
    public:
        explicit TcpDeserializer(boost::asio::ip::tcp::socket &socket)
            : NetDeserializer(socket) {}

        void get_server_message(auto message_handle_callback, boost::asio::yield_context yield)
        {
            BOOST_LOG_TRIVIAL(debug) << "in TcpDeserializer::get_server_message";
            auto message_code = get_number<server_message_code_t>(yield);
            switch (message_code)
            {
            case server_message_code_t::Hello:
                return get_hello_message();
            case server_message_code_t::AcceptedPlayer:
                return get_accepted_player_message();
            case server_message_code_t::GameStarted:
                return get_game_started_message();
            case server_message_code_t::Turn:
                return get_turn_message();
            case server_message_code_t::GameEnded:
                return get_game_ended_message();
            }

        }

    private:

        void read_n_bytes(std::size_t read_n, boost::asio::yield_context yield)
        {
            buffer.resize(buffer.size() + read_n);
            boost::system::error_code ec;
            BOOST_LOG_TRIVIAL(debug) << "server start waiting for " << read_n << " bytes";
            boost::asio::async_read(socket_, boost::asio::buffer(buffer.data() + write_idx, read_n), yield[ec]);
            if (!ec)
            {
                BOOST_LOG_TRIVIAL(debug) << "Received " << read_n << " bytes from server";
            }
            else
            {
                BOOST_LOG_TRIVIAL(debug) << "Error in TcpDeserializer::read_n_bytes while async_read";
                throw ReceiveError("Server");
            } 
            write_idx += read_n;
        }
        
        template <typename T>
        T get_number(boost::asio::yield_context yield)
        {
            std::size_t read_n = sizeof(T);
            read_n_bytes(read_n, yield);
            return decode_number<T>();
        }

        std::string get_string(boost::asio::yield_context yield)
        {
            str_len_t str_len = get_number<str_len_t>(yield);
            read_n_bytes((std::size_t) str_len, yield);
            std::string result;
            while(read_idx < write_idx)
                result.push_back(buffer[read_idx++]);
            return result;
        }

        void get_hello_message()
        {

        }

        void get_accepted_player_message()
        {
        }
        void get_game_started_message()
        {
        }
        void get_turn_message()
        {
        }
        void get_game_ended_message()
        {
        }
    };
    class UdpDeserializer : public NetDeserializer<boost::asio::ip::udp::socket>
    {
    public:
        explicit UdpDeserializer(boost::asio::ip::udp::socket &socket)
            : NetDeserializer(socket) {}

        void get_message(auto message_handle_callback)
        {
            // if buffer is empty we asynchronously listen on incoming messages and then call this function again
            if (buffer.empty())
            {
                BOOST_LOG_TRIVIAL(debug) << "Start gui async receive\n";
                buffer.resize(MAX_GUI_TO_CLIENT_MESSAGE_SIZE + 1);
                // receive up to maximum message length + 1 to check if upd datagram is not too long
                socket_.async_receive(boost::asio::buffer(buffer.data(), MAX_GUI_TO_CLIENT_MESSAGE_SIZE + 1),
                                      [=, this](boost::system::error_code ec, std::size_t read_length)
                                      {
                                          if (!ec)
                                          {
                                              this->write_idx += read_length;
                                              BOOST_LOG_TRIVIAL(debug) << "Received " << read_length << " bytes from gui";
                                              get_message(message_handle_callback);
                                          }
                                          else
                                          {
                                              BOOST_LOG_TRIVIAL(debug) << "Error in UdpDeserializer::get_message while async_receive";
                                              throw ReceiveError("GUI");
                                          }
                                      });
                return;
            }

            input_message_t input_message;
            auto message_code = decode_number<input_message_code_t>();
            BOOST_LOG_TRIVIAL(debug) << "GUI message code: " << (uint8_t)message_code;
            switch (message_code)
            {
            case input_message_code_t::PlaceBomb:
                if (write_idx != 1)
                {
                    BOOST_LOG_TRIVIAL(debug) << "invalid message length for input_message_code_t::PlaceBomb, write_idx =" << write_idx;
                    throw InvalidMessage("GUI");
                }
                input_message = PlaceBomb{};
                break;
            case input_message_code_t::PlaceBlock:
                if (write_idx != 1)
                {
                    BOOST_LOG_TRIVIAL(debug) << "invalid message length for input_message_code_t::PlaceBlock, write_idx =" << write_idx;
                    throw InvalidMessage("GUI");
                }
                input_message = PlaceBlock{};
                break;
            case input_message_code_t::Move:
                auto direction = decode_number<direction_t>();
                if (write_idx != 2 || direction > direction_t::Left)
                {
                    BOOST_LOG_TRIVIAL(debug) << "invalid message for input_message_code_t::Move, write_idx =" << write_idx << ", direction = " << (uint8_t)direction;
                    throw InvalidMessage("GUI");
                }
                input_message = Move{direction};
                break;
            }

            BOOST_LOG_TRIVIAL(debug) << "RECEIVED MESSAGE!\n";
            reset_state();

            message_handle_callback(input_message);
        }
    };

} // namespace bomberman

#endif // BOMBERMAN_MESSAGES_H

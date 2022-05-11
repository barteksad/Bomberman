#ifndef BOMBERMAN_MESSAGES_H
#define BOMBERMAN_MESSAGES_H

#include "common.h"

#include <boost/asio.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <boost/bind/bind.hpp>
// boost::placeholders
#include <queue>
#include <variant>

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
        virtual void reset_state() = 0;
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

    class TcpNetDeserializer : public NetDeserializer<boost::asio::ip::tcp::socket>
    {
        public:
            explicit TcpNetDeserializer(boost::asio::ip::tcp::socket &socket)
                : NetDeserializer(socket) {}

        void get_server_message(auto message_handle_callback)
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
                                              socket_.close();
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

    protected:
        void reset_state() override
        {
            buffer.resize(0);
            write_idx = 0;
            read_idx = 0;
        }
    };

} // namespace bomberman

#endif // BOMBERMAN_MESSAGES_H

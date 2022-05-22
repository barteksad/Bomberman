#ifndef BOMBERMAN_NET_H
#define BOMBERMAN_NET_H

#include "errors.h"
#include "messages.h"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include <functional>
#include <optional>

namespace bomberman
{

    template <class T>
    concept message_t = requires()
    {
        std::same_as<T, input_message_t> ||
            std::same_as<T, client_message_t> ||
            std::same_as<T, server_message_t> ||
            std::same_as<T, draw_message_t>;
    };

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
                result = std::bit_cast<T>(ntohs(*std::bit_cast<uint16_t *>(buffer.data() + read_idx)));
            else if constexpr (sizeof(T) == 4)
                result = std::bit_cast<T>(ntohl(*std::bit_cast<uint32_t *>(buffer.data() + read_idx)));
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

    class TcpDeserializer : public NetDeserializer<boost::asio::ip::tcp::socket>
    {
    public:
        explicit TcpDeserializer(boost::asio::ip::tcp::socket &socket)
            : NetDeserializer(socket) {}

        message_t auto get_server_message(boost::asio::yield_context yield)
        {
            reset_state();
            server_message_code_t message_code = get_number<server_message_code_t>(yield);
            BOOST_LOG_TRIVIAL(debug) << "received server message code: " << static_cast<uint16_t>(message_code);
            switch (message_code)
            {
            case server_message_code_t::Hello:
                return get_hello_message(yield);
            case server_message_code_t::AcceptedPlayer:
                return get_accepted_player_message(yield);
            case server_message_code_t::GameStarted:
                return get_game_started_message(yield);
            case server_message_code_t::Turn:
                return get_turn_message(yield);
            case server_message_code_t::GameEnded:
                return get_game_ended_message(yield);
            default:
                BOOST_LOG_TRIVIAL(fatal) << "Invalid server message code!";
                throw InvalidMessage("Server");
            }
        }

        message_t auto get_client_message(boost::asio::yield_context yield)
        {
            reset_state();
            client_message_code_t message_code = get_number<client_message_code_t>(yield);
            BOOST_LOG_TRIVIAL(debug) << "received client message code: " << static_cast<uint16_t>(message_code);
            switch (message_code)
            {
            case client_message_code_t::Join:
                return get_join(yield);
            case client_message_code_t::PlaceBomb:
                return get_place_bomb();
            case client_message_code_t::PlaceBlock:
                return get_place_block();
            case client_message_code_t::Move:
                return get_move(yield);
            default:
                BOOST_LOG_TRIVIAL(fatal) << "Invalid client message code!";
                throw InvalidMessage("Client");
            }
        }

    private:
        void read_n_bytes(std::size_t read_n, boost::asio::yield_context yield)
        {
            buffer.resize(buffer.size() + read_n);
            boost::system::error_code ec;
            std::size_t read_count = boost::asio::async_read(socket_, boost::asio::buffer(buffer.data() + write_idx, read_n), yield[ec]);
            if (ec || read_count != read_n)
            {
                BOOST_LOG_TRIVIAL(debug) << "Error in TcpDeserializer::read_n_bytes " << ec.message();
                throw ReceiveError("Server", ec);
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
            read_n_bytes(static_cast<std::size_t>(str_len), yield);
            std::string result;
            while (read_idx < write_idx)
                result.push_back(buffer[read_idx++]);
            return result;
        }

        player_t get_player(boost::asio::yield_context yield)
        {
            return player_t{
                .name = get_string(yield),
                .address = get_string(yield),
            };
        }

        event_t get_event(boost::asio::yield_context yield)
        {
            event_code_t event_code = get_number<event_code_t>(yield);
            if (event_code == event_code_t::BombPlaced)
            {
                BombPlaced bomb_placed;
                bomb_placed.bomb_id = get_number<bomb_id_t>(yield);
                bomb_placed.position = {
                    .x = get_number<size_x_t>(yield),
                    .y = get_number<size_y_t>(yield),
                };
                return bomb_placed;
            }
            else if (event_code == event_code_t::BombExploded)
            {
                BombExploded bomb_exploded;
                bomb_exploded.bomb_id = get_number<bomb_id_t>(yield);
                list_len_t rob_dest_list_len = get_number<list_len_t>(yield);
                while (rob_dest_list_len--)
                {
                    bomb_exploded.robots_destroyed.insert(get_number<player_id_t>(yield));
                }
                list_len_t block_dest_list_len = get_number<list_len_t>(yield);
                while (block_dest_list_len--)
                {
                    bomb_exploded.blocks_destroyed.insert(
                        position_t{
                            .x = get_number<size_x_t>(yield),
                            .y = get_number<size_y_t>(yield),
                        });
                }
                return bomb_exploded;
            }
            else if (event_code == event_code_t::PlayerMoved)
            {
                PlayerMoved player_moved;
                player_moved.player_id = get_number<player_id_t>(yield);
                player_moved.position = {
                    .x = get_number<size_x_t>(yield),
                    .y = get_number<size_y_t>(yield),
                };
                return player_moved;
            }
            else if (event_code == event_code_t::BlockPlaced)
            {
                BlockPlaced block_placed;
                block_placed.position = {
                    .x = get_number<size_x_t>(yield),
                    .y = get_number<size_y_t>(yield),
                };
                return block_placed;
            }

            BOOST_LOG_TRIVIAL(fatal) << "invalid event code :  " << static_cast<std::underlying_type<event_code_t>::type>(event_code);
            throw InvalidMessage("Server");
        }

        server_message_t get_hello_message(boost::asio::yield_context yield)
        {
            Hello hello;
            hello.server_name = get_string(yield);
            hello.players_count = get_number<players_count_t>(yield);
            hello.size_x = get_number<size_x_t>(yield);
            hello.size_y = get_number<size_y_t>(yield);
            hello.game_length = get_number<game_length_t>(yield);
            hello.explosion_radius = get_number<explosion_radius_t>(yield);
            hello.bomb_timer = get_number<bomb_timer_t>(yield);

            return hello;
        }

        server_message_t get_accepted_player_message(boost::asio::yield_context yield)
        {
            AcceptedPlayer accepted_player;
            accepted_player.player_id = get_number<player_id_t>(yield);
            accepted_player.player = get_player(yield);
            return accepted_player;
        }
        server_message_t get_game_started_message(boost::asio::yield_context yield)
        {
            GameStarted game_started;
            map_len_t map_len = get_number<map_len_t>(yield);
            while (map_len--)
            {
                game_started.players.insert({get_number<player_id_t>(yield),
                                             get_player(yield)});
            }
            return game_started;
        }
        server_message_t get_turn_message(boost::asio::yield_context yield)
        {
            Turn turn;
            turn.turn = get_number<turn_t>(yield);
            list_len_t event_list_len = get_number<list_len_t>(yield);
            while (event_list_len--)
            {
                turn.events.push_back(get_event(yield));
            }
            return turn;
        }
        server_message_t get_game_ended_message(boost::asio::yield_context yield)
        {
            GameEnded game_ended;
            map_len_t scores_map_len = get_number<map_len_t>(yield);
            while (scores_map_len--)
            {

                game_ended.scores.insert({
                    get_number<player_id_t>(yield),
                    get_number<score_t>(yield),
                });
            }
            return game_ended;
        }

        client_message_t get_join(boost::asio::yield_context yield)
        {
            Join join;
            join.name = get_string(yield);
            return join;
        }

        client_message_t get_place_bomb()
        {
            PlaceBomb place_bomb;
            return place_bomb;
        }

        client_message_t get_place_block()
        {
            PlaceBlock place_block;
            return place_block;
        }

        client_message_t get_move(boost::asio::yield_context yield)
        {
            Move move;
            move.direction = get_number<direction_t>(yield);
            return move;
        }
    };

    class UdpDeserializer : public NetDeserializer<boost::asio::ip::udp::socket>
    {
    public:
        explicit UdpDeserializer(boost::asio::ip::udp::socket &socket)
            : NetDeserializer(socket) { reset_state(); }

        void get_message(std::function<void(input_message_t)> message_handle_callback)
        {
            // if buffer is empty we asynchronously listen on incoming messages and then call this function again
            if (buffer.empty())
            {
                buffer.resize(MAX_GUI_TO_CLIENT_MESSAGE_SIZE + 1);
                // receive up to maximum message length + 1 to check if upd datagram is not too long
                socket_.async_receive_from(boost::asio::buffer(buffer.data(), MAX_GUI_TO_CLIENT_MESSAGE_SIZE + 1),
                                           gui_endpoint,
                                           [message_handle_callback, this](boost::system::error_code ec, std::size_t read_length) {
                                               if (!ec)
                                               {
                                                   this->write_idx += read_length;
                                                   BOOST_LOG_TRIVIAL(debug) << "received from GUI at " << gui_endpoint;
                                                   get_message(message_handle_callback);
                                               }
                                               else
                                               {
                                                   BOOST_LOG_TRIVIAL(debug) << "Error in UdpDeserializer::get_message";
                                                   throw ReceiveError("GUI", ec);
                                               }
                                           });
                return;
            }

            std::optional<input_message_t> input_message;
            auto message_code = decode_number<input_message_code_t>();
            BOOST_LOG_TRIVIAL(debug) << "GUI message code: " << static_cast<uint16_t>(message_code);
            // if (message_code < input_message_code_t::PlaceBomb || message_code > input_message_code_t::Move)
            // {
            //     BOOST_LOG_TRIVIAL(debug) << "Received invalid message code from GUI";
            //     reset_state();
            //     return get_message(message_handle_callback, gui_endpoint);
            // }
            switch (message_code)
            {
            case input_message_code_t::PlaceBomb:
                if (write_idx != 1)
                {
                    BOOST_LOG_TRIVIAL(debug) << "invalid message length for input_message_code_t::PlaceBomb, write_idx =" << write_idx;
                    break;
                }
                input_message = PlaceBomb{};
                break;
            case input_message_code_t::PlaceBlock:
                if (write_idx != 1)
                {
                    BOOST_LOG_TRIVIAL(debug) << "invalid message length for input_message_code_t::PlaceBlock, write_idx =" << write_idx;
                    break;
                }
                input_message = PlaceBlock{};
                break;
            case input_message_code_t::Move:
                auto direction = decode_number<direction_t>();
                if (write_idx != 2 || direction > direction_t::Left)
                {
                    BOOST_LOG_TRIVIAL(debug) << "invalid message for input_message_code_t::Move, write_idx =" << write_idx << ", direction = " << static_cast<std::underlying_type<direction_t>::type>(direction);
                    break;
                }
                input_message = Move{direction};
                break;
            }

            reset_state();

            if(input_message.has_value())
                return message_handle_callback(input_message.value());
            else
            {
                BOOST_LOG_TRIVIAL(debug) << "Received invalid message code from GUI";
                return get_message(message_handle_callback);
            }
        }
    private:
        boost::asio::ip::udp::endpoint gui_endpoint;
    };

    class NetSerializer
    {
    public:
        NetSerializer() {}
        // #TODO!
        buffer_t &serialize(client_message_t &client_message)
        {
            reset_state();
            std::visit(overloaded{
                           std::bind(&NetSerializer::write_join, this, std::placeholders::_1),
                           std::bind(&NetSerializer::write_place_bomb, this, std::placeholders::_1),
                           std::bind(&NetSerializer::write_place_block, this, std::placeholders::_1),
                           std::bind(&NetSerializer::write_move, this, std::placeholders::_1),
                       },
                       client_message);

            return buffer;
        }
        buffer_t &serialize(server_message_t &server_message)
        {
            reset_state();
            std::visit(overloaded{
                           std::bind(&NetSerializer::write_hello, this, std::placeholders::_1),
                           std::bind(&NetSerializer::write_accepted_player, this, std::placeholders::_1),
                           std::bind(&NetSerializer::write_game_started, this, std::placeholders::_1),
                           std::bind(&NetSerializer::write_turn, this, std::placeholders::_1),
                           std::bind(&NetSerializer::write_game_ended, this, std::placeholders::_1)},
                       server_message);

            return buffer;
        }
        buffer_t &serialize(draw_message_t &draw_message)
        {
            reset_state();
            std::visit(overloaded{
                           std::bind(&NetSerializer::write_lobby, this, std::placeholders::_1),
                           std::bind(&NetSerializer::write_game, this, std::placeholders::_1),
                       },
                       draw_message);
            return buffer;
        }

    private:
        buffer_t buffer;

        void reset_state()
        {
            buffer.resize(0);
        }

        template <typename T>
        void write_number(T number)
        {
            if constexpr (sizeof(T) == 1)
                number = number;
            else if constexpr (sizeof(T) == 2)
                number = std::bit_cast<T>(htons(std::bit_cast<uint16_t>(number)));
            else if constexpr (sizeof(T) == 4)
                number = std::bit_cast<T>(htonl(std::bit_cast<uint32_t>(number)));
            else
                assert(false);

            char *bytes = std::bit_cast<char *>(&number);
            write_bytes_to_buffer(bytes, sizeof(T));
        }

        void write_bytes_to_buffer(const char *bytes, std::size_t length)
        {
            assert(bytes);
            for (std::size_t i = 0; i < length; i++)
                buffer.push_back(bytes[i]);
        }

        void write_string(const std::string &s)
        {
            write_number((str_len_t)s.size());
            for (auto i = s.begin(); i != s.end(); i++)
                buffer.push_back(*i);
        }

        void write_player(const player_t &player)
        {
            write_string(player.name);
            write_string(player.address);
        }

        void write_position(const position_t &position)
        {
            write_number<size_x_t>(position.x);
            write_number<size_y_t>(position.y);
        }

        void write_bomb(const bomb_t &bomb)
        {
            write_position(bomb.position);
            write_number<bomb_timer_t>(bomb.timer);
        }

        void write_join(Join &join)
        {
            write_number<client_message_code_t>(client_message_code_t::Join);
            write_string(join.name);
        }

        void write_place_bomb(PlaceBomb &)
        {
            write_number<client_message_code_t>(client_message_code_t::PlaceBomb);
        }

        void write_place_block(PlaceBlock &)
        {
            write_number<client_message_code_t>(client_message_code_t::PlaceBlock);
        }

        void write_move(Move &move)
        {
            write_number<client_message_code_t>(client_message_code_t::Move);
            write_number<direction_t>(move.direction);
        }

        void write_lobby(Lobby &lobby)
        {
            write_number<draw_message_code_t>(draw_message_code_t::Lobby);
            write_string(lobby.server_name);
            write_number<players_count_t>(lobby.players_count);
            write_number<size_x_t>(lobby.size_x);
            write_number<size_y_t>(lobby.size_y);
            write_number<game_length_t>(lobby.game_length);
            write_number<explosion_radius_t>(lobby.explosion_radius);
            write_number<bomb_timer_t>(lobby.bomb_timer);
            write_number<map_len_t>((map_len_t)lobby.players.size());
            std::for_each(lobby.players.begin(), lobby.players.end(),
                          [this](auto &players_map_entry) {
                              write_number<player_id_t>(players_map_entry.first);
                              write_player(players_map_entry.second);
                          });
        }

        void write_game(Game &game)
        {
            write_number<draw_message_code_t>(draw_message_code_t::Game);
            write_string(game.server_name);
            write_number<size_x_t>(game.size_x);
            write_number<size_y_t>(game.size_y);
            write_number<game_length_t>(game.game_length);
            write_number<turn_t>(game.turn);
            write_number<map_len_t>((map_len_t)game.players.size());
            std::for_each(game.players.begin(), game.players.end(),
                          [this](auto &players_map_entry) {
                              write_number<player_id_t>(players_map_entry.first);
                              write_player(players_map_entry.second);
                          });
            write_number<map_len_t>((map_len_t)game.players_positions.size());
            BOOST_LOG_TRIVIAL(debug) << "Sending players positions, map size: " << game.players_positions.size();
            std::for_each(game.players_positions.begin(), game.players_positions.end(),
                          [this](auto &players_positions_map_entry) {
                              BOOST_LOG_TRIVIAL(debug) << "player : " << static_cast<uint16_t>(players_positions_map_entry.first) << " position " << players_positions_map_entry.second.x << "," << players_positions_map_entry.second.y;
                              write_number<player_id_t>(players_positions_map_entry.first);
                              write_position(players_positions_map_entry.second);
                          });
            BOOST_LOG_TRIVIAL(debug) << "Sending blocks positions, list size: " << game.blocks.size();
            write_number<list_len_t>((list_len_t)game.blocks.size());
            std::for_each(game.blocks.begin(), game.blocks.end(),
                          [this](auto &block) {
                              BOOST_LOG_TRIVIAL(debug) << " position " << block.x << "," << block.y;
                              write_position(block);
                          });
            write_number<list_len_t>((list_len_t)game.bombs.size());
            std::for_each(game.bombs.begin(), game.bombs.end(),
                          [this](auto &bomb_pair) {
                              write_bomb(bomb_pair.second);
                          });
            BOOST_LOG_TRIVIAL(debug) << "Sending explosion positions, list size: " << game.explosions.size();
            write_number<list_len_t>((list_len_t)game.explosions.size());
            std::for_each(game.explosions.begin(), game.explosions.end(),
                          [this](auto &explosion) {
                              BOOST_LOG_TRIVIAL(debug) << " position " << explosion.x << "," << explosion.y;
                              write_position(explosion);
                          });
            write_number<map_len_t>((map_len_t)game.scores.size());
            for (auto &scores_map_entry : game.scores)
            {
                write_number<player_id_t>(scores_map_entry.first);
                write_number<score_t>(scores_map_entry.second);
            }
        }

        void write_hello(Hello &hello)
        {
            write_number<server_message_code_t>(server_message_code_t::Hello);
            write_string(hello.server_name);
            write_number<players_count_t>(hello.players_count);
            write_number<size_x_t>(hello.size_x);
            write_number<size_y_t>(hello.size_y);
            write_number<game_length_t>(hello.game_length);
            write_number<explosion_radius_t>(hello.explosion_radius);
            write_number<bomb_timer_t>(hello.bomb_timer);
        }

        void write_accepted_player(AcceptedPlayer &accepted_player)
        {
            write_number<server_message_code_t>(server_message_code_t::AcceptedPlayer);
            write_number<player_id_t>(accepted_player.player_id);
            write_player(accepted_player.player);
        }

        void write_game_started(GameStarted &game_started)
        {
            write_number<server_message_code_t>(server_message_code_t::GameStarted);
            write_number<map_len_t>((map_len_t)game_started.players.size());
            for (auto &players_map_entry : game_started.players)
            {
                write_number<player_id_t>(players_map_entry.first);
                write_player(players_map_entry.second);
            }
        }

        void write_turn(Turn &turn)
        {
            write_number<server_message_code_t>(server_message_code_t::Turn);
            write_number<turn_t>(turn.turn);
            write_number<list_len_t>((list_len_t)turn.events.size());
            for (event_t &event : turn.events)
            {
                std::visit(overloaded{[this](BombPlaced &bomb_placed) {
                                          write_number<event_code_t>(event_code_t::BombPlaced);
                                          write_number<bomb_id_t>(bomb_placed.bomb_id);
                                          write_position(bomb_placed.position);
                                      },
                                      [this](BombExploded &bomb_exploded) {
                                          write_number<event_code_t>(event_code_t::BombExploded);
                                          write_number<bomb_id_t>(bomb_exploded.bomb_id);
                                          write_number<list_len_t>((list_len_t)bomb_exploded.robots_destroyed.size());
                                          for (const player_id_t &player_id : bomb_exploded.robots_destroyed)
                                              write_number<player_id_t>(player_id);
                                          write_number<list_len_t>((list_len_t)bomb_exploded.blocks_destroyed.size());
                                          for (const position_t &block_pos : bomb_exploded.blocks_destroyed)
                                              write_position(block_pos);
                                      },
                                      [this](PlayerMoved &player_moved) {
                                          write_number<event_code_t>(event_code_t::PlayerMoved);
                                          write_number<player_id_t>(player_moved.player_id);
                                          write_position(player_moved.position);
                                      },
                                      [this](BlockPlaced &block_placed) {
                                          write_number<event_code_t>(event_code_t::BlockPlaced);
                                          write_position(block_placed.position);
                                      }},
                           event);
            }
        }

        void write_game_ended(GameEnded &game_ended)
        {
            write_number<server_message_code_t>(server_message_code_t::GameEnded);
            write_number<map_len_t>((map_len_t)game_ended.scores.size());
            for (auto &scores_map_entry : game_ended.scores)
            {
                write_number<player_id_t>(scores_map_entry.first);
                write_number<score_t>(scores_map_entry.second);
            }
        }
    };

} // namespace bomberman

#endif // BOMBERMAN_NET_H
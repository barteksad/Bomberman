#ifndef BOMBERMAN_SERVER_H
#define BOMBERMAN_SERVER_H

// This is just to silent boost pragma message about old version of this file.
// The correct version is included above.
#include <boost/core/scoped_enum.hpp>
#define BOOST_DETAIL_SCOPED_ENUM_EMULATION_HPP
// ---

#include "common.h"
#include "net.h"

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <list>
#include <map>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>

namespace bomberman
{

    struct robots_server_args_t
    {
        bomb_timer_t bomb_timer;
        players_count_t players_count;
        turn_duration_t turn_duration;
        explosion_radius_t explosion_radius;
        uint16_t initial_blocks;
        game_length_t game_length;
        std::string server_name;
        uint16_t port;
        uint32_t seed;
        size_x_t size_x;
        size_y_t size_y;
    };

    namespace
    {
        Hello hello_from_server_args(const robots_server_args_t &args)
        {
            return Hello(
                args.server_name,
                args.players_count,
                args.size_x,
                args.size_y,
                args.game_length,
                args.explosion_radius,
                args.bomb_timer);
        }
    } // namespace

    class RobotsServer
    {
    public:
        RobotsServer(robots_server_args_t &args, boost::asio::io_context &io_context)
            : args_(args),
              io_context_(io_context),
              random_(args.seed),
              acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v6(),
                                                                   args.port)),
              turn_timer_(io_context)
        {
            state_ = LOBBY;
            connect_loop();
        }

        void connect_loop()
        {
            acceptor_.async_accept(
                [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
                    if (!ec)
                    {
                        // Turn off Nagle's algorithm
                        boost::asio::ip::tcp::no_delay option(true);
                        socket.set_option(option);
                        handle_new_connection(std::move(socket));
                        connect_loop();
                    }
                    else
                    {
                        BOOST_LOG_TRIVIAL(debug) << "Error in connect_loop, " << ec.message();
                    }

                    connect_loop();
                });
        }

        void handle_new_connection(boost::asio::ip::tcp::socket &&socket)
        {
            // If server reached connection limit, disconnect new clients.
            if (open_connections_hm_.size() == MAX_SERVER_CONNECTIONS)
            {
                socket.close();
                BOOST_LOG_TRIVIAL(debug) << "Connection limit reached, closing new connection";
            }
            else
            {
                // Assign player id to new client and call read message from client loop.
                const player_id_t new_player_id = static_cast<player_id_t>(open_connections_hm_.size());
                open_connections_hm_.insert({new_player_id, std::move(socket)});
                messages_to_send_q_.push(
                    target_one_t{
                        .to_who = new_player_id,
                        .message = hello_from_server_args(args_)});

                // Send accepted player and turns to new player if needed.
                notify_new(new_player_id);

                // Spawn loop for handling connection from this client.
                auto spawn_callback =
                    [new_player_id, this](boost::asio::yield_context yield) {
                        read_message_from_client(yield, new_player_id);
                    };
                boost::asio::spawn(io_context_, spawn_callback);
            }
        }

        void read_message_from_client(boost::asio::yield_context yield, const player_id_t player_id)
        {
            // Create deserializer with client socket.
            TcpDeserializer tcp_deserializer(open_connections_hm_.at(player_id));

            while (true)
            {
                try
                {
                    // Receive message from client.
                    const client_message_t client_message = tcp_deserializer.get_client_message(yield);

                    // Server needs to process this message if it is in LOBBY state.
                    // In GAME state it automatically processes messages every turn-duration miliseconds.
                    clients_messages_hm_.insert({player_id, client_message});
                    if (state_ == LOBBY)
                    {
                        process_lobby();
                    }
                }
                catch (std::exception &e)
                {
                    open_connections_hm_.erase(player_id);
                    BOOST_LOG_TRIVIAL(debug) << "error processing player: " << player_id << " connection, boost error: " << e.what() << ". Disconnects.\n";
                    break;
                }
            }
        }

        void send_to_one(buffer_t &buffer, target_one_t &targeted_message)
        {
            // Send message to one specific client (endpoint).
            auto target = open_connections_hm_.find(targeted_message.to_who);
            if (target == open_connections_hm_.end())
                return;

            auto after_write_callback = [to_who = targeted_message.to_who, this](boost::system::error_code ec, std::size_t) {
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(debug) << "error sending to client " << to_who << ", disconnects";
                    open_connections_hm_.erase(to_who);
                }
            };
            boost::asio::async_write(target->second,
                                     boost::asio::buffer(buffer, buffer.size()),
                                     after_write_callback);
        }

        void send_to_all(buffer_t &buffer)
        {
            // Send message to all open connections.
            for (auto &[player_id, socket] : open_connections_hm_)
            {
                auto after_write_callback = [to_who = player_id, this](boost::system::error_code ec, std::size_t) {
                    if (ec)
                    {
                        BOOST_LOG_TRIVIAL(debug) << "error sending to client " << to_who << ", disconnects";
                        open_connections_hm_.erase(to_who);
                    }
                };
                boost::asio::async_write(socket,
                                         boost::asio::buffer(buffer, buffer.size()),
                                         after_write_callback);
            }
        }

        void send_messages()
        {
            // Send message to one or to all depending on message type.
            NetSerializer net_serializer;

            while (!messages_to_send_q_.empty())
            {
                targeted_message_t targeted_message = messages_to_send_q_.front();
                buffer_t buffer = net_serializer.serialize(targeted_message);
                if (std::holds_alternative<target_one_t>(targeted_message))
                    send_to_one(buffer, std::get<target_one_t>(targeted_message));
                else
                    send_to_all(buffer);

                messages_to_send_q_.pop();
            }
        }

        void notify_new(const player_id_t player_id)
        {
            // Send all accepted player messages currently stored.
            for (AcceptedPlayer &accpeted : accepted_player_messages_l_)
            {
                target_one_t notify_new{
                    .to_who = player_id,
                    .message = accpeted};
                messages_to_send_q_.push(notify_new);
            }

            // If game events is not empty, it game is in progress and we have to send all events.
            for (const Turn &turn : turn_messages_l_)
            {
                target_one_t notify_new{
                    .to_who = player_id,
                    .message = turn};
                messages_to_send_q_.push(notify_new);
            }

            send_messages();
        }

        void process_lobby()
        {
            assert(!clients_messages_hm_.empty());
            assert(state_ == LOBBY);

            std::unordered_set<player_id_t> processed_messages;

            // In lobby we only accept Join messages.
            for (const auto &[player_id, client_message] : clients_messages_hm_)
            {
                processed_messages.insert(player_id);

                // Ignore messages in LOBBY from accepted player.
                if (game_state_.players.contains(player_id))
                    continue;

                // Client wants to join the game.
                if (std::holds_alternative<Join>(client_message))
                {
                    Join join = std::get<Join>(client_message);
                    // Find their's socket to get address and port.
                    auto &socket = open_connections_hm_.at(player_id);
                    const std::string player_address = "[" + socket.remote_endpoint().address().to_string() + "]:" + std::to_string(socket.remote_endpoint().port());
                    BOOST_LOG_TRIVIAL(debug) << "New player address: " << player_address;
                    player_t new_player = player_t{.name = join.name, .address = player_address};

                    game_state_.players.insert({player_id, new_player});

                    // Send information about new cliento to everyone and store this message.
                    AcceptedPlayer accepted_player(player_id, new_player);
                    target_all_t notify_all{.message = accepted_player};
                    messages_to_send_q_.push(notify_all);
                    accepted_player_messages_l_.push_back(accepted_player);
                }

                if (game_state_.players.size() == args_.players_count)
                    break;
            }

            // Erase processed messages.
            std::erase_if(clients_messages_hm_, [&processed_messages](const auto &item) {
                auto const &[player_id, _] = item;
                return processed_messages.contains(player_id);
            });

            send_messages();

            if (game_state_.players.size() == args_.players_count)
                start_game();
        }

        void start_game()
        {
            // All variables should be zeroed in end_game()
            assert(game_state_.players.size() == args_.players_count);
            assert(game_state_.blocks.size() == 0);
            assert(game_state_.bombs.size() == 0);
            assert(game_state_.player_to_position.size() == 0);
            assert(game_state_.scores.size() == 0);
            assert(turn_messages_l_.size() == 0);

            messages_to_send_q_.push(target_all_t{.message = GameStarted(game_state_.players)});
            state_ = GAME;

            events_t events;

            // Generate random players positions and set their scores to zero.
            for (auto &[player_id, _] : game_state_.players)
            {
                position_t player_position{
                    .x = static_cast<size_x_t>(random_() % (long unsigned int)args_.size_x),
                    .y = static_cast<size_y_t>(random_() % (long unsigned int)args_.size_y),
                };
                game_state_.player_to_position.insert({player_id, player_position});
                game_state_.scores[player_id] = 0;
                events.push_back(PlayerMoved(player_id, player_position));
            }

            // Generate random blocks
            for (auto i = 0; i < args_.initial_blocks; i++)
            {
                position_t block_position{
                    .x = static_cast<size_x_t>(random_() % (long unsigned int)args_.size_x),
                    .y = static_cast<size_y_t>(random_() % (long unsigned int)args_.size_y),
                };
                game_state_.blocks.insert(block_position);
                events.push_back(BlockPlaced(block_position));
            }

            Turn turn(0, events);
            messages_to_send_q_.push(target_all_t{.message = turn});
            turn_messages_l_.push_back(turn);

            send_messages();

            // Set timer for turns.
            turn_timer_.expires_from_now(boost::posix_time::milliseconds(args_.turn_duration));
            turn_timer_.async_wait(boost::bind(&RobotsServer::process_one_turn, this, boost::asio::placeholders::error));
        }

        void end_game()
        {
            state_ = LOBBY;
            clients_messages_hm_.clear();
            accepted_player_messages_l_.clear();
            turn_messages_l_.clear();

            GameEnded game_ended(game_state_.scores);
            messages_to_send_q_.push(target_all_t{.message = game_ended});

            send_messages();

            game_state_.reset();
        }

        void process_bombs(robots_destroyed_t &robots_destroyed, blocks_destroyed_t &blocks_destroyed, events_t &events)
        {
            // Calculate exploding bombs effects.
            for (auto &[bomb_id, bomb] : game_state_.bombs)
            {
                // Bomb explodes.
                if (!--bomb.timer)
                {
                    BombExploded bomb_exploded;
                    bomb_exploded.bomb_id = bomb_id;
                    auto explosion_range = calculate_explosion_range(bomb.position, args_.explosion_radius, args_.size_x, args_.size_y, game_state_.blocks);
                    for (const auto &position : explosion_range)
                    {
                        if (game_state_.blocks.contains(position))
                        {
                            blocks_destroyed.insert(position);
                            bomb_exploded.blocks_destroyed.insert(position);
                        }
                        for (const auto &[player_id, player_position] : game_state_.player_to_position)
                        {
                            if (player_position == position)
                            {
                                robots_destroyed.insert(player_id);
                                bomb_exploded.robots_destroyed.insert(player_id);
                            }
                        }
                    }
                    events.push_back(bomb_exploded);
                }
            }

            std::erase_if(game_state_.bombs,
                          [](const auto &bomb_pair) {
                              return !bomb_pair.second.timer;
                          });
            std::erase_if(game_state_.blocks,
                          [&blocks_destroyed](const auto &block_position) {
                              return blocks_destroyed.contains(block_position);
                          });
        }

        std::optional<position_t> calculate_move(position_t position, Move &move)
        {
            int32_t x = static_cast<int32_t>(position.x);
            int32_t y = static_cast<int32_t>(position.y);
            switch (move.direction)
            {
            case bomberman::direction_t::Up:
                y++;
                break;
            case bomberman::direction_t::Right:
                x++;
                break;
            case bomberman::direction_t::Down:
                y--;
                break;
            case bomberman::direction_t::Left:
                x--;
                break;
            };

            if (x < 0 || y < 0 || x >= args_.size_x || y >= args_.size_y)
                return {};
            position.x = static_cast<size_x_t>(x);
            position.y = static_cast<size_y_t>(y);
            if (game_state_.blocks.contains(position))
                return {};
            else
                return position;
        }

        void process_player_turn(events_t &events, const player_id_t player_id, client_message_t &client_message)
        {
            std::visit(overloaded{
                           // Join message, ignore in game
                           [](Join &) {},
                           // PlaceBomb message
                           [player_id, &events, this](PlaceBomb &) {
                               const bomb_id_t bomb_id = game_state_.free_bomb_id++;
                               bomb_t bomb{
                                   .position = game_state_.player_to_position[player_id],
                                   .timer = args_.bomb_timer,
                               };
                               game_state_.bombs.insert({bomb_id, bomb});
                               events.push_back(BombPlaced(bomb_id, bomb.position));
                           },
                           // PlaceBlock message
                           [player_id, &events, this](PlaceBlock &) {
                               position_t block_position = game_state_.player_to_position[player_id];
                               if (game_state_.blocks.insert(block_position).second)
                               {
                                   events.push_back(BlockPlaced(block_position));
                               }
                           },
                           // Move message
                           [player_id, &events, this](Move &move) {
                               position_t position = game_state_.player_to_position[player_id];
                               auto new_position = calculate_move(position, move);
                               if (new_position)
                               {
                                   game_state_.player_to_position[player_id] = new_position.value();
                                   events.push_back(PlayerMoved(player_id, new_position.value()));
                               }
                           }},
                       client_message);
        }

        void process_players(robots_destroyed_t &robots_destroyed, events_t &events)
        {
            for (auto &[player_id, player] : game_state_.players)
            {
                if (!robots_destroyed.contains(player_id))
                {
                    auto player_message_it = clients_messages_hm_.find(player_id);
                    if (player_message_it != clients_messages_hm_.end())
                    {
                        process_player_turn(events, player_id, player_message_it->second);
                    }
                }
                else
                {
                    position_t player_new_position{
                        .x = static_cast<size_x_t>(random_() % (long unsigned int)args_.size_x),
                        .y = static_cast<size_y_t>(random_() % (long unsigned int)args_.size_y),
                    };
                    game_state_.player_to_position[player_id] = player_new_position;
                    events.push_back(PlayerMoved(player_id, player_new_position));
                }
            }
            clients_messages_hm_.clear();
        }

        void process_one_turn(const boost::system::error_code &ec)
        {
            if (ec)
            {
                BOOST_LOG_TRIVIAL(debug) << "Error in turn_timer_.async_wait , boost error:" << ec.message();
                throw TimerError("Error in turn_timer_.async_wait", ec);
            }

            robots_destroyed_t robots_destroyed;
            blocks_destroyed_t blocks_destroyed;
            events_t events;

            process_bombs(robots_destroyed, blocks_destroyed, events);
            process_players(robots_destroyed, events);
            // Add scores to players whose rob was destroyed.
            for (auto player_id : robots_destroyed)
                game_state_.scores[player_id]++;

            Turn turn(++game_state_.turn, events);
            turn_messages_l_.push_back(turn);

            messages_to_send_q_.push(target_all_t{.message = turn});
            send_messages();

            if (game_state_.turn == args_.game_length)
            {
                end_game();
            }
            else
            {
                turn_timer_.expires_at(turn_timer_.expires_at() + boost::posix_time::milliseconds(args_.turn_duration));
                turn_timer_.async_wait(boost::bind(&RobotsServer::process_one_turn, this, boost::asio::placeholders::error));
            }
        }

    private:
        const robots_server_args_t args_;
        boost::asio::io_context &io_context_;
        game_state_t game_state_;
        std::minstd_rand random_;
        boost::asio::ip::tcp::acceptor acceptor_;
        boost::asio::deadline_timer turn_timer_;
        std::unordered_map<player_id_t, client_message_t> clients_messages_hm_;
        std::list<AcceptedPlayer> accepted_player_messages_l_;
        std::list<Turn> turn_messages_l_;
        std::queue<targeted_message_t> messages_to_send_q_;
        std::unordered_map<player_id_t, boost::asio::ip::tcp::socket> open_connections_hm_;
        enum server_state_t
        {
            LOBBY,
            GAME,
        } state_;
    };

    robots_server_args_t get_server_arguments(int ac, char *av[])
    {
        boost::program_options::options_description desc("Usage");
        desc.add_options()("-h", "produce help message")("-b", boost::program_options::value<bomb_timer_t>(), "bomb-timer <u16>")("-c", boost::program_options::value<uint16_t>(), "players-count <u8>")("-d", boost::program_options::value<turn_duration_t>(), "turn-duration <u64, milisekundy>")("-e", boost::program_options::value<explosion_radius_t>(), "explosion-radius <u16>")("-k", boost::program_options::value<uint16_t>(), "initial-blocks <u16>")("-l", boost::program_options::value<game_length_t>(), "game-length <u16>")("-n", boost::program_options::value<std::string>(), "server-name <String>")("-p", boost::program_options::value<uint16_t>(), "port <u16>")("-s", boost::program_options::value<uint32_t>()->default_value(static_cast<uint32_t>(time(NULL))), "seed <u32, parametr opcjonalny>")("-x", boost::program_options::value<size_x_t>(), "size-x <u16>")("-y", boost::program_options::value<size_y_t>(), "size-y <u16>");

        boost::program_options::variables_map vm;
        boost::program_options::store(boost::program_options::parse_command_line(ac, av, desc), vm);
        boost::program_options::notify(vm);

        if (vm.count("help") || !(vm.count("-b") && vm.count("-c") && vm.count("-d") && vm.count("-e") && vm.count("-k") && vm.count("-l") && vm.count("-n") && vm.count("-p") && vm.count("-x") && vm.count("-y")))
        {
            std::cout << desc;
            exit(1);
        }

        robots_server_args_t args;

        args.bomb_timer = vm["-b"].as<bomb_timer_t>();
        uint16_t players_count = vm["-c"].as<uint16_t>();
        if (args.players_count > std::numeric_limits<uint8_t>::max())
            throw InvalidArguments("player counts must be unsigned 8-bits integer!");
        else
            args.players_count = static_cast<players_count_t>(players_count);
        args.turn_duration = vm["-d"].as<turn_duration_t>();
        args.explosion_radius = vm["-e"].as<explosion_radius_t>();
        args.initial_blocks = vm["-k"].as<uint16_t>();
        args.game_length = vm["-l"].as<game_length_t>();
        args.server_name = vm["-n"].as<std::string>();
        args.port = vm["-p"].as<uint16_t>();
        args.seed = vm["-s"].as<uint32_t>();
        args.size_x = vm["-x"].as<size_x_t>();
        args.size_y = vm["-y"].as<size_y_t>();

        BOOST_LOG_TRIVIAL(debug) << "Server run with arguments: "
                                 << "\nargs.bomb_timer " << args.bomb_timer
                                 << "\nargs.players_count " << args.players_count
                                 << "\nargs.turn_duration " << args.turn_duration
                                 << "\nargs.explosion_radius " << args.explosion_radius
                                 << "\nargs.initial_blocks " << args.initial_blocks
                                 << "\nargs.server_name " << args.server_name
                                 << "\nargs.port " << args.port
                                 << "\nargs.seed " << args.seed
                                 << "\nargs.size_x " << args.size_x
                                 << "\nargs.size_y " << args.size_y;

        return args;
    }

} // bomberman

#endif
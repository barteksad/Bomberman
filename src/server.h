#ifndef BOMBERMAN_SERVER_H
#define BOMBERMAN_SERVER_H

#include "common.h"
#include "net.h"

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>
#include <list>
#include <queue>

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

    struct target_one_t
    {
        player_id_t to_who;
        server_message_t message;
    };

    struct target_all_t
    {
        server_message_t message;
    };

    using targeted_message_t = std::variant<target_one_t, target_all_t>;

    namespace
    {
        Hello hello_from_server_args(const robots_server_args_t& args)
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
                        .message=hello_from_server_args(args_)
                        });
                
                notify_new(new_player_id, true);

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

            while(true)
            {
                try
                {
                    const client_message_t client_message = tcp_deserializer.get_client_message(yield);
                    
                    // Server needs to process this message if it is in LOBBY state.
                    // In GAME state it automatically processes messages every turn-duration miliseconds.
                    bool handle_in_progress = !clients_messages_hm_.empty();
                    clients_messages_hm_.insert({player_id, client_message});
                    if(state_ == LOBBY && !handle_in_progress)
                    {
                        process_lobby();
                    }
                }
                catch(std::exception &e)
                {
                    open_connections_hm_.erase(player_id);
                    BOOST_LOG_TRIVIAL(debug) << "error processing player: " << player_id << " connection, what: " << e.what() << ". Disconnects.\n";
                }
            }
        }

        void send_messages()
        {
        }

        void notify_new(const player_id_t player_id, bool call_send_message=false)
        {
            // Send all accepted player messages currently stored.
            bool send_in_progress = !messages_to_send_q_.empty();
            for(AcceptedPlayer &accpeted : accepted_player_messages_l_)
            {
                target_one_t notify_new{
                    .to_who = player_id,
                    .message = accpeted
                };
                messages_to_send_q_.push(notify_new);
            }
            
            // If game events is not empty, it game is in progress and we have to send all events.
            for(const Turn & turn : turn_messages_l_)
            {
                target_one_t notify_new{
                    .to_who = player_id,
                    .message = turn
                };
                messages_to_send_q_.push(notify_new);
            }

            if(call_send_message && !send_in_progress && !messages_to_send_q_.empty())
            {
                send_messages();
            }
        }

        void process_lobby()
        {
            assert(!clients_messages_hm_.empty());
            assert(state_ == LOBBY);

            bool send_in_progress = !messages_to_send_q_.size();
            std::unordered_set<player_id_t> processed_messages;

            // In lobby we only accept Join messages.
            for(const auto &[player_id, client_message] : clients_messages_hm_)
            {
                processed_messages.insert(player_id);

                // Ignore messages in LOBBY from accepted player.
                if(game_state_.players.contains(player_id))
                    continue;

                if(std::holds_alternative<Join>(client_message))
                {
                    Join join = std::get<Join>(client_message);
                    auto &socket = open_connections_hm_.at(player_id);
                    const std::string player_address = socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
                    player_t new_player =  player_t{.name = join.name, .address = player_address};
                    
                    game_state_.players.insert({player_id, new_player});
                    // Send previus accepted player messages to new client.
                    for(AcceptedPlayer &accpeted : accepted_player_messages_l_)
                    {
                        target_one_t notify_new{
                            .to_who = player_id,
                            .message = accpeted
                        };
                        messages_to_send_q_.push(notify_new);
                    }
                    // Send information about new cliento to everyone and store this message.
                    AcceptedPlayer accepted_player(player_id, new_player);
                    accepted_player_messages_l_.push_back(accepted_player);
                    messages_to_send_q_.push(target_all_t{.message = accepted_player});
                }

                if(game_state_.players.size() == args_.players_count)
                    break;
            }

            // Erase processed messages.
            std::erase_if(clients_messages_hm_, [&processed_messages](const auto& item)
            { 
                auto const& [player_id, _] = item;
                return processed_messages.contains(player_id);
            });

            if(!send_in_progress && !messages_to_send_q_.empty())
            {
                send_messages();
            }

            if(game_state_.players.size() == args_.players_count)
                start_game();
        }

        void start_game()
        {
            assert(game_state_.players.size() == args_.players_count);
            assert(game_state_.blocks.size() == 0);
            assert(game_state_.bombs.size() == 0);
            assert(game_state_.player_to_position.size() == 0);
            assert(game_state_.scores.size() == 0);
            assert(turn_messages_l_.size() == 0);

            messages_to_send_q_.push(target_all_t{.message = GameStarted(game_state_.players)});
            state_ = GAME;


        }

        void process_one_turn()
        {
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
        desc.add_options()("-h", "produce help message")("-b", boost::program_options::value<bomb_timer_t>(), "bomb-timer <u16>")("-c", boost::program_options::value<players_count_t>(), "players-count <u8>")("-d", boost::program_options::value<turn_duration_t>(), "turn-duration <u64, milisekundy>")("-e", boost::program_options::value<explosion_radius_t>(), "explosion-radius <u16>")("-k", boost::program_options::value<uint16_t>(), "initial-blocks <u16>")("-l", boost::program_options::value<game_length_t>(), "game-length <u16>")("-n", boost::program_options::value<std::string>(), "server-name <String>")("-p", boost::program_options::value<uint16_t>(), "port <u16>")("-s", boost::program_options::value<uint32_t>()->default_value(static_cast<uint32_t>(time(NULL))), "seed <u32, parametr opcjonalny>")("-x", boost::program_options::value<size_x_t>(), "size-x <u16>")("-y", boost::program_options::value<size_y_t>(), "size-y <u16>");

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
        args.players_count = vm["-c"].as<players_count_t>();
        args.turn_duration = vm["-d"].as<turn_duration_t>();
        args.explosion_radius = vm["-e"].as<explosion_radius_t>();
        args.initial_blocks = vm["-k"].as<uint16_t>();
        args.game_length = vm["-l"].as<game_length_t>();
        args.server_name = vm["-n"].as<std::string>();
        args.port = vm["-p"].as<uint16_t>();
        args.seed = vm["-s"].as<uint32_t>();
        args.size_x = vm["-x"].as<size_x_t>();
        args.size_y = vm["-y"].as<size_y_t>();

        return args;
    }

} // bomberman

#endif
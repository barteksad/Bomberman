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
        struct target_one_t
        {
            player_id_t to_who;
            server_message_t message;
        };

        struct target_all
        {
            server_message_t message;
        };

        struct received_message_t
        {
            player_id_t from_who;
            client_message_t message;
        };

        using targeted_message_t = std::variant<target_one_t, target_all>;

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
                        boost::asio::ip::tcp::no_delay option(true);
                        socket.set_option(option);
                        // This lambda looks weird but mutable is needed to pass socket in spawn
                        auto spawn_callback =
                            [this, socket = std::move(socket)](boost::asio::yield_context yield) mutable {
                                handle_new_connection(yield, std::move(socket));
                            };
                        boost::asio::spawn(io_context_, spawn_callback);
                    }
                    else
                    {
                        BOOST_LOG_TRIVIAL(debug) << "Error in connect_loop, " << ec.message();
                    }

                    connect_loop();
                });
        }

        void handle_new_connection(boost::asio::yield_context yield, boost::asio::ip::tcp::socket &&socket)
        {
            if (open_connections_.size() == MAX_SERVER_CONNECTIONS)
            {
                socket.close();
                BOOST_LOG_TRIVIAL(debug) << "Connection limit reached, closing new connection";
            }
            else
            {
                TcpDeserializer tcp_deserializer(socket);
                client_message_t first_message = tcp_deserializer.get_client_message(yield);
                if(if const Join* join = std::get_if<Join>(&first_message))
                {
                    const player_id_t new_player_id = open_connections_.size();
                    open_connections_.insert({new_player_id, socket});
                    game_state_.players.insert({new_player_id, player_t{.name = join.name, .address = socket.remote_endpoint()}});
                }
                else
                {
                    socket.close();
                    BOOST_LOG_TRIVIAL(debug) << "New client did not send Join message as first message. Disconnects.";
                }
            }
        }

        void read_message_from_client()
        {
        }

        void send_message()
        {
        }

        void add_message(client_message_t client_message)
        {
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
        std::unordered_map<player_id_t, client_message_t> clients_messages_;
        std::unordered_map<player_id_t, boost::asio::ip::tcp::socket> open_connections_;
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
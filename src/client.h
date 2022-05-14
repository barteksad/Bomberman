#ifndef BOMBERMAN_CLIENT_H
#define BOMBERMAN_CLIENT_H

#include "common.h"
#include "messages.h"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/bind/placeholders.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <queue>
#include <memory>

// namespace
// {

//     // https://en.cppreference.com/w/cpp/utility/variant/visit
//     template <class... Ts>
//     struct overloaded : Ts...
//     {
//         using Ts::operator()...;
//     };
//     template <class... Ts>
//     overloaded(Ts...) -> overloaded<Ts...>;

// }

namespace bomberman
{

    class RobotsClient
    {
    public:
        RobotsClient(boost::asio::io_context &io_context,
                     std::string &server_endpoint_input,
                     std::string &gui_endpoint_input,
                     std::string &player_name,
                     uint16_t port)
            : io_context_(io_context),
              server_socket_(io_context),
              gui_socket_(io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)),
              server_deserializer_(server_socket_),
              gui_deserializer_(gui_socket_),
              player_name_(player_name),
              state(LOBBY)
        {

            // connect to GUI
            boost::asio::ip::udp::resolver gui_resolver(io_context);
            auto gui_split_idx = gui_endpoint_input.find(":"); // host:port
            gui_endpoints_iter_ = gui_resolver.resolve(gui_endpoint_input.substr(0, gui_split_idx), gui_endpoint_input.substr(gui_split_idx + 1));
            boost::asio::post(boost::bind(&RobotsClient::read_from_gui, this));

            // connect to server
            boost::asio::ip::tcp::resolver server_resolver(io_context);
            auto server_split_idx = server_endpoint_input.find(":"); // host:port
            auto server_endpoints = server_resolver.resolve(server_endpoint_input.substr(0, server_split_idx), server_endpoint_input.substr(server_split_idx + 1));
            boost::asio::async_connect(server_socket_, server_endpoints,
                                       [this](boost::system::error_code ec, const boost::asio::ip::tcp::endpoint &endpoint)
                                       {
                                           if (!ec)
                                           {
                                               BOOST_LOG_TRIVIAL(info) << "Successfully connected to server at: " << endpoint;
                                               boost::asio::spawn(io_context_, [this](boost::asio::yield_context yield){read_from_server(yield);});
                                           }
                                           else
                                           {
                                               throw ConnectError("server");
                                           }
                                       });
        }

        void read_from_server(boost::asio::yield_context yield)
        {
            auto message_handle_callback = [this, yield](const server_message_t msg)
            {
                server_messages_q_.push(msg);
                if (!server_messages_q_.empty())
                {
                    // order handling message
                    boost::asio::post(boost::bind(&RobotsClient::handle_server_message, this));
                }

                BOOST_LOG_TRIVIAL(debug) << "starting read_from_server again";
                read_from_server(yield);
            };

            BOOST_LOG_TRIVIAL(debug) << "in read_from_server, calling tcp_deserializer_.get_server_message";
            server_deserializer_.get_server_message(message_handle_callback, yield);
        }

        void read_from_gui()
        {
            auto message_handle_callback = [this](const input_message_t msg)
            {
                input_messages_q_.push(msg);
                if (!input_messages_q_.empty())
                {
                    // order handling message
                    boost::asio::post(boost::bind(&RobotsClient::handle_gui_message, this));
                }

                BOOST_LOG_TRIVIAL(debug) << "starting read_from_gui again";
                read_from_gui();
            };

            BOOST_LOG_TRIVIAL(debug) << "in read_from_gui, calling gui_deserializer_.get_message";
            gui_deserializer_.get_message(message_handle_callback);
        }

        void handle_gui_message()
        {
            while (!input_messages_q_.empty())
            {
                if (state == LOBBY)
                {
                    client_messages_q_.push(Join{player_name_});
                    input_messages_q_ = std::queue<input_message_t>();
                }
                else if (state == IN_GAME)
                {
                    input_message_t input_message = input_messages_q_.front();
                    std::visit(overloaded{
                                   [this](PlaceBomb &)
                                   { client_messages_q_.push(PlaceBomb{}); },
                                   [this](PlaceBlock &)
                                   { client_messages_q_.push(PlaceBlock{}); },
                                   [this](Move &msg)
                                   { client_messages_q_.push(Move{msg.direction}); },
                               },
                               input_message);
                    input_messages_q_.pop();
                }
                else if (state == OBSERVE)
                {
                    // do nothing
                    input_messages_q_ = std::queue<input_message_t>();
                    client_messages_q_ = std::queue<client_message_t>();
                }

            }

            if (!client_messages_q_.empty())
            {
                boost::asio::post([this]()
                                  { send_to_server(); });
            }
        }

        void handle_server_message()
        {
            BOOST_LOG_TRIVIAL(debug) << "in handle server message!";
            while(!server_messages_q_.empty())
            {
                std::visit(overloaded{
                    [this](Hello& hello)
                    {
                        game_state = game_state_t(hello); // reset game state
                    },
                    [this](AcceptedPlayer &accepted_player)
                    {
                        game_state.players.insert({accepted_player.player_id, accepted_player.player});
                        Lobby lobby(game_state.hello, game_state.players);
                        draw_messages_q_.push(lobby);
                    },
                    [this](GameStarted &game_started)
                    {
                        game_state.players = game_started.players;
                        state = client_state_t::IN_GAME;
                        for(auto& players_map_entry: game_state.players)
                            game_state.scores.insert({players_map_entry.first, 0});
                    },
                    [this](Turn &turn)
                    {   
                        Game game(game_state.hello, turn.turn, game_state.players);
                        for(event_t &event: turn.events)
                        {   
                            std::visit(overloaded{
                                [this](BombPlaced &bomb_placed)
                                {
                                    game_state.bombs.push_back(bomb_t{.position = bomb_placed.position, .timer=game_state.hello.bomb_timer});
                                    game_state.id_to_bomb_pos.insert({bomb_placed.bomb_id, bomb_placed.position});
                                },
                                [&game, this](BombExploded &bomb_exploded)
                                {
                                    auto bomb_pos_it = game_state.id_to_bomb_pos.find(bomb_exploded.bomb_id);
                                    if(bomb_pos_it ==  game_state.id_to_bomb_pos.end())
                                        throw InvalidMessage("Server");
                                    auto bomb_it = std::find_if(game_state.bombs.begin(), game_state.bombs.end(), [&](bomb_t &bomb) {return bomb.position == bomb_pos_it->second; });
                                    if(bomb_it ==  game_state.bombs.end())
                                        throw InvalidMessage("Server");
                                    game.explosions.push_back(bomb_pos_it->second);
                                    for(player_id_t &robot_destroyed : bomb_exploded.robots_destroyed)
                                    {
                                        game_state.scores[robot_destroyed]++;
                                        game_state.player_to_position.erase(robot_destroyed);
                                    }
                                    for(position_t &block_destroyed : bomb_exploded.blocks_destroyed)
                                        game_state.blocks.remove(block_destroyed);
                                    game_state.id_to_bomb_pos.erase(bomb_pos_it);
                                    game_state.bombs.erase(bomb_it);
                                },
                                [this](PlayerMoved &player_moved)
                                {
                                    game_state.player_to_position[player_moved.player_id] = player_moved.position;
                                },
                                [this](BlockPlaced &block_placed)
                                {
                                    game_state.blocks.push_back(block_placed.position);
                                },
                            }, event);
                        }
                        game.players_positions = game_state.player_to_position;
                        game.blocks = game_state.blocks;
                        game.bombs = game_state.bombs;
                        game.scores = game_state.scores;
                        draw_messages_q_.push(game);
                    },
                    [this](GameEnded &game_ended)
                    {
                        game_state.scores = game_ended.scores;
                        state = client_state_t::LOBBY;
                    }
                }, server_messages_q_.front()); 

                if(!draw_messages_q_.empty())
                {
                    boost::asio::post(boost::bind(&RobotsClient::send_to_gui, this));
                }
                server_messages_q_.pop();
            }
        }

        void send_to_server()
        {
            assert(!client_messages_q_.empty());
            NetSerializer net_serializer;
            buffer_t buffer = net_serializer.serialize(client_messages_q_.front());
            BOOST_LOG_TRIVIAL(debug) << "start sending to server " << buffer.size() << " bytes";
            boost::asio::async_write(server_socket_, boost::asio::buffer(buffer, buffer.size()), [this](boost::system::error_code ec, std::size_t)
                                     {
                if(!ec)
                {

                client_messages_q_.pop();
                if(!client_messages_q_.empty())
                    send_to_server();
                }
                else
                {
                    throw SendError("Server");
                } });
        }

        void send_to_gui()
        {
            assert(!draw_messages_q_.empty());

            while(!draw_messages_q_.empty())
            {
                NetSerializer net_serializer;
                buffer_t buffer = net_serializer.serialize(draw_messages_q_.front());
                BOOST_LOG_TRIVIAL(debug) << "start sending to gui " << buffer.size() << " bytes";
                gui_socket_.send_to(boost::asio::buffer(buffer, buffer.size()), gui_endpoints_iter_->endpoint());
                draw_messages_q_.pop();
            }
        }
        
        ~RobotsClient()
        {
            server_socket_.close();
            gui_socket_.close();
        }

    // TODO!
    // private:
        boost::asio::io_context &io_context_;
        boost::asio::ip::tcp::socket server_socket_;
        boost::asio::ip::udp::socket gui_socket_;
        bomberman::TcpDeserializer server_deserializer_;
        bomberman::UdpDeserializer gui_deserializer_;
        std::string player_name_;
        enum client_state_t
        {
            LOBBY,
            IN_GAME,
            OBSERVE
        } state;
        boost::asio::ip::udp::resolver::iterator gui_endpoints_iter_;
        std::queue<input_message_t> input_messages_q_;
        std::queue<client_message_t> client_messages_q_;
        std::queue<server_message_t> server_messages_q_;
        std::queue<draw_message_t> draw_messages_q_;
        struct game_state_t{
            game_state_t(){}
            game_state_t(Hello &_hello)
                : hello(_hello) {}
            Hello hello;
            players_t players;
            blocks_t blocks;
            bombs_t bombs;
            id_to_bomb_pos_t id_to_bomb_pos;
            player_to_position_t player_to_position;
            scores_t scores;
        } game_state;
    };

} // namespace bomberman

#endif // BOMBERMAN_CLIENT_H

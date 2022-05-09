#ifndef BOMBERMAN_CLIENT_H
#define BOMBERMAN_CLIENT_H

#include "common.h"
#include "messages.h"

#include <boost/asio.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

namespace bomberman {

    class RobotsClient {
    public:
        RobotsClient(boost::asio::io_context &io_context,
                     std::string &server_endpoint_input,
                     std::string &gui_endpoint_input,
                     std::string &player_name,
                     uint16_t port)
                : io_context_(io_context),
                server_socket_(io_context),
                gui_socket_(io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)),
                  gui_deserializer_(gui_socket_),
                player_name_(player_name),
                state(LOBBY) {

            // connect to GUI
            boost::asio::ip::udp::resolver gui_resolver(io_context);
            auto gui_split_idx = gui_endpoint_input.find(":"); // host:port
            auto gui_endpoints = gui_resolver.resolve(gui_endpoint_input.substr(0, gui_split_idx), gui_endpoint_input.substr(gui_split_idx + 1));
            boost::asio::async_connect(gui_socket_, gui_endpoints,
                                       [this](boost::system::error_code ec, const boost::asio::ip::udp::endpoint& endpoint)
                                       {
                                            if(!ec)
                                            {
                                                BOOST_LOG_TRIVIAL(info) << "Successfully connected to gui at: " << endpoint << "\n";
                                                read_from_gui();
                                            }
                                            else
                                            {
                                                throw ConnectError("GUI");
                                            }
                                       });

            // connect to server
            boost::asio::ip::tcp::resolver server_resolver(io_context);
            auto server_split_idx = server_endpoint_input.find(":"); // host:port
            auto server_endpoints = server_resolver.resolve(server_endpoint_input.substr(0, server_split_idx), server_endpoint_input.substr(server_split_idx + 1));
            boost::asio::async_connect(server_socket_, server_endpoints,
                                       [this](boost::system::error_code ec, const boost::asio::ip::tcp::endpoint& endpoint)
                                       {
                                           if(!ec)
                                           {
                                               BOOST_LOG_TRIVIAL(info) << "Successfully connected to server at: " << endpoint << "\n";
                                               read_header_from_server();
                                           }
                                           else
                                           {
                                               throw ConnectError("server");
                                           }
                                       });
        }

        void read_header_from_server()
        {
//            boost::asio::async_read()
        }
        void read_from_gui()
        {

        }

    private:

        boost::asio::io_context &io_context_;
        boost::asio::ip::tcp::socket server_socket_;
        boost::asio::ip::udp::socket gui_socket_;
        UdpDeserializer gui_deserializer_;
        std::string player_name_;
        enum client_state_t {
            LOBBY, IN_GAME, OBSERVE
        } state;
    };

} // namespace bomberman

#endif //BOMBERMAN_CLIENT_H

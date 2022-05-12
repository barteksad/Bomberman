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

#include <unistd.h> // getpid
namespace
{

    // https://en.cppreference.com/w/cpp/utility/variant/visit
    template <class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };
    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

}

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
                if (server_messages_q_.size() == 1)
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
                if (input_messages_q_.size() == 1)
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
                }
                else if (state == OBSERVE)
                {
                    // do nothing
                }

                input_messages_q_.pop();
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
        }

        void send_to_server()
        {
            NetSerializer net_serializer;
            assert(!client_messages_q_.empty());
            buffer_t buffer = net_serializer.serialize(client_messages_q_.front());
            BOOST_LOG_TRIVIAL(debug) << "start sending to server!";
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
        
        ~RobotsClient()
        {
            server_socket_.close();
            gui_socket_.close();
        }


    private:
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
    };

} // namespace bomberman

#endif // BOMBERMAN_CLIENT_H

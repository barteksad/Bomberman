#include "client.h"

#include <boost/program_options.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <iostream>


void init_logging()
{
    boost::log::core::get()->set_filter
            (
                    boost::log::trivial::severity  >= boost::log::trivial::debug
            );
}

// -d 127.0.0.1:4200 -n Bartek -p 4300 -s 127.0.0.1:4400
int main(int ac, char *av[])
{
    try
    {
        // process input parameters
        init_logging();

        boost::program_options::options_description desc("Usage");
        desc.add_options()("h", "produce help message")("-d", boost::program_options::value<std::string>(), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>")("-n", boost::program_options::value<std::string>(), "player name")("-p", boost::program_options::value<uint16_t>(), "port number")("-s", boost::program_options::value<std::string>(), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>");

        boost::program_options::variables_map vm;
        boost::program_options::store(boost::program_options::parse_command_line(ac, av, desc), vm);
        boost::program_options::notify(vm);

        if (vm.count("help") || !(vm.count("-d") && vm.count("-n") && vm.count("-p") && vm.count("-s")))
        {
            std::cout << desc;
            return 0;
        }

        std::string server_endpoint_input, gui_endpoint_input, player_name;
        uint16_t port;

        server_endpoint_input = vm["-s"].as<std::string>();
        gui_endpoint_input = vm["-d"].as<std::string>();
        player_name = vm["-n"].as<std::string>();
        port = vm["-p"].as<uint16_t>();

        BOOST_LOG_TRIVIAL(info) << "Run with arguments, server_endpoint_input: " << server_endpoint_input
                                << ", gui_endpoint_input: " << gui_endpoint_input
                                << ", player_name: " << player_name
                                << ", port: " << port;

        // run client
        boost::asio::io_context io_context;
        bomberman::RobotsClient robot_client(io_context, server_endpoint_input, gui_endpoint_input, player_name, port);
        io_context.run();
    }
    catch (std::exception &e)
    {
        BOOST_LOG_TRIVIAL(fatal) << "error: " << e.what() << "\n";
        return 1;
    }
    catch (...)
    {
        BOOST_LOG_TRIVIAL(fatal) << "Exception of unknown type!\n";
    }

}
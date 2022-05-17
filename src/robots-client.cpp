#include "client.h"

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <iostream>


void init_logging()
{
    boost::log::core::get()->set_filter
            (
                    // for debug
                    boost::log::trivial::severity  >= boost::log::trivial::debug

                    // for release (no logs)
                    // boost::log::trivial::severity > boost::log::trivial::fatal
            );
}

int main(int ac, char *av[])
{
    try
    {
        init_logging();
        
        bomberman::robots_client_args_t args = bomberman::get_client_arguments(ac, av);
        boost::asio::io_context io_context;

        bomberman::RobotsClient robot_client(io_context, args);
        io_context.run();
    }
    catch (std::exception &e)
    {
        BOOST_LOG_TRIVIAL(fatal) << "error: " << e.what() << "\n";
        std::cerr << e.what();
        return 1;
    }
    catch (...)
    {
        BOOST_LOG_TRIVIAL(fatal) << "Exception of unknown type!\n";
        std::cerr << "unknown problem\n";
        return 1;
    }

}
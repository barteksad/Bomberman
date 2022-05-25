#include "client.h"

#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>

#include <iostream>

namespace
{

    #ifdef NDEBUG
    static const bool ROBOTS_DEBUG = false;
    #else
    static const bool ROBOTS_DEBUG = true;
    #endif

    void init_logging()
    {
        if(ROBOTS_DEBUG)
        {
            // Log debug informations.
            boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::debug);
        }
        else
        {
            // No logs.
            boost::log::core::get()->set_filter( boost::log::trivial::severity > boost::log::trivial::fatal);
        }
    }
    
} // namespace

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
        std::cerr << e.what() << "\n";
        return 1;
    }
    catch (...)
    {
        BOOST_LOG_TRIVIAL(fatal) << "Exception of unknown type!\n";
        std::cerr << "unknown problem\n";
        return 1;
    }

    return 0;
}
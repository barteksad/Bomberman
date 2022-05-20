#include "server.h"

#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>

#include <iostream>

namespace
{
    void init_logging()
    {
        boost::log::core::get()->set_filter(
            boost::log::trivial::severity >= boost::log::trivial::debug);
    }
} // namespace

int main(int ac, char *av[])
{
    try
    {
        init_logging();
        bomberman::robots_server_args_t args = bomberman::get_server_arguments(ac, av);
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
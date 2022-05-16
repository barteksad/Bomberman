#ifndef BOMBERMAN_ERRORS_H
#define BOMBERMAN_ERRORS_H

#include <boost/asio/error.hpp>

namespace bomberman
{

    class ConnectError : public std::runtime_error
    {
    public:
        explicit ConnectError(std::string &&to, boost::system::error_code ec)
            : std::runtime_error("Failed to connect to " + to + ", boost error message: " + ec.message()) {}
    };

    class ReceiveError : public std::runtime_error
    {
    public:
        explicit ReceiveError(std::string &&from, boost::system::error_code ec)
            : std::runtime_error("Failed to receive message from " + from + ", boost error message: " + ec.message()) {}
    };

    class InvalidMessage : public std::runtime_error
    {
    public:
        explicit InvalidMessage(std::string &&from)
            : std::runtime_error("Received invalid message from " + from) {}
    };

    class SendError : public std::runtime_error
    {
    public:
        explicit SendError(std::string &&to, boost::system::error_code &ec)
            : std::runtime_error("Error sending message to " + to + ", boost error message: " + ec.message()) {}
    };

    class InvalidArguments : public std::invalid_argument
    {
        public:
            explicit InvalidArguments(std::string && description, boost::system::error_code ec)
                : std::invalid_argument(description + ", boost error message: " + ec.message()) {}
    };

} // namespace bomberman

#endif

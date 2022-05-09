#ifndef BOMBERMAN_COMMON_H
#define BOMBERMAN_COMMON_H

#include <boost/asio.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <exception>

namespace bomberman {

    using buffer_t = std::vector<char>;

    struct position_t {
        uint16_t x, y;
    };

    struct player_t {
        std::string name, address;
    };

    using server_name = std::string;
    using str_len_t = uint8_t;
    using message_t = uint8_t;
    using players_count_t = uint8_t;
    using player_id_t = uint8_t;
    using size_x_t = uint16_t;
    using size_y_t = uint16_t;
    using game_length_t = uint16_t;
    using explosion_radius_t = uint16_t;
    using bomb_timer_t = uint16_t;
    using turn_t = uint16_t;
    using bomb_id_t = uint32_t;
    using score_t = uint32_t;
    using map_len_t = uint32_t;
    using list_len_t = uint32_t;

    using players_t = std::unordered_map<player_id_t, player_t>;
    using robots_destroyed_t = std::unordered_set<player_id_t>;
    using blocks_destroyed_t = std::unordered_set<position_t>;

    enum class direction_t : uint8_t {
        Up = 0,
        Right = 1,
        Down = 2,
        Left = 3
    };

    class ConnectError : public std::runtime_error
    {
    public:
        explicit ConnectError(std::string &&to)
                : std::runtime_error("Failed to connect to " + to + " " ) {}
    };

    class ReceiveError : public std::runtime_error
    {
    public:
        explicit ReceiveError(std::string &&from)
        :    std::runtime_error("Failed to receive message from " + from + " ") {}

    };
} // bomberman

#endif //BOMBERMAN_COMMON_H

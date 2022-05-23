#ifndef BOMBERMAN_TYPES_H
#define BOMBERMAN_TYPES_H

#include <boost/asio.hpp>

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace bomberman
{

    using buffer_t = std::vector<char>;

    using server_name = std::string;
    using str_len_t = uint8_t;
    using message_code_t = uint8_t;
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
    using turn_duration_t = uint64_t;

    struct position_t
    {
        uint16_t x, y;

        bool operator==(const position_t &other) const noexcept
        {
            return (x == other.x) & (y == other.y);
        }
        struct hash
        {
            std::size_t operator()(const position_t &position) const noexcept
            {
                return std::hash<uint16_t>{}(position.x) ^ std::hash<uint16_t>{}(position.y);
            }
        };
    };

    struct player_t
    {
        std::string name, address;
    };

    struct bomb_t
    {
        position_t position;
        bomb_timer_t timer;
    };

    enum class direction_t : uint8_t
    {
        Up = 0,
        Right = 1,
        Down = 2,
        Left = 3
    };

    enum class input_message_code_t : message_code_t
    {
        PlaceBomb = 0,
        PlaceBlock = 1,
        Move = 2
    };

    enum class server_message_code_t : message_code_t
    {
        Hello = 0,
        AcceptedPlayer = 1,
        GameStarted = 2,
        Turn = 3,
        GameEnded = 4
    };

    enum class client_message_code_t : message_code_t
    {
        Join = 0,
        PlaceBomb = 1,
        PlaceBlock = 2,
        Move = 3
    };

    enum class draw_message_code_t : message_code_t
    {
        Lobby = 0,
        Game = 1,
    };

    enum class event_code_t : message_code_t
    {
        BombPlaced = 0,
        BombExploded = 1,
        PlayerMoved = 2,
        BlockPlaced = 3
    };

    using players_t = std::map<player_id_t, player_t>;
    using robots_destroyed_t = std::unordered_set<player_id_t>;
    using blocks_destroyed_t = std::unordered_set<position_t, position_t::hash>;
    using player_positions_t = std::unordered_map<player_id_t, position_t>;
    using id_to_bomb_pos_t = std::unordered_map<bomb_id_t, position_t>;
    using player_to_position_t = std::unordered_map<player_id_t, position_t>;
    using explosions_t = std::unordered_set<position_t, position_t::hash>;
    using blocks_t = std::unordered_set<position_t, position_t::hash>;
    using bombs_t = std::unordered_map<bomb_id_t, bomb_t>;
    using scores_t = std::unordered_map<player_id_t, score_t>;

    struct BombPlaced
    {
        BombPlaced(){};
        BombPlaced(bomb_id_t _bomb_id, position_t _position)
            : bomb_id(_bomb_id), position(_position) {}
        bomb_id_t bomb_id;
        position_t position;
    };

    struct BombExploded
    {
        BombExploded(){};
        BombExploded(bomb_id_t _bomb_id,
                     robots_destroyed_t &_robots_destroyed,
                     blocks_destroyed_t &_blocks_destroyed)
            : bomb_id(_bomb_id), robots_destroyed(_robots_destroyed), blocks_destroyed(_blocks_destroyed) {}
        bomb_id_t bomb_id;
        robots_destroyed_t robots_destroyed;
        blocks_destroyed_t blocks_destroyed;
    };
    struct PlayerMoved
    {
        PlayerMoved(){};
        PlayerMoved(player_id_t _player_id, position_t _position)
            : player_id(_player_id), position(_position) {}
        player_id_t player_id;
        position_t position;
    };
    struct BlockPlaced
    {
        BlockPlaced(){};
        BlockPlaced(position_t _position)
            : position(_position) {}
        position_t position;
    };
    using event_t = std::variant<BombPlaced, BombExploded, PlayerMoved, BlockPlaced>;
    using events_t = std::list<event_t>;

    // Use preprocessing to silent warnings if target is only client or server.
    #ifdef ROBOTS_CLIENT
    // size of move message
    static constinit std::size_t MAX_GUI_TO_CLIENT_MESSAGE_SIZE = sizeof(message_code_t) + sizeof(direction_t);
    #endif

    #ifdef ROBOTS_SERVER
    static constinit std::size_t MAX_SERVER_CONNECTIONS = 25;
    #endif
} // bomberman

#endif // BOMBERMAN_TYPES_H

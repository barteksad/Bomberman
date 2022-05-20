#ifndef BOMBERMAN_MESSAGES_H
#define BOMBERMAN_MESSAGES_H

#include "types.h"

#include <boost/asio.hpp>
#include <boost/log/trivial.hpp>

#include <functional>
#include <queue>
#include <variant>

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

} // namespace

namespace bomberman
{

    struct Join
    {
        Join(){};
        explicit Join(std::string &_name)
            : name(_name) {}
        std::string name;
    };

    struct PlaceBomb
    {
    };

    struct PlaceBlock
    {
    };

    struct Move
    {
        Move(){};
        explicit Move(direction_t _direction)
            : direction(_direction) {}
        direction_t direction;
    };

    struct Hello
    {
        Hello(){};
        Hello(std::string _server_name,
              players_count_t _players_count,
              size_x_t _size_x,
              size_y_t _size_y,
              game_length_t _game_length,
              explosion_radius_t _explosion_radius,
              bomb_timer_t _bomb_timer)
            : server_name(_server_name), players_count(_players_count), size_x(_size_x), size_y(_size_y), game_length(_game_length), explosion_radius(_explosion_radius), bomb_timer(_bomb_timer) {}
        std::string server_name;
        players_count_t players_count;
        size_x_t size_x;
        size_y_t size_y;
        game_length_t game_length;
        explosion_radius_t explosion_radius;
        bomb_timer_t bomb_timer;
    };

    struct AcceptedPlayer
    {
        AcceptedPlayer(){};
        AcceptedPlayer(player_id_t _player_id, player_t _player)
            : player_id(_player_id), player(_player) {}
        player_id_t player_id;
        player_t player;
    };

    struct GameStarted
    {
        GameStarted(){};
        GameStarted(players_t &_players)
            : players(_players) {}
        players_t players;
    };

    struct Turn
    {
        Turn(){};
        Turn(turn_t _turn, events_t &_events)
            : turn(_turn), events(_events) {}
        turn_t turn;
        events_t events;
    };

    struct GameEnded
    {
        GameEnded(){};
        GameEnded(scores_t &_scores)
            : scores(_scores) {}
        scores_t scores;
    };

    struct Lobby
    {
        Lobby(){};
        Lobby(const Hello &hello,
              const players_t &_players)
            : server_name(hello.server_name), players_count(hello.players_count), size_x(hello.size_x), size_y(hello.size_y), game_length(hello.game_length), explosion_radius(hello.explosion_radius), bomb_timer(hello.bomb_timer), players(_players) {}
        std::string server_name;
        players_count_t players_count;
        size_x_t size_x;
        size_y_t size_y;
        game_length_t game_length;
        explosion_radius_t explosion_radius;
        bomb_timer_t bomb_timer;
        players_t players;
    };

    struct Game
    {
        Game(){};
        Game(const Hello &hello, const turn_t _turn, const players_t &_players)
            : server_name(hello.server_name), size_x(hello.size_x), size_y(hello.size_y), game_length(hello.game_length), turn(_turn), players(_players) {}
        std::string server_name;
        size_x_t size_x;
        size_y_t size_y;
        game_length_t game_length;
        turn_t turn;
        players_t players;
        player_positions_t players_positions;
        blocks_t blocks;
        bombs_t bombs;
        explosions_t explosions;
        scores_t scores;
    };

    using input_message_t = std::variant<PlaceBomb, PlaceBlock, Move>;
    using client_message_t = std::variant<Join, PlaceBomb, PlaceBlock, Move>;
    using server_message_t = std::variant<Hello, AcceptedPlayer, GameStarted, Turn, GameEnded>;
    using draw_message_t = std::variant<Lobby, Game>;

} // namespace bomberman

#endif // BOMBERMAN_MESSAGES_H

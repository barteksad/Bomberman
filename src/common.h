#ifndef BOMBER_COMMON_H
#define BOMBER_COMMON_H

#include "types.h"
#include "messages.h"

#include <unordered_set>

namespace bomberman
{
    struct game_state_t
    {
        players_t players;
        blocks_t blocks;
        bombs_t bombs;
        player_to_position_t player_to_position;
        scores_t scores;

        void reset()
        {
            players.clear();
            blocks.clear();
            bombs.clear();
            player_to_position.clear();
            scores.clear();
        }
    };

    std::unordered_set<position_t, position_t::hash> calculate_explosion_range(const position_t bomb_position, const explosion_radius_t explosion_radius,
                                                             const size_x_t size_x, const size_y_t size_y,
                                                             const blocks_t blocks)
    {
        std::unordered_set<position_t, position_t::hash> result;

        for (auto sign : {-1, 1})
        {
            bool x_exp_blocked = false, y_exp_blocked = false;
            for (size_x_t i = 0; i <= explosion_radius; i++)
            {
                if (!x_exp_blocked)
                {
                    x_exp_blocked = true;
                    if(bomb_position.x + sign * i >= 0 && bomb_position.x + sign * i < size_x)
                    {
                        size_x_t x_i = sign > 0 ? bomb_position.x + i : bomb_position.x - i;
                        position_t current_pos{
                            .x = x_i,
                            .y = bomb_position.y,
                        };
                        result.insert(current_pos);
                        if (!blocks.contains(current_pos))
                            x_exp_blocked = false;
                    }
                }
            }
            for (size_y_t i = 0; i <= explosion_radius; i++)
            {
                if (!y_exp_blocked)
                {
                    y_exp_blocked = true;
                    if (bomb_position.y + sign * i >= 0 && bomb_position.y + sign * i < size_y)
                    {
                        size_y_t y_i = sign > 0 ? bomb_position.y + i : bomb_position.y - i;
                        position_t current_pos
                        {
                            .x = bomb_position.x,
                            .y = y_i,
                        };
                        result.insert(current_pos);
                        if (!blocks.contains(current_pos))
                            y_exp_blocked = false;
                    }
                }
            }
        }

        return result;
    }
}

#endif
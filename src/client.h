#ifndef BOMBERMAN_CLIENT_H
#define BOMBERMAN_CLIENT_H

#include <boost/asio.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <optional>
#include <queue>
#include <tuple>

#include "common.h"
#include "net.h"

namespace bomberman
{

  namespace
  {
    // This function is used to split host:port input string by ':'.
    std::tuple<std::string, std::string> split_by_colon(std::string &s)
    {
      auto split_idx = s.find_last_of(":");
      std::string s1 = s.substr(0, split_idx);
      std::string s2 = s.substr(split_idx + 1);
      return {s1, s2};
    }
  } // namespace

  struct robots_client_args_t
  {
    std::string server_endpoint_input, gui_endpoint_input, player_name;
    uint16_t port;
  };

  class RobotsClient
  {
  public:
    RobotsClient(boost::asio::io_context &io_context, robots_client_args_t &args)
        : io_context_(io_context),
          server_socket_(io_context),
          gui_socket_(io_context,
                      boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v6(),
                                                     args.port)),
          server_deserializer_(server_socket_),
          gui_deserializer_(gui_socket_),
          player_name_(args.player_name),
          state_(LOBBY)
    {
      boost::system::error_code ec;

      // Connect to GUI. First resolve given endpoint to IP address.
      boost::asio::ip::udp::resolver gui_resolver(io_context);
      auto [gui_host, gui_port] = split_by_colon(args.gui_endpoint_input);
      gui_endpoints = gui_resolver.resolve(gui_host, gui_port, ec);
      if (ec)
        throw InvalidArguments("Invalid GUI endpoint", ec);
      // Call read from gui loop.
      read_from_gui();
      // --- --- ---

      // Connect to server. First resolve given endpoint to IP address.
      boost::asio::ip::tcp::resolver server_resolver(io_context);
      auto [server_host, server_port] =
          split_by_colon(args.server_endpoint_input);
      auto server_endpoints =
          server_resolver.resolve(server_host, server_port, ec);
      if (ec)
        throw InvalidArguments("Invalid server endpoint", ec);
      // This callback will be called if connecting with server succeeds.
      auto after_server_connected_callback =
          [this](boost::system::error_code ec,
                 const boost::asio::ip::tcp::endpoint &) {
            if (!ec)
            {
              // Turn off Nagle's algorithm
              boost::asio::ip::tcp::no_delay option(true);
              server_socket_.set_option(option);
              // Create new coroutine for listening to messages.
              boost::asio::spawn(io_context_,
                                 [this](boost::asio::yield_context yield) {
                                   // Call read from server loop.
                                   read_from_server(yield);
                                 });
            }
            else
            {
              throw ConnectError("server", ec);
            }
          };
      // Async connecting to server and passing callback.
      boost::asio::async_connect(
          server_socket_, server_endpoints,
          after_server_connected_callback);
    }

    void read_from_server(boost::asio::yield_context yield)
    {
      // Infinite loop to receive from server, add messages to queue and call handle message.
      while (true)
      {
        const server_message_t msg =
            server_deserializer_.get_server_message(yield);
        server_messages_q_.push(msg);
        BOOST_LOG_TRIVIAL(debug) << "received message from server";
        handle_server_message();
      }
    }

    void read_from_gui()
    {
      // Receive from gui, add to messages queue and call handle message.
      // It is not recursive because of how boost async works.
      auto message_handle_callback = [this](const input_message_t msg) {
        input_messages_q_.push(msg);
        handle_gui_message();
        read_from_gui();
      };

      gui_deserializer_.get_message(message_handle_callback);
    }

    void handle_gui_message()
    {
      // This function should not be calld when input_messages_q_ is empty.
      assert(!input_messages_q_.empty());

      // If client_messages_q_ is not empty, it means that asynchonus sending is in progress and we only need to add new message to queue..
      // If not, then we have to call send_to_server after adding message to queue.
      // The same logic is implmented in handle_server_message
      bool send_in_progress = !client_messages_q_.empty();

      while (!input_messages_q_.empty())
      {
        if (state_ == LOBBY)
        {
          client_messages_q_.push(Join{player_name_});
          BOOST_LOG_TRIVIAL(debug) << "sending Join to server";
          // Clear qui messages queue because we are not in game yet.
          input_messages_q_ = std::queue<input_message_t>();
        }
        else if (state_ == IN_GAME)
        {
          input_message_t input_message = input_messages_q_.front();
          std::visit(
              overloaded{
                  [this](PlaceBomb &) { client_messages_q_.push(PlaceBomb{}); },
                  [this](PlaceBlock &) { client_messages_q_.push(PlaceBlock{}); },
                  [this](Move &msg) {
                    client_messages_q_.push(Move{msg.direction});
                  },
              },
              input_message);
          input_messages_q_.pop();
        }
        else if (state_ == OBSERVE)
        {
          // If client in an observer then they do not have to send anything to server.
          input_messages_q_ = std::queue<input_message_t>();
          client_messages_q_ = std::queue<client_message_t>();
        }
      }

      if (!send_in_progress && !client_messages_q_.empty())
      {
        send_to_server();
      }
    }

    // --- SERVER MESSAGES PROCESSING ---
    void process_hello(const Hello &hello)
    {
      hello_ = hello;
      Lobby lobby(hello_, game_state_.players);
      draw_messages_q_.push(lobby);
    }

    void process_accepted_player(const AcceptedPlayer &accepted_player)
    {
      // Register new player and send message to GUI.
      game_state_.players.insert(
          {accepted_player.player_id, accepted_player.player});
      Lobby lobby(hello_, game_state_.players);
      draw_messages_q_.push(lobby);
    }

    void process_game_started(const GameStarted &game_started)
    {
      // Set players and change state to IN_GAME
      game_state_.players = game_started.players;
      state_ = client_state_t::IN_GAME;
      for (auto &players_map_entry : game_state_.players)
        game_state_.scores.insert({players_map_entry.first, 0});
      Game game(hello_, 0, game_state_.players);
      game.scores = game_state_.scores;
      draw_messages_q_.push(game);
    }

    void process_bomb_placed(const BombPlaced &bomb_placed)
    {
      game_state_.bombs.insert(
          {bomb_placed.bomb_id,
           bomb_t{.position = bomb_placed.position,
                  .timer = hello_.bomb_timer}});
    }

    void process_bomb_exploded(const BombExploded &bomb_exploded, Game &game, std::unordered_set<player_id_t> &who_to_add_score, std::unordered_set<position_t, position_t::hash> &blocks_destroyed)
    {
      // Find bomb by id to get it's position.
      auto exploded_bomb_it =
          game_state_.bombs.find(bomb_exploded.bomb_id);
      if (exploded_bomb_it == game_state_.bombs.end())
      {
        // Server is always right but we do not know anythong about this bomb.
      }
      else
      {
        // Add blocks within explosion range and erase exploded bomb.
        auto explosions = calculate_explosion_range(
            exploded_bomb_it->second.position,
            hello_.explosion_radius, hello_.size_x,
            hello_.size_y, game_state_.blocks); 
        for(const auto&explosion : explosions)
          game.explosions.insert(explosion);
        game_state_.bombs.erase(exploded_bomb_it);
      }

      // Remove destroyed players.
      for (const player_id_t &robot_destroyed :
           bomb_exploded.robots_destroyed)
      {
        who_to_add_score.insert(robot_destroyed);
        game_state_.player_to_position.erase(
            robot_destroyed);
      }
      // Remove destroyed blocks.
      for (const position_t &block_destroyed :
           bomb_exploded.blocks_destroyed)
        blocks_destroyed.insert(block_destroyed);
    }

    void process_player_moved(const PlayerMoved &player_moved)
    {
      // Update player position.
      game_state_.player_to_position[player_moved.player_id] =
          player_moved.position;
    }

    void process_block_placed(const BlockPlaced &block_placed)
    {
      // Add new block.
      game_state_.blocks.insert(block_placed.position);
    }

    void process_turn(const Turn &turn)
    {
      Game game(hello_, turn.turn, game_state_.players);
      // Each player whose robot was at least once destroyed gets point.
      std::unordered_set<player_id_t> who_to_add_score;
      // Blocks destroyed are set after all events are processed to properly calculate explosion.
      std::unordered_set<position_t, position_t::hash> blocks_destroyed;
      for (const event_t &event : turn.events)
      {
        std::visit(
            overloaded{
                std::bind(&RobotsClient::process_bomb_placed, this, std::placeholders::_1),
                std::bind(&RobotsClient::process_bomb_exploded, this, std::placeholders::_1, std::ref(game), std::ref(who_to_add_score), std::ref(blocks_destroyed)),
                std::bind(&RobotsClient::process_player_moved, this, std::placeholders::_1),
                std::bind(&RobotsClient::process_block_placed, this, std::placeholders::_1),
            },
            event);
      }
      for (const auto &player : who_to_add_score)
      {
        game_state_.scores[player]++;
      }
      // Decrease each bomb timer.
      for (auto &bomb_pair : game_state_.bombs)
      {
        if (bomb_pair.second.timer > 0)
          bomb_pair.second.timer--;
      }
      // Erase destroyed blocks
      for(const auto &block_destroyed : blocks_destroyed)
        game_state_.blocks.erase(block_destroyed);
      // Set information in message to GUI .
      game.players_positions = game_state_.player_to_position;
      game.blocks = game_state_.blocks;
      game.bombs = game_state_.bombs;
      game.scores = game_state_.scores;
      draw_messages_q_.push(game);
    }

    // Maybe unused because in debug we want to assert scores but in release we do not use game_ended.
    void process_game_ended(const GameEnded &game_ended [[maybe_unused]])
    {
      assert(game_ended.scores == game_state_.scores);
      game_state_.reset();
      state_ = client_state_t::LOBBY;
      Lobby lobby(hello_, game_state_.players);
      draw_messages_q_.push(lobby);
    }
    // --- END OF SERVER MESSAGES PROCESSING ---

    void handle_server_message()
    {
      // This function should not be calld when server_messages_q_ is empty.
      assert(!server_messages_q_.empty());

      // Update game state according to received message and send updated state to GUI.
      while (!server_messages_q_.empty())
      {
        bool send_in_progress = !draw_messages_q_.empty();
        std::visit(
            overloaded{
                std::bind(&RobotsClient::process_hello, this, std::placeholders::_1),
                std::bind(&RobotsClient::process_accepted_player, this, std::placeholders::_1),
                std::bind(&RobotsClient::process_game_started, this, std::placeholders::_1),
                std::bind(&RobotsClient::process_turn, this, std::placeholders::_1),
                std::bind(&RobotsClient::process_game_ended, this, std::placeholders::_1),
            },
            server_messages_q_.front());

        if (!send_in_progress && !draw_messages_q_.empty())
        {
          send_to_gui();
        }

        server_messages_q_.pop();
      }
    }

    void send_to_server()
    {
      assert(!client_messages_q_.empty());
      NetSerializer net_serializer;
      buffer_t buffer = net_serializer.serialize(client_messages_q_.front());
      auto after_write_callback = [this](boost::system::error_code ec, std::size_t) {
        if (!ec)
        {
          // If there is still some message in client_messages_q_ then call send_to_server again.
          client_messages_q_.pop();
          if (!client_messages_q_.empty())
            send_to_server();
        }
        else
        {
          throw SendError("Server", ec);
        }
      };
      boost::asio::async_write(server_socket_,
                               boost::asio::buffer(buffer, buffer.size()),
                               after_write_callback);
    }

    void send_to_gui()
    {
      assert(!draw_messages_q_.empty());

      while (!draw_messages_q_.empty())
      {
        NetSerializer net_serializer;
        buffer_t buffer = net_serializer.serialize(draw_messages_q_.front());
        gui_socket_.send_to(boost::asio::buffer(buffer, buffer.size()),
                            *gui_endpoints);
        draw_messages_q_.pop();
      }
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
    } state_;
    boost::asio::ip::udp::resolver::iterator gui_endpoints;
    std::queue<input_message_t> input_messages_q_;
    std::queue<client_message_t> client_messages_q_;
    std::queue<server_message_t> server_messages_q_;
    std::queue<draw_message_t> draw_messages_q_;
    Hello hello_;
    game_state_t game_state_;
  };

  robots_client_args_t get_client_arguments(int ac, char *av[])
  {
    boost::program_options::options_description desc("Usage");
    desc.add_options()("h", "produce help message")(
        "-d", boost::program_options::value<std::string>(),
        "<(host):(port) or (IPv4):(port) or (IPv6):(port)>")(
        "-n", boost::program_options::value<std::string>(), "player name (max 255 characters)")(
        "-p", boost::program_options::value<uint16_t>(), "port number")(
        "-s", boost::program_options::value<std::string>(),
        "<(host):(port) or (IPv4):(port) or (IPv6):(port)>");

    try
    {
      boost::program_options::variables_map vm;
      boost::program_options::store(
          boost::program_options::parse_command_line(ac, av, desc), vm);
      boost::program_options::notify(vm);

      if (vm.count("help") ||
          !(vm.count("-d") && vm.count("-n") && vm.count("-p") && vm.count("-s")))
      {
        std::cout << desc;
        exit(1);
      }

      robots_client_args_t args;

      args.server_endpoint_input = vm["-s"].as<std::string>();
      args.gui_endpoint_input = vm["-d"].as<std::string>();
      args.player_name = vm["-n"].as<std::string>();
      args.port = vm["-p"].as<uint16_t>();

      if (args.player_name.length() > 255)
        throw InvalidArguments("player name must be shorter than 256 characters");

      BOOST_LOG_TRIVIAL(debug) << "Run with arguments, server_endpoint_input: "
                               << args.server_endpoint_input
                               << ", gui_endpoint_input: "
                               << args.gui_endpoint_input
                               << ", player_name: " << args.player_name
                               << ", port: " << args.port;

      return args;
    }
    catch (...)
    {
      std::cout << desc;
      exit(1);
    }
    __builtin_unreachable();
  }

} // namespace bomberman

#endif // BOMBERMAN_CLIENT_H

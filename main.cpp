/*
 Copyright © 2023 Oliver Lau <ola@ct.de>, Heise Medien GmbH & Co. KG - Redaktion c't

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <string>
#include <memory>
#include <sstream>
#include <list>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>

#include <boost/lexical_cast.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/regex.hpp>

#include <angelscript.h>

#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/stdx.hpp>
#include <mongocxx/client.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>

#include "global.hpp"
#include "helper.hpp"
#include "httpworker.hpp"
#include "handlers.hpp"
#include "trip/router.hpp"

#ifndef NDEBUG
const char *DEFAULT_HOST = "0.0.0.0";
#else
const char *DEFAULT_HOST = "127.0.0.1";
#endif
constexpr uint16_t DEFAULT_PORT = 31337U;

using tcp = boost::asio::ip::tcp;
namespace net = boost::asio;

void hello()
{
  std::cout << SERVER_INFO << std::endl
            << std::endl;
}

void usage()
{
  std::cout << "Usage:" << std::endl
            << "  dascript-webservice [<ip> <port> <num_workers> <num_threads>]" << std::endl
            << std::endl
            << "for example:" << std::endl
            << "  dascript-webservice 0.0.0.0 8081 2 2" << std::endl
            << std::endl
            << "or just:" << std::endl
            << "  dascript-webservice" << std::endl
            << std::endl
            << "to use the defaults: " << DEFAULT_HOST << " " << DEFAULT_PORT << " N N" << std::endl
            << "where N stands for the number of CPU cores ("
            << std::thread::hardware_concurrency() << ")." << std::endl
            << std::endl;
}

int main(int argc, const char *argv[])
{
  hello();
  if (!(argc == 1 || argc == 5))
  {
    usage();
    return EXIT_FAILURE;
  }

  net::ip::address host = net::ip::make_address(DEFAULT_HOST);
  uint16_t port = DEFAULT_PORT;
  unsigned int num_workers = std::thread::hardware_concurrency();
  unsigned int num_threads = num_workers;
  if (argc == 5)
  {
    try
    {
      host = net::ip::make_address(argv[1]);
      port = boost::lexical_cast<uint16_t>(argv[2]);
      num_workers = std::max(1U, boost::lexical_cast<unsigned int>(argv[3]));
      num_threads = std::max(1U, boost::lexical_cast<unsigned int>(argv[4]));
    }
    catch (boost::exception const &)
    {
      usage();
      return EXIT_FAILURE;
    }
  }

  mongocxx::instance instance{};
  auto client = mongocxx::client{mongocxx::uri{"mongodb://192.168.0.181:27017"}};
  mongocxx::database db = client["tasks"];
  mongocxx::collection test_task_coll = db["test"];

#ifdef INIT_MONGODB_COLLECTION
  auto builder = bsoncxx::builder::stream::document{};
  bsoncxx::document::value doc_value =
      builder
      << "name" << "Produkt"
      << "task" << "Bilde das Produkt zweier float."
      << "signature" << "float calc(float, float)"
      << "tests"
      << bsoncxx::builder::stream::open_array

      << bsoncxx::builder::stream::open_document
      << "input" << bsoncxx::builder::stream::open_array
      << 0.0
      << 0.0
      << bsoncxx::builder::stream::close_array
      << "output" << 0.0
      << bsoncxx::builder::stream::close_document

      << bsoncxx::builder::stream::open_document
      << "input" << bsoncxx::builder::stream::open_array
      << -7892.3678
      << -1243.89679
      << bsoncxx::builder::stream::close_array
      << "output" << 9817290.971919362
      << bsoncxx::builder::stream::close_document

      << bsoncxx::builder::stream::open_document
      << "input" << bsoncxx::builder::stream::open_array
      << 2.0
      << 3.0
      << bsoncxx::builder::stream::close_array
      << "output" << 6.0
      << bsoncxx::builder::stream::close_document

      << bsoncxx::builder::stream::open_document
      << "input" << bsoncxx::builder::stream::open_array
      << -11.0
      << 11.0
      << bsoncxx::builder::stream::close_array
      << "output" << -121.0
      << bsoncxx::builder::stream::close_document

      << bsoncxx::builder::stream::close_array
      << bsoncxx::builder::stream::finalize;
  bsoncxx::document::view view = doc_value.view();
  bsoncxx::stdx::optional<mongocxx::result::insert_one> result =
    test_task_coll.insert_one(view);
#endif

  boost::asio::io_context ioc;
  tcp::acceptor acceptor{ioc, {host, port}};

  std::mutex log_mtx;
  http_worker::log_callback_t logger = [&log_mtx](const std::string &msg)
  {
    std::lock_guard<std::mutex> lock(log_mtx);
    std::cout << std::chrono::system_clock::now() << ' '
              << msg << std::endl;
  };

  trip::router router;
  router
      .options(boost::regex("/execute"), handle_execution_preflight{})
      .post(boost::regex("/execute"), handle_execution{test_task_coll});

  std::list<http_worker> workers;
  for (auto i = 0U; i < num_workers; ++i)
  {
    workers.emplace_back(acceptor, router, &logger);
    workers.back().start();
  }

  std::vector<std::thread> threads;
  threads.reserve(num_threads - 1);
  for (auto i = 0U; i < num_threads - 1; ++i)
  {
    threads.emplace_back(
        [&ioc]
        {
          ioc.run();
        });
  }

  net::signal_set signals(ioc, SIGINT, SIGTERM);
  signals.async_wait(
      [&ioc](boost::system::error_code const &, int)
      {
        ioc.stop();
      });

  std::cout << (num_workers > 1 ? std::to_string(num_workers) + " workers" : " 1 worker")
            << (num_threads > 1 ? " in " + std::to_string(num_threads) + " threads" : " in 1 thread")
            << " listening on " << host << ':' << port << " ..."
            << std::endl;

  ioc.run();

  for (auto &t : threads)
  {
    t.join();
  }

  return EXIT_SUCCESS;
}

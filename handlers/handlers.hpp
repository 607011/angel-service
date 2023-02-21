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

#ifndef __HANDLERS_HPP__
#define __HANDLERS_HPP__

#include <regex>
#include "mongocxx/collection.hpp"
#include "../trip/response_request.hpp"
#include "../trip/handler.hpp"


struct handle_find_task : trip::handler
{
    mongocxx::collection &coll;
    handle_find_task(mongocxx::collection &coll);
    trip::response operator()(trip::request const &req, std::regex const &re);
};

struct handle_execution : trip::handler
{
    mongocxx::collection &coll;
    handle_execution(mongocxx::collection &coll);
    trip::response operator()(trip::request const &req, std::regex const &);
};

struct handle_execution_preflight : trip::handler
{
    trip::response operator()(trip::request const &req, std::regex const &);
};

struct handle_task_list : trip::handler
{
    mongocxx::collection &coll;
    handle_task_list(mongocxx::collection &coll);
    trip::response operator()(trip::request const &req, std::regex const &re);
};

#endif // __HANDLERS_HPP__

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

#include "handlers.hpp"

#include <string>
#include <sstream>

#include <boost/beast/http/string_body.hpp>
#include <boost/url.hpp>

#include <mongocxx/cursor.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/builder/stream/document.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace url = boost::urls;

handle_task_list::handle_task_list(mongocxx::collection &coll)
    : coll(coll)
{
}

trip::response handle_task_list::operator()(trip::request const &req, std::regex const &re)
{
    url::result<url::url_view> const &target = url::parse_origin_form(req.target());
    std::string const &path = target->path();
    std::smatch match;
    if (!std::regex_match(path, match, re))
    {
        return trip::response{http::status::no_content, ""};
    }
    std::string const status = match[1];
    bsoncxx::document::value query = bsoncxx::builder::stream::document{} << bsoncxx::builder::stream::finalize;
    if (status == "current")
    {
        query = bsoncxx::builder::stream::document{}
               << "valid.from"
               << bsoncxx::builder::stream::open_document
               << "$lte"
               << bsoncxx::types::b_date{std::chrono::system_clock::now()}
               << bsoncxx::builder::stream::close_document
               << "valid.until"
               << bsoncxx::builder::stream::open_document
               << "$gt"
               << bsoncxx::types::b_date{std::chrono::system_clock::now()}
               << bsoncxx::builder::stream::close_document
               << bsoncxx::builder::stream::finalize;
    }
    else if (status == "archived")
    {
        query = bsoncxx::builder::stream::document{}
               << "valid.until"
               << bsoncxx::builder::stream::open_document
               << "$lt"
               << bsoncxx::types::b_date{std::chrono::system_clock::now()}
               << bsoncxx::builder::stream::close_document
               << bsoncxx::builder::stream::finalize;
    }
    mongocxx::options::find opts{};
    opts.projection(bsoncxx::builder::stream::document{}
                    << "name"
                    << 1
                    << "task"
                    << 1
                    << bsoncxx::builder::stream::finalize);
    mongocxx::cursor cursor = coll.find(std::move(query), opts);
    if (cursor.begin() == cursor.end())
    {
        return trip::response{http::status::no_content, ""};
    }

    std::ostringstream os;
    os << "[";
    for (auto const &task : cursor)
    {
        os << bsoncxx::to_json(task) << ',';
    }
    os.seekp(-1, os.cur); // remove last comma
    os << "]";
    return trip::response{http::status::ok, os.str()};
}

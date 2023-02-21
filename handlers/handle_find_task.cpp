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

#include <boost/beast/http/string_body.hpp>
#include <boost/url.hpp>

#include <mongocxx/cursor.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/builder/stream/document.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace url = boost::urls;

handle_find_task::handle_find_task(mongocxx::collection &coll)
    : coll(coll) {}

trip::response handle_find_task::operator()(trip::request const &req, std::regex const &re)
{
    url::result<url::url_view> const &target = url::parse_origin_form(req.target());
    std::string const &path = target->path();
    std::smatch match;
    if (!std::regex_match(path, match, re))
    {
        return trip::response{http::status::no_content, ""};
    }
    auto query = bsoncxx::builder::stream::document{}
                 << "_id"
                 << bsoncxx::oid(match[1].str())
                 << bsoncxx::builder::stream::finalize;
    auto const result = coll.find_one(std::move(query));
    if (!result)
    {
        return trip::response{http::status::no_content, ""};
    }
    return trip::response{http::status::ok, ""};
}

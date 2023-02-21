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
#include <iostream>
#include <sstream>
#include <chrono>
#include <vector>
#include <utility>
#include <algorithm>
#include <memory>

#include <boost/asio/io_context.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/url.hpp>

#include <mongocxx/cursor.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/exception/exception.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>

namespace pt = boost::property_tree;
namespace beast = boost::beast;
namespace http = beast::http;
namespace chrono = std::chrono;
namespace url = boost::urls;

#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>
#include <scriptmath/scriptmath.h>

void PrintString(std::string const &s)
{
    std::cout << s << std::endl;
}

void PrintString_Generic(asIScriptGeneric *gen)
{
    const std::string *const str = reinterpret_cast<std::string *>(gen->GetArgAddress(0));
    std::cout << *str << std::endl;
}

#ifdef __GNUC__
void LineCallback(asIScriptContext *ctx, chrono::system_clock::time_point *timeout)
#else
void LineCallback(asIScriptContext *ctx, chrono::steady_clock::time_point *timeout)
#endif
{
    if (*timeout < chrono::high_resolution_clock::now())
    {
        ctx->Abort();
    }
}

void MessageCallback(const asSMessageInfo *msg, std::stringstream *out)
{
    *out << "[";
    switch (msg->type)
    {
    case asMSGTYPE_WARNING:
        *out << "WARN";
        break;
    case asMSGTYPE_INFORMATION:
        *out << "INFO";
        break;
    case asMSGTYPE_ERROR:
        *out << "ERROR";
        break;
    default:
        *out << "LOG";
        break;
    }
    *out << "] " << msg->section << " (" << msg->row << ", " << msg->col << ") " << msg->message << std::endl;
}

bool approximately_equal(float a, float b, float epsilon = 0.000001)
{
    return fabs(a - b) <= ((fabs(a) < fabs(b) ? fabs(b) : fabs(a)) * epsilon);
}

bool execute_script(std::string const &script, mongocxx::collection &coll, bsoncxx::oid const &oid, std::string &err_msg, std::stringstream &err_log)
{
    auto query = bsoncxx::builder::stream::document{}
                 << "_id"
                 << oid
                 << bsoncxx::builder::stream::finalize;
    auto result = coll.find_one(std::move(query));
    if (!result)
    {
        err_log << "OID »" << oid.to_string() << "« not found in database." << std::endl;
        return false;
    }
#ifndef NDEBUG
    err_log << "[DEBUG]" << bsoncxx::to_json(*result) << std::endl;
#endif

    if (!result->view()["tests"])
    {
        err_log << "Field \"tests\" not found in database." << std::endl;
        return false;
    }
    if (result->view()["tests"].type() != bsoncxx::type::k_array)
    {
        err_log << "Field \"tests\" is not an array." << std::endl;
        return false;
    }
    auto tests = result->view()["tests"].get_array().value;

    if (!result->view()["signature"])
    {
        err_log << "Field \"signature\" missing in task." << std::endl;
        return false;
    }
    if (result->view()["signature"].type() != bsoncxx::type::k_string)
    {
        err_log << "Field \"signature\" is not a string." << std::endl;
        return false;
    }
    auto signature = result->view()["signature"].get_string().value;

    int rc;
    asIScriptEngine *engine = asCreateScriptEngine();
    if (engine == nullptr)
    {
        std::cerr << "Failed to create script engine." << std::endl;
        return false;
    }
    rc = engine->SetMessageCallback(asFUNCTION(MessageCallback), &err_log, asCALL_CDECL);

    RegisterStdString(engine);
    RegisterScriptMath_Native(engine);

    asIScriptModule *mod = engine->GetModule(0, asGM_ALWAYS_CREATE);
    rc = mod->AddScriptSection("script", script.c_str(), script.size());
    if (rc < 0)
    {
        err_log << "AddScriptSection() failed." << std::endl;
        engine->Release();
        return false;
    }
    rc = mod->Build();
    if (rc < 0)
    {
        err_log << "Build failed." << std::endl;
        engine->Release();
        return false;
    }
    asIScriptContext *ctx = engine->CreateContext();
    if (ctx == nullptr)
    {
        err_log << "Failed to create the context." << std::endl;
        engine->Release();
        return false;
    }
    asIScriptFunction *func = engine->GetModule(0)->GetFunctionByDecl(signature.to_string().c_str());
    if (func == nullptr)
    {
        err_log << "The function `" << signature.to_string() << "` could not be found." << std::endl;
        ctx->Release();
        engine->ShutDownAndRelease();
        return false;
    }
    bool correct = true;
    for (auto const &test : tests)
    {
        rc = ctx->Prepare(func);
        if (rc < 0)
        {
            err_log << "Failed to prepare the context." << std::endl;
            ctx->Release();
            engine->ShutDownAndRelease();
            return false;
        }
        if (!test["input"])
        {
            err_log << "Field \"input\" missing in task." << std::endl;
            ctx->Release();
            engine->ShutDownAndRelease();
            return false;
        }
        if (test["input"].type() != bsoncxx::type::k_array)
        {
            err_log << "Field \"input\" is not an array." << std::endl;
            ctx->Release();
            engine->ShutDownAndRelease();
            return false;
        }
        auto input = test["input"].get_array().value;
        if (!test["output"])
        {
            err_log << "Field \"output\" missing in task." << std::endl;
            ctx->Release();
            engine->ShutDownAndRelease();
            return false;
        }
        if (test["output"].type() != bsoncxx::type::k_double)
        {
            err_log << "Field \"output\" is not a double." << std::endl;
            ctx->Release();
            engine->ShutDownAndRelease();
            return false;
        }
        auto output = test["output"].get_double().value;
        asUINT arg_idx = 0U;
        for (auto i = input.cbegin(); i != input.cend(); ++i)
        {
            if (i->type() != bsoncxx::type::k_double)
            {
                err_log << "Field \"output\" does not contain double values." << std::endl;
                ctx->Release();
                engine->ShutDownAndRelease();
                return false;
            }
            ctx->SetArgFloat(arg_idx++, static_cast<float>(i->get_double().value));
        }
        auto timeout = chrono::high_resolution_clock::now() + chrono::seconds(5);
        rc = ctx->SetLineCallback(asFUNCTION(LineCallback), &timeout, asCALL_CDECL);
        if (rc < 0)
        {
            err_log << "Failed to set the line callback function." << std::endl;
            ctx->Release();
            engine->ShutDownAndRelease();
            return false;
        }
        rc = ctx->Execute();
        if (rc == asEXECUTION_FINISHED)
        {
            auto return_value = ctx->GetReturnFloat();
            correct &= approximately_equal(output, return_value);
        }
        else if (rc == asEXECUTION_ABORTED)
        {
            err_log << "The script was aborted before it could finish. Probably it timed out." << std::endl;
            correct = false;
            break;
        }
        else if (rc == asEXECUTION_EXCEPTION)
        {
            err_log << "The script ended with an exception." << std::endl;
            asIScriptFunction *func = ctx->GetExceptionFunction();
            err_log << "func: " << func->GetDeclaration() << std::endl;
            err_log << "modl: " << func->GetModuleName() << std::endl;
            err_log << "sect: " << func->GetScriptSectionName() << std::endl;
            err_log << "line: " << ctx->GetExceptionLineNumber() << std::endl;
            err_log << "desc: " << ctx->GetExceptionString() << std::endl;
            correct = false;
            break;
        }
        else
        {
            err_log << "The script ended for some unforeseen reason (result code = " << rc << ")." << std::endl;
            correct = false;
            break;
        }
    }

    ctx->Release();
    engine->ShutDownAndRelease();

    if (!correct)
    {
        err_msg = "Your script failed in at least one test. Try again.";
    }
    return correct;
}


handle_execution::handle_execution(mongocxx::collection &coll)
    : coll(coll)
{
}
trip::response handle_execution::operator()(trip::request const &req, std::regex const &)
{
    pt::ptree request;
    std::stringstream iss;
    iss << req.body();
    try
    {
        pt::read_json(iss, request);
    }
    catch (pt::ptree_error const &e)
    {
        return trip::response{http::status::bad_request, "{\"error\": \"" + std::string(e.what()) + "\""};
    }
    if (request.find("script") == request.not_found())
    {
        return trip::response{http::status::bad_request, "{\"error\": \"field \\\"script\\\" is missing\"}"};
    }
    if (request.find("task_id") == request.not_found())
    {
        return trip::response{http::status::bad_request, "{\"error\": \"field \\\"task_id\\\" is missing\"}"};
    }
    try
    {
        bsoncxx::oid oid(request.get<std::string>("task_id"));
    }
    catch(bsoncxx::exception const& e)
    {
        return trip::response{http::status::bad_request, e.what(), "text/plain"};
    }
    bsoncxx::oid oid(request.get<std::string>("task_id"));
    auto t0 = chrono::high_resolution_clock::now();
    std::stringstream err_log;
    std::string err_msg;
    auto script = request.get<std::string>("script");
    bool correct = execute_script(script, coll, oid, err_msg, err_log);
    auto t1 = chrono::high_resolution_clock::now();
    auto dt = chrono::duration_cast<chrono::duration<double>>(t1 - t0);
    pt::ptree response;
    response.put("error", err_msg);
    response.put("messages", err_log.str());
    response.put("elapsed_msecs", "[elapsed_msecs]");
    response.put("correct", "[correct]");
    std::ostringstream ss;
    pt::write_json(ss, response, true);
    std::string responseStr = ss.str();
    boost::replace_all(responseStr, "\"[elapsed_msecs]\"", std::to_string(1e3 * dt.count()));
    boost::replace_all(responseStr, "\"[correct]\"", correct ? "true" : "false");
    return trip::response{http::status::ok, responseStr};
}

trip::response handle_execution_preflight::operator()(trip::request const &req, std::regex const &)
{
    return trip::response{http::status::ok, ""};
}

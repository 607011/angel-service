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
#pragma once
#ifndef __HANDLERS_HPP__
#define __HANDLERS_HPP__

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
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

#include <angelscript.h>

#include "helper.hpp"
#include "trip/router.hpp"
#include "trip/response_request.hpp"
#include "trip/handler.hpp"

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

#ifdef __linux__
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
    switch (msg->type)
    {
    case asMSGTYPE_WARNING:
        *out << "WARN";
        break;
    case asMSGTYPE_INFORMATION:
        *out << "INFO";
        break;
    default:
        *out << "ERROR";
        break;
    }
    *out << " in " << msg->section << " (" << msg->row << ", " << msg->col << ") " << msg->message << std::endl;
}

bool execute_script(std::string const &script, std::stringstream &err)
{
    int rc;
    asIScriptEngine *engine = asCreateScriptEngine();
    if (engine == nullptr)
    {
        err << "Failed to create script engine." << std::endl;
        return false;
    }
    rc = engine->SetMessageCallback(asFUNCTION(MessageCallback), &err, asCALL_CDECL);
    RegisterStdString(engine);
    RegisterScriptMath_Native(engine);

    asIScriptModule *mod = engine->GetModule(0, asGM_ALWAYS_CREATE);
    rc = mod->AddScriptSection("script", script.c_str(), script.size());
    if (rc < 0)
    {
        err << "AddScriptSection() failed" << std::endl;
        engine->Release();
        return false;
    }
    rc = mod->Build();
    if (rc < 0)
    {
        err << "Build() failed." << std::endl;
        engine->Release();
        return false;
    }
    asIScriptContext *ctx = engine->CreateContext();
    if (ctx == nullptr)
    {
        err << "Failed to create the context." << std::endl;
        engine->Release();
        return false;
    }
    asIScriptFunction *func = engine->GetModule(0)->GetFunctionByDecl("float calc(float, float)");
    if (func == nullptr)
    {
        err << "The function 'float calc(float, float)' was not found." << std::endl;
        ctx->Release();
        engine->Release();
        return false;
    }
    rc = ctx->Prepare(func);
    if (rc < 0)
    {
        err << "Failed to prepare the context." << std::endl;
        ctx->Release();
        engine->Release();
        return false;
    }
    ctx->SetArgFloat(0, 3.14159265359f);
    ctx->SetArgFloat(1, 2.71828182845904523536028747135266249775724709369995f);
    auto timeout = chrono::high_resolution_clock::now() + chrono::seconds(5);
    rc = ctx->SetLineCallback(asFUNCTION(LineCallback), &timeout, asCALL_CDECL);
    if (rc < 0)
    {
        err << "Failed to set the line callback function." << std::endl;
        ctx->Release();
        engine->Release();
        return false;
    }
    rc = ctx->Execute();
    if (rc != asEXECUTION_FINISHED)
    {
        if (rc == asEXECUTION_ABORTED)
        {
            err << "The script was aborted before it could finish. Probably it timed out." << std::endl;
        }
        else if (rc == asEXECUTION_EXCEPTION)
        {
            err << "The script ended with an exception." << std::endl;
            asIScriptFunction *func = ctx->GetExceptionFunction();
            err << "func: " << func->GetDeclaration() << std::endl;
            err << "modl: " << func->GetModuleName() << std::endl;
            err << "sect: " << func->GetScriptSectionName() << std::endl;
            err << "line: " << ctx->GetExceptionLineNumber() << std::endl;
            err << "desc: " << ctx->GetExceptionString() << std::endl;
        }
        else
        {
            err << "The script ended for some unforeseen reason (" << rc << ")." << std::endl;
        }
    }
    else
    {
        auto returnValue = ctx->GetReturnFloat();
        err << "DEBUG: The script function returned: " << returnValue << std::endl;
    }

    engine->ClearMessageCallback();
    ctx->Release();
    engine->ShutDownAndRelease();
    return true;
}

struct handle_execution : trip::handler
{
    trip::response operator()(trip::request const &req, boost::smatch const &)
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
        auto t0 = chrono::high_resolution_clock::now();
        std::stringstream err;
        auto script = request.get<std::string>("script");
        bool correct = execute_script(script, err);
        auto t1 = chrono::high_resolution_clock::now();
        auto dt = chrono::duration_cast<chrono::duration<double>>(t1 - t0);
        pt::ptree response;
        response.put("errors", err.str());
        response.put("elapsed_msecs", "[elapsed_msecs]");
        response.put("correct", "[correct]");
        std::ostringstream ss;
        pt::write_json(ss, response, true);
        std::string responseStr = ss.str();
        boost::replace_all(responseStr, "\"[elapsed_msecs]\"", std::to_string(1e3 * dt.count()));
        boost::replace_all(responseStr, "\"[correct]\"", correct ? "true" : "false");
        return trip::response{http::status::ok, responseStr};
    }
};

#endif // __HANDLERS_HPP__

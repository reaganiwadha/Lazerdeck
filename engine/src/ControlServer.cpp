#include "ControlServer.hpp"
#include "Control.hpp"
#include "Engine.hpp"
#include "Logger.hpp"

#include <httplib.h>
#include <glaze/glaze.hpp>

ControlServer::ControlServer(Engine* engine, int defaultPort)
    : engine(engine), port_(defaultPort) {}

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(int port) {
    std::lock_guard<std::mutex> lk(mtx);

    // Tear down any running instance first (stop() without re-locking).
    if (server) server->stop();
    if (listenThread.joinable()) listenThread.join();
    running.store(false);

    port_.store(port);

    // Fresh server each start; register the routes.
    server = std::make_unique<httplib::Server>();

    // POST /action — body is a ControlRequest JSON object. Replies with a
    // ControlResponse: {"ok":true} or 400 {"ok":false,"error":"…"}. Execution is
    // async (queued onto the engine thread); only parse/validation is synchronous.
    server->Post("/action", [this](const httplib::Request& req, httplib::Response& res) {
        ControlResponse resp;

        ControlRequest parsed;
        if (auto ec = glz::read_json(parsed, req.body)) {
            resp.ok = false;
            resp.error = "json parse error: " + glz::format_error(ec, req.body);
            res.status = 400;
            res.set_content(glz::write_json(resp).value_or("{\"ok\":false}"), "application/json");
            return;
        }

        if (parsed.actions.empty()) {
            resp.ok = false;
            resp.error = "no actions";
            res.status = 400;
            res.set_content(glz::write_json(resp).value_or("{\"ok\":false}"), "application/json");
            return;
        }

        engine->submitActions(parsed);
        res.set_content(glz::write_json(resp).value_or("{\"ok\":true}"), "application/json");
    });

    // Liveness probe for clients.
    server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"ok\":true}", "application/json");
    });

    // Attempt the bind synchronously so the caller learns the outcome now. A busy
    // port returns false rather than throwing — non-fatal by design.
    if (!server->bind_to_port("127.0.0.1", port)) {
        Logger::error("ControlServer: port " + std::to_string(port) + " unavailable");
        server.reset();
        return false;
    }

    running.store(true);
    listenThread = std::thread([this, port]() {
        Logger::info("ControlServer listening on 127.0.0.1:" + std::to_string(port));
        server->listen_after_bind();  // blocks until stop()
        running.store(false);
    });
    // Block until the accept loop is actually live. Without this, a stop() issued
    // immediately after start() can race ahead of the loop and be a no-op, leaving
    // listen_after_bind() (and the join in stop()) hung forever.
    server->wait_until_ready();
    return true;
}

void ControlServer::stop() {
    std::lock_guard<std::mutex> lk(mtx);
    if (server) server->stop();
    if (listenThread.joinable()) listenThread.join();
    running.store(false);
    server.reset();
}

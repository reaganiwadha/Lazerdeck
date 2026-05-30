#pragma once

#include <thread>
#include <memory>
#include <atomic>
#include <mutex>

namespace httplib { class Server; }
class Engine;

// HTTP/JSON control plane. Owns an httplib::Server on its own thread, listening
// on localhost. `POST /action` carries a ControlRequest JSON body (see
// Control.hpp), parsed with Glaze and handed to Engine::submitActions(). The HTTP
// thread only parses + validates; all deck mutation is marshalled onto the engine
// thread by submitActions().
//
// start()/stop() are idempotent and safe to call from the host (FFI) thread; the
// bind is attempted synchronously so callers learn immediately whether the port
// was free. A failed bind is non-fatal — the server just stays stopped and the
// host can retry on another port.
class ControlServer {
public:
    ControlServer(Engine* engine, int defaultPort = 8203);
    ~ControlServer();

    // Binds `port` on 127.0.0.1 and starts serving. Stops any running instance
    // first. Returns true iff the bind succeeded; on failure the server stays
    // stopped (no throw, no crash). `port` is remembered either way.
    bool start(int port);
    void stop();

    bool isRunning() const { return running.load(); }
    int  port() const { return port_.load(); } // last requested/bound port

private:
    Engine* engine;
    std::atomic<int>  port_;
    std::atomic<bool> running{false};
    std::unique_ptr<httplib::Server> server;
    std::thread listenThread;
    std::mutex mtx; // serializes start/stop against each other
};

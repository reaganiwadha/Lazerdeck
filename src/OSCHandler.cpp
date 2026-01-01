#include "OSCHandler.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include <iostream>

OSCHandler::OSCHandler(Engine* engine, int port) 
    : engine(engine), port(port), socket(nullptr), listenThread(nullptr), running(false) {}

OSCHandler::~OSCHandler() {
    stop();
}

void OSCHandler::start() {
    if (running) return;
    running = true;
    listenThread = new std::thread([this]() {
        try {
            socket = new UdpListeningReceiveSocket(
                IpEndpointName(IpEndpointName::ANY_ADDRESS, port),
                this
            );
            Logger::info("OSC Server listening on port " + std::to_string(port));
            socket->RunUntilSigInt();
        } catch (std::exception& e) {
            Logger::error("OSC Server error: " + std::string(e.what()));
        }
    });
}

void OSCHandler::stop() {
    if (!running) return;
    running = false;
    if (socket) {
        socket->AsynchronousBreak();
        // Give it a moment to break
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        delete socket;
        socket = nullptr;
    }
    if (listenThread) {
        if (listenThread->joinable()) listenThread->join();
        delete listenThread;
        listenThread = nullptr;
    }
}

void OSCHandler::ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint) {
    (void)remoteEndpoint;
    try {
        std::string address = m.AddressPattern();

        // Expected format: /deck/<index>/<command>
        if (address.find("/deck/") == 0) {
            size_t secondSlash = address.find('/', 6);
            if (secondSlash != std::string::npos) {
                int deckIdx = std::stoi(address.substr(6, secondSlash - 6));
                std::string cmd = address.substr(secondSlash + 1);

                engine->handleOSCCommand(deckIdx, cmd, m);
            }
        }
        else if (address.find("/system/") == 0) {
            size_t secondSlash = address.find('/', 8);
            if (secondSlash != std::string::npos) {
                std::string cmd = address.substr(secondSlash + 1);
                engine->handleSystemCommand(cmd, m);
            } else {
                std::string cmd = address.substr(8);
                engine->handleSystemCommand(cmd, m);
            }
        }
    } catch (std::exception& e) {
        Logger::error("Error processing OSC message: " + std::string(e.what()));
    }
}

#pragma once

#include <osc/OscPacketListener.h>
#include <ip/UdpSocket.h>
#include <thread>
#include <vector>
#include <string>

class Engine;

class OSCHandler : public osc::OscPacketListener {
public:
    OSCHandler(Engine* engine, int port = 9000);
    ~OSCHandler();

    void start();
    void stop();

protected:
    void ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint) override;

private:
    Engine* engine;
    int port;
    UdpListeningReceiveSocket* socket;
    std::thread* listenThread;
    bool running;
};

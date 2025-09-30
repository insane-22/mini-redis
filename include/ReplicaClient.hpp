#pragma once
#include <string>

class ReplicaClient {
public:
    ReplicaClient(const std::string& host, int port, int replicaPort);
    ~ReplicaClient();

    void startReplicationLoop();
    void connectToMaster();
    void startHandshake();

private:
    std::string master_host;
    int master_port;
    int replica_port;
    int sock_fd;

    std::string recv_buffer;
    void sendAll(const std::string& data);
    std::string readLine();
};

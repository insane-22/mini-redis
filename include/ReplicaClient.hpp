#pragma once
#include <string>

class ReplicaClient {
public:
    ReplicaClient(const std::string& host, int port, int replicaPort);
    ~ReplicaClient();

    // Connect to master (resolves hostnames)
    void connectToMaster();

    // Perform the handshake: PING -> REPLCONF(s) -> PSYNC
    // This function blocks while performing the handshake, then returns.
    void startHandshake();

private:
    std::string master_host;
    int master_port;
    int replica_port;
    int sock_fd;

    // Buffer to hold leftover bytes read from socket between readLine() calls
    std::string recv_buffer;

    // low-level send that ensures all bytes are written
    void sendAll(const std::string& data);

    // read a single CRLF-terminated line (returns the line WITHOUT "\r\n").
    // returns empty string on EOF/error.
    std::string readLine();
};

#include "ReplicaClient.hpp"
#include "Handler.hpp"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <errno.h>

ReplicaClient::ReplicaClient(const std::string& host, int port, int replicaPort)
    : master_host(host), master_port(port), replica_port(replicaPort), sock_fd(-1) {}

ReplicaClient::~ReplicaClient() {
    if (sock_fd != -1) {
        close(sock_fd);
        sock_fd = -1;
    }
}

void ReplicaClient::startReplicationLoop() {
    char buffer[4096];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(sock_fd, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) {
            std::cerr << "Master closed replication connection\n";
            break;
        }

        std::string msg(buffer, n);
        Handler handler(sock_fd, true, nullptr);
        handler.handleMessage(msg);
    }
}

void ReplicaClient::connectToMaster() {
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;     
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(master_host.c_str(), std::to_string(master_port).c_str(), &hints, &res);
    if (status != 0) {
        std::cerr << "getaddrinfo failed: " << gai_strerror(status) << "\n";
        close(sock_fd);
        exit(1);
    }

    if (connect(sock_fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("Connection failed");
        freeaddrinfo(res);
        close(sock_fd);
        exit(1);
    }

    freeaddrinfo(res);
    std::cout << "Replica: connected to master " << master_host << ":" << master_port << "\n";
}

void ReplicaClient::sendAll(const std::string& data) {
    size_t sent = 0;
    const char* ptr = data.c_str();
    size_t total = data.size();

    while (sent < total) {
        ssize_t n = send(sock_fd, ptr + sent, total - sent, 0);
        if (n < 0) {
            perror("send failed");
            close(sock_fd);
            sock_fd = -1;
            exit(1);
        }
        sent += static_cast<size_t>(n);
    }
}

std::string ReplicaClient::readLine() {
    size_t pos;
    while ((pos = recv_buffer.find("\r\n")) == std::string::npos) {
        char tmp[512];
        ssize_t n = recv(sock_fd, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            if (n == 0) {
                std::cerr << "Replica: connection closed by master\n";
            } else {
                perror("recv failed");
            }
            return std::string();
        }
        recv_buffer.append(tmp, static_cast<size_t>(n));
    }

    std::string line = recv_buffer.substr(0, pos);
    recv_buffer.erase(0, pos + 2);
    return line;
}

void ReplicaClient::startHandshake() {
    const std::string ping = "*1\r\n$4\r\nPING\r\n";
    sendAll(ping);
    std::string resp = readLine();
    if (resp.empty()) {
        std::cerr << "Replica: no response to PING\n";
        return;
    }
    if (resp.rfind("+PONG", 0) != 0 && resp.rfind("+PONG", 0) != 0 && resp != "+PONG") {
        std::cerr << "Replica: unexpected PING response: '" << resp << "'\n";
    } else {
        std::cout << "Replica: got PONG\n";
    }

    std::string portStr = std::to_string(replica_port);
    std::string replconf1;
    replconf1 += "*3\r\n";
    replconf1 += "$8\r\nREPLCONF\r\n";
    replconf1 += "$14\r\nlistening-port\r\n";
    replconf1 += "$" + std::to_string(portStr.size()) + "\r\n";
    replconf1 += portStr + "\r\n";

    sendAll(replconf1);
    resp = readLine();
    if (resp.empty()) {
        std::cerr << "Replica: no response to REPLCONF listening-port\n";
        return;
    }
    if (resp.rfind("+OK", 0) != 0 && resp != "+OK") {
        std::cerr << "Replica: unexpected REPLCONF (listening-port) response: '" << resp << "'\n";
    } else {
        std::cout << "Replica: REPLCONF listening-port -> OK\n";
    }

    const std::string replconf2 = "*3\r\n$8\r\nREPLCONF\r\n$4\r\ncapa\r\n$6\r\npsync2\r\n";
    sendAll(replconf2);
    resp = readLine();
    if (resp.empty()) {
        std::cerr << "Replica: no response to REPLCONF capa psync2\n";
        return;
    }
    if (resp.rfind("+OK", 0) != 0 && resp != "+OK") {
        std::cerr << "Replica: unexpected REPLCONF (capa) response: '" << resp << "'\n";
    } else {
        std::cout << "Replica: REPLCONF capa psync2 -> OK\n";
    }

    const std::string psync = "*3\r\n$5\r\nPSYNC\r\n$1\r\n?\r\n$2\r\n-1\r\n";
    sendAll(psync);
    resp = readLine();
    if (resp.empty()) {
        std::cerr << "Replica: no response to PSYNC\n";
        return;
    }
    if (resp.rfind("+FULLRESYNC", 0) == 0) {
        std::cout << "Replica: got FULLRESYNC reply: " << resp << "\n";
    } else {
        std::cout << "Replica: PSYNC response: " << resp << "\n";
    }
}

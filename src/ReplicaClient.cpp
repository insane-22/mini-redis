#include "ReplicaClient.hpp"
#include "Handler.hpp"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <errno.h>
#include <string>
#include <cstdlib>

ReplicaClient::ReplicaClient(const std::string& host, int port, int replicaPort)
    : master_host(host), master_port(port), replica_port(replicaPort), sock_fd(-1) {}

ReplicaClient::~ReplicaClient() {
    if (sock_fd != -1) {
        close(sock_fd);
        sock_fd = -1;
    }
}

static bool extractNextRespMessage(const std::string &buf, size_t &endIndex) {
    if (buf.empty()) return false;
    size_t pos = 0;
    char t = buf[pos];

    if (t == '+' || t == '-' || t == ':') {
        size_t crlf = buf.find("\r\n", pos);
        if (crlf == std::string::npos) return false;
        endIndex = crlf + 2;
        return true;
    }

    if (t == '$') {
        size_t crlf = buf.find("\r\n", pos);
        if (crlf == std::string::npos) return false;
        std::string len_str = buf.substr(1, crlf - 1);
        long len;
        try {
            len = std::stol(len_str);
        } catch (...) {
            return false;
        }
        if (len < 0) {
            endIndex = crlf + 2;
            return true;
        }
        size_t needed = crlf + 2 + static_cast<size_t>(len) + 2;
        if (buf.size() < needed) return false;
        endIndex = needed;
        return true;
    }

    if (t == '*') {
        size_t crlf = buf.find("\r\n", pos);
        if (crlf == std::string::npos) return false;
        std::string elements_str = buf.substr(1, crlf - 1);
        int elements;
        try {
            elements = std::stoi(elements_str);
        } catch (...) {
            return false;
        }
        size_t idx = crlf + 2;
        for (int i = 0; i < elements; ++i) {
            if (idx >= buf.size()) return false;
            char ct = buf[idx];
            if (ct == '$') {
                size_t crlf2 = buf.find("\r\n", idx);
                if (crlf2 == std::string::npos) return false;
                std::string len_str = buf.substr(idx + 1, crlf2 - (idx + 1));
                long len;
                try {
                    len = std::stol(len_str);
                } catch (...) {
                    return false;
                }
                if (len < 0) {
                    idx = crlf2 + 2;
                } else {
                    size_t need = crlf2 + 2 + static_cast<size_t>(len) + 2;
                    if (buf.size() < need) return false;
                    idx = need;
                }
            } else if (ct == '+' || ct == '-' || ct == ':') {
                size_t crlf2 = buf.find("\r\n", idx);
                if (crlf2 == std::string::npos) return false;
                idx = crlf2 + 2;
            } else if (ct == '*') {
                return false;
            } else {
                return false; 
            }
        }
        endIndex = idx;
        return true;
    }
    return false; 
}

void ReplicaClient::startReplicationLoop() {
    if (sock_fd == -1) {
        std::cerr << "Replica: socket not connected\n";
        return;
    }

    Handler handler(sock_fd, true, nullptr, "./", "dump.rdb"); 

    std::string recv_buffer;
    char buffer[4096];
    while (true) {
        ssize_t n = recv(sock_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            std::cerr << "Master closed replication connection\n";
            break;
        }

        recv_buffer.append(buffer, static_cast<size_t>(n));
        size_t msg_end = 0;
        while (extractNextRespMessage(recv_buffer, msg_end)) {
            std::string msg = recv_buffer.substr(0, msg_end);
            handler.handleMessage(msg); 
            recv_buffer.erase(0, msg_end);
        }
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

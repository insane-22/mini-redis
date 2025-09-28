#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <vector>
#include "Handler.hpp"

void handleResponse(int client_fd, bool isReplica) {
  Handler handler(client_fd, isReplica);

  char buffer[1024];
  while(true){
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(client_fd, buffer, sizeof(buffer)-1, 0);
    if(bytes_received<=0){
      std::cerr << "Client disconnected or error occurred\n";
      break;
    }

    std::string message(buffer, bytes_received);
    handler.handleMessage(message);
  }
  close(client_fd);
}

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  int port = 6379;
  bool isReplica = false;
  std::string masterHost;
  int masterPort = 0;
  for(int i=1;i<argc;i++){
    std::string arg=argv[i];
    if (arg=="--port" && i + 1 < argc) {
      port = std::stoi(argv[i + 1]);
      i++;
    } else if (arg=="--replicaof" && i+1<argc) {
      isReplica = true;
      std::string hostPort = argv[++i];
      size_t pos = hostPort.find(' ');
      if (pos != std::string::npos) {
        masterHost = hostPort.substr(0, pos);
        masterPort = std::stoi(hostPort.substr(pos+1));
      } else if (i+1 < argc) {
        masterHost = hostPort;
        masterPort = std::stoi(argv[++i]);
      } else {
        std::cerr << "--replicaof requires host and port\n";
        return 1;
      }
    }
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port "<<port<<"\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  std::cout << "Server listening on port " << port << "\n";
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  while(true){
    std::cout << "Waiting for a client to connect...\n";
    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    std::cout << "Client connected\n";

    std::thread clients(handleResponse, client_fd, isReplica);
    clients.detach();
  }

  close(server_fd);

  return 0;
}

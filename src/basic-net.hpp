#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <iostream>

int listen_and_accept_client_connection(uint16_t port) {

    // Create a TCP socket
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::perror("socket");
        return -1;
    }

    // Allow quick reuse of the address after the program exits
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::perror("setsockopt");
        ::close(server_fd);
        return -1;
    }

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(server_fd);
        return -1;
    }

    // Listen for incoming connections (backlog = 1 is enough for this demo)
    if (listen(server_fd, 1) < 0) {
        std::perror("listen");
        ::close(server_fd);
        return -1;
    }

    std::cout << "Server listening on port " << port << " ...\n";

    // Accept a single client
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
        std::perror("accept");
        ::close(server_fd);
        return -1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    std::cout << "Client connected from " << client_ip << ":"
              << ntohs(client_addr.sin_port) << "\n";

    return client_fd;
}


#ifndef JSON_SOCKET_SERVER_H
#define JSON_SOCKET_SERVER_H

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using sock_t = SOCKET;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
using sock_t = int;
#define INVALID_SOCK -1
#endif

class JsonSocketServer {
public:
    explicit JsonSocketServer(uint16_t port = 5555)
        : port_(port), running_(true), listenSock_(INVALID_SOCK), clientSock_(INVALID_SOCK), clientConnected_(false) {
        thread_ = std::thread(&JsonSocketServer::serverThreadFunc, this);
    }

    ~JsonSocketServer() {
        running_ = false;
        closeSocket(listenSock_);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            closeSocket(clientSock_);
        }
        if (thread_.joinable()) thread_.join();
    }

    bool hasConnection() const {
        return clientConnected_.load();
    }

    // Blocks the caller until a client connects or timeout occurs
    bool waitForConnection(int timeoutMs = 30000) {
        std::unique_lock<std::mutex> lk(mtx_);
        return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] { return clientConnected_.load(); });
    }

    void sendLine(const std::string& line) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (clientSock_ == INVALID_SOCK) return;

        std::string out = line + "\n";
        const char* data = out.data();
        size_t remaining = out.size();

        while (remaining > 0) {
            int sent = ::send(clientSock_, data, (int)remaining, 0);
            if (sent <= 0) {
                closeSocket(clientSock_);
                clientSock_ = INVALID_SOCK;
                clientConnected_ = false;
                return;
            }
            data += sent;
            remaining -= sent;
        }
    }

private:
    void serverThreadFunc() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        listenSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int opt = 1;
        setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port_);

        if (bind(listenSock_, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(listenSock_, 1) != 0) {
            std::cerr << "[JsonSocketServer] Setup failed\n";
            return;
        }

        while (running_) {
            sockaddr_in cAddr;
            socklen_t cLen = sizeof(cAddr);
            sock_t client = accept(listenSock_, (sockaddr*)&cAddr, &cLen);

            if (!running_) { closeSocket(client); break; }
            if (client == INVALID_SOCK) continue;

            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (clientSock_ != INVALID_SOCK) closeSocket(clientSock_);
                clientSock_ = client;
                clientConnected_ = true;
            }
            cv_.notify_all(); // Wake up the main thread!

            // Monitor connection
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::lock_guard<std::mutex> lk(mtx_);
                if (clientSock_ == INVALID_SOCK) break;
            }
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void closeSocket(sock_t s) {
        if (s != INVALID_SOCK) {
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
        }
    }

    uint16_t port_;
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
    std::atomic<bool> clientConnected_;
    sock_t listenSock_;
    sock_t clientSock_;
};

#endif
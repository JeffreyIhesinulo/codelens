#pragma once
// A local protocol fixture: checks UI integration without downloading a language model.
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <nlohmann/json.hpp>

class FakeTutor {
    int fd = -1;
    std::atomic<bool> stopping{false};
    std::atomic<bool> installed{false};
    std::thread worker;

  public:
    int port = 0;
    static std::string exercise() {
        return nlohmann::json({{"title", "One more"},
                               {"text", "Read a whole number and print the next number."},
                               {"starter", "#include <stdio.h>\nint main(void) { int x; scanf(\"%d\", &x); "
                                           "printf(\"%d\\n\", x); }\n"},
                               {"solution", "#include <stdio.h>\nint main(void) { int x; scanf(\"%d\", &x); "
                                            "printf(\"%d\\n\", x + 1); }\n"},
                               {"tests", nlohmann::json::array({{{"input", "1\n"}, {"expected", "2\n"}},
                                                                {{"input", "3\n"}, {"expected", "4\n"}}})}})
            .dump();
    }
    FakeTutor() {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd >= 0);
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(fd, (sockaddr *)&address, sizeof address) == 0);
        socklen_t size = sizeof address;
        getsockname(fd, (sockaddr *)&address, &size);
        port = ntohs(address.sin_port);
        listen(fd, 8);
        worker = std::thread([this] {
            while (!stopping) {
                fd_set descriptors;
                FD_ZERO(&descriptors);
                FD_SET(fd, &descriptors);
                timeval timeout{0, 100000};
                if (select(fd + 1, &descriptors, nullptr, nullptr, &timeout) <= 0)
                    continue;
                int client = accept(fd, nullptr, nullptr);
                if (client < 0)
                    continue;
                timeval receive_timeout{3, 0};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof receive_timeout);
                std::string request;
                char buffer[4096];
                size_t end = std::string::npos, length = 0;
                for (ssize_t n; (n = recv(client, buffer, sizeof buffer, 0)) > 0;) {
                    request.append(buffer, n);
                    end = request.find("\r\n\r\n");
                    if (end != std::string::npos) {
                        auto p = request.find("Content-Length:");
                        if (p == std::string::npos)
                            p = request.find("content-length:");
                        length = p == std::string::npos ? 0 : std::stoul(request.substr(p + 15));
                        if (request.size() >= end + 4 + length)
                            break;
                    }
                }
                std::string response = "{}";
                if (request.find(" /api/version ") != std::string::npos)
                    response = "{\"version\":\"test\"}";
                else if (request.find(" /api/tags ") != std::string::npos)
                    response = installed ? "{\"models\":[{\"name\":\"test-model\"}]}" : "{\"models\":[]}";
                else if (request.find(" /api/pull ") != std::string::npos) {
                    installed = true;
                    response = "{\"status\":\"pulling "
                               "test\",\"completed\":5,\"total\":10}\n{\"status\":\"success\"}\n";
                } else if (request.find(" /api/chat ") != std::string::npos) {
                    std::string answer =
                        request.find("You author small C exercises") != std::string::npos
                            ? exercise()
                            : "Follow the highlighted line. The pointer changes the box it points to.";
                    response = nlohmann::json({{"message", {{"content", answer}}}}).dump();
                }
                std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                    std::to_string(response.size()) + "\r\nConnection: close\r\n\r\n" +
                                    response;
                for (size_t sent = 0; sent < reply.size();) {
                    ssize_t n = send(client, reply.data() + sent, reply.size() - sent, MSG_NOSIGNAL);
                    if (n <= 0)
                        break;
                    sent += n;
                }
                close(client);
            }
        });
    }
    ~FakeTutor() {
        stopping = true;
        worker.join();
        close(fd);
    }
};

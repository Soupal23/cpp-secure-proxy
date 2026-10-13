#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

using namespace std;

int main(int argc, char* argv[]) {
    string url = "http://example.com/";

    if (argc >= 2) {
        url = argv[1];
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;

    int status = getaddrinfo("127.0.0.1", "8080", &hints, &result);

    if (status != 0) {
        cerr << "getaddrinfo: " << gai_strerror(status) << '\n';
        return 1;
    }

    int sockfd = -1;

    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (sockfd == -1) {
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd == -1) {
        cerr << "Could not connect to proxy at 127.0.0.1:8080\n";
        return 1;
    }

    string request =
        "GET " + url + " HTTP/1.1\r\n"
        "Host: " + url.substr(url.find("://") + 3) + "\r\n"
        "Connection: close\r\n"
        "\r\n";

    size_t sent = 0;

    while (sent < request.size()) {
        ssize_t n = send(
            sockfd,
            request.data() + sent,
            request.size() - sent,
            0
        );

        if (n <= 0) {
            perror("send");
            close(sockfd);
            return 1;
        }

        sent += static_cast<size_t>(n);
    }

    char buffer[4096];

    while (true) {
        ssize_t bytes = recv(sockfd, buffer, sizeof(buffer), 0);

        if (bytes == 0) {
            break;
        }

        if (bytes < 0) {
            perror("recv");
            close(sockfd);
            return 1;
        }

        cout.write(buffer, bytes);
    }

    close(sockfd);
    return 0;
}
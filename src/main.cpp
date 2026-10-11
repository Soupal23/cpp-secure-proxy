
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unordered_set>
#include <thread>
#include <functional>
#include <sys/time.h>
#include <cerrno>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;

bool send_all(int fd, const string& data) {
    size_t sent = 0;

    while (sent < data.size()) {
        ssize_t n = send(
            fd,
            data.data() + sent,
            data.size() - sent,
            0
        );

        if (n <= 0) return false;

        sent += static_cast<size_t>(n);
    }

    return true;
}

int connect_to_host(const string& host, const string& port) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;

    int status = getaddrinfo(
        host.c_str(), port.c_str(), &hints, &result
    );

    if (status != 0) {
        cerr << "DNS error: " << gai_strerror(status) << '\n';
        return -1;
    }

    int fd = -1;

    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (fd == -1) continue;

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

unordered_set<string> load_blocked_domains(const string& filename) {
    unordered_set<string> domains;
    ifstream file(filename);

    if (!file.is_open()) {
        cerr << "Could not open blocklist: " << filename << '\n';
        return domains;
    }

    string domain;

    while (getline(file, domain)) {
        size_t start = domain.find_first_not_of(" \t\r\n");

        if (start == string::npos) continue;

        size_t end = domain.find_last_not_of(" \t\r\n");
        domain = domain.substr(start, end - start + 1);

        transform(domain.begin(), domain.end(), domain.begin(),
            [](unsigned char c) {
                return static_cast<char>(tolower(c));
            });

        domains.insert(domain);
    }

    return domains;
}

bool is_domain_blocked( const string& host, const unordered_set<string>& blocked_domains ) {
    string domain = host;

    transform(domain.begin(), domain.end(), domain.begin(),
        [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });

    while (!domain.empty() && domain.back() == '.') {
        domain.pop_back();
    }

    while (true) {
        if (blocked_domains.count(domain)) {
            return true;
        }

        size_t pos = domain.find('.');

        if (pos == string::npos) break;

        domain = domain.substr(pos + 1);
    }

    return false;
}


bool set_socket_timeout(int fd, int seconds) {
    timeval timeout{};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    return setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    ) == 0;
}


void handle_client( int client_fd, const unordered_set<string>& blocked_domains ) {
    string request;
    char buffer[4096];
    
    if (!set_socket_timeout(client_fd, 10)) {
    perror("setsockopt client");
    close(client_fd);
    return;
    }

    while (request.find("\r\n\r\n") == string::npos && request.size() < 16384) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);

        if (n == 0) {
            // Client closed the connection.
            close(client_fd);
            return;
        }

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                cerr << "Client receive timeout\n";
            } else {
                perror("recv client");
            }

            close(client_fd);
            return;
        }

        request.append(buffer, static_cast<size_t>(n));
    }

    istringstream input(request);
    string method, url, version;
    input >> method >> url >> version;

    if (method != "GET" || url.rfind("http://", 0) != 0) {
        string response =
            "HTTP/1.1 501 Not Implemented\r\n"
            "Connection: close\r\n"
            "Content-Length: 0\r\n\r\n";

        send_all(client_fd, response);
        close(client_fd);
        return;
    }

    string destination = url.substr(7);
    size_t slash = destination.find('/');

    string host = destination.substr(0, slash);
    string path = (slash == string::npos)
        ? "/"
        : destination.substr(slash);

    string port = "80";
    size_t colon = host.rfind(':');

    if (colon != string::npos) {
        port = host.substr(colon + 1);
        host = host.substr(0, colon);
    }

    transform(host.begin(), host.end(), host.begin(),
        [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });

    cout << method << " " << host << path << '\n';

    if (is_domain_blocked(host, blocked_domains)) {
        string body = "Domain blocked\n";

        string response =
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "Content-Length: " + to_string(body.size()) +
            "\r\n\r\n" + body;

        send_all(client_fd, response);
        close(client_fd);
        return;
    }

    int upstream_fd = connect_to_host(host, port);

    if (upstream_fd == -1) {
        string response =
            "HTTP/1.1 502 Bad Gateway\r\n"
            "Connection: close\r\n"
            "Content-Length: 0\r\n\r\n";

        send_all(client_fd, response);
        close(client_fd);
        return;
    }

    if (!set_socket_timeout(upstream_fd, 10)) {
    perror("setsockopt upstream");
    close(upstream_fd);
    close(client_fd);
    return;
    }

    string upstream_request =
        method + " " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Connection: close\r\n\r\n";
    
    if (send_all(upstream_fd, upstream_request)) {
        while (true) {
            ssize_t n = recv(upstream_fd, buffer, sizeof(buffer), 0);

            if (n == 0) {
                // Upstream server closed the connection normally.
                break;
            }

            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    cerr << "Upstream receive timeout: "
                        << host << '\n';
                } else {
                    perror("recv upstream");
                }

                break;
            }

            string chunk(buffer, static_cast<size_t>(n));

            if (!send_all(client_fd, chunk)) break;
        }
    }

    close(upstream_fd);
    close(client_fd);
}

queue<int> client_queue;
mutex queue_mutex;
condition_variable queue_cv;

void worker(const unordered_set<string>& blocked_domains) {
    while (true) {
        int client_fd;

        {
            unique_lock<mutex> lock(queue_mutex);

            queue_cv.wait(lock, [] {
                return !client_queue.empty();
            });

            client_fd = client_queue.front();
            client_queue.pop();
        }

        handle_client(client_fd, blocked_domains);
    }
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    cout << "HTTP proxy listening on port 8080...\n";

    const unordered_set<string> blocked_domains =
        load_blocked_domains("config/blocked_domains.txt");

    cout << "Loaded " << blocked_domains.size()
         << " blocked domains\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        thread client_thread(
            handle_client,
            client_fd,
            cref(blocked_domains)
        );

        client_thread.detach();
    }

    close(server_fd);
    return 0;
}

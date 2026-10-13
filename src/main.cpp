
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
#include <poll.h>
#include <fcntl.h>
#include <array>
#include <csignal>

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

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}


struct PendingBuffer {
    array<char, 16384> data;
    size_t start = 0;
    size_t end = 0;

    size_t size() const {
        return end - start;
    }

    size_t space() const {
        return data.size() - end;
    }

    bool empty() const {
        return start == end;
    }

    void consume(size_t n) {
        start += n;

        if (start == end) {
            start = 0;
            end = 0;
        }
    }

    void compact() {
        if (start > 0 && start < end) {
            move(data.begin() + start, data.begin() + end, data.begin());
            end -= start;
            start = 0;
        }
    }
};

void relay_tunnel(int client_fd, int upstream_fd) {
    if (!set_nonblocking(client_fd) || !set_nonblocking(upstream_fd)) {
        perror("fcntl O_NONBLOCK");
        close(upstream_fd);
        close(client_fd);
        return;
    }

    PendingBuffer to_client;
    PendingBuffer to_upstream;

    bool client_eof = false;
    bool upstream_eof = false;
    bool client_write_closed = false;
    bool upstream_write_closed = false;

    while (true) {
        if (client_eof && upstream_eof && to_client.empty() && to_upstream.empty()) {
            break;
        }

        pollfd fds[2]{};

        fds[0].fd = client_fd;
        fds[0].events = 0;

        if (!client_eof && to_upstream.space() > 0) {
            fds[0].events |= POLLIN;
        }

        if (!to_client.empty()) {
            fds[0].events |= POLLOUT;
        }

        fds[1].fd = upstream_fd;
        fds[1].events = 0;

        if (!upstream_eof && to_client.space() > 0) {
            fds[1].events |= POLLIN;
        }

        if (!to_upstream.empty()) {
            fds[1].events |= POLLOUT;
        }

        int result = poll(fds, 2, 30000);

        if (result == 0) {
            cerr << "HTTPS tunnel idle timeout\n";
            break;
        }

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("poll");
            break;
        }

        for (int i = 0; i < 2; i++) {
            if (fds[i].revents & (POLLERR | POLLNVAL)) {
                goto cleanup;
            }
        }

        // Client -> upstream
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            if (!client_eof && to_upstream.space() > 0) {
                to_upstream.compact();

                ssize_t n = recv(
                    client_fd,
                    to_upstream.data.data() + to_upstream.end,
                    to_upstream.space(),
                    0
                );

                if (n > 0) {
                    to_upstream.end += static_cast<size_t>(n);
                } else if (n == 0) {
                    client_eof = true;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    perror("recv client");
                    goto cleanup;
                }
            }
        }

        // Upstream -> client
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            if (!upstream_eof && to_client.space() > 0) {
                to_client.compact();

                ssize_t n = recv(
                    upstream_fd,
                    to_client.data.data() + to_client.end,
                    to_client.space(),
                    0
                );

                if (n > 0) {
                    to_client.end += static_cast<size_t>(n);
                } else if (n == 0) {
                    upstream_eof = true;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    perror("recv upstream");
                    goto cleanup;
                }
            }
        }

        // Flush pending bytes to client
        if ((fds[0].revents & POLLOUT) && !to_client.empty()) {
            ssize_t n = send(
                client_fd,
                to_client.data.data() + to_client.start,
                to_client.size(),
                MSG_NOSIGNAL
            );

            if (n > 0) {
                to_client.consume(static_cast<size_t>(n));
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                perror("send client");
                goto cleanup;
            }
        }

        // Flush pending bytes to upstream
        if ((fds[1].revents & POLLOUT) && !to_upstream.empty()) {
            ssize_t n = send(
                upstream_fd,
                to_upstream.data.data() + to_upstream.start,
                to_upstream.size(),
                MSG_NOSIGNAL
            );

            if (n > 0) {
                to_upstream.consume(static_cast<size_t>(n));
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                perror("send upstream");
                goto cleanup;
            }
        }

        // Propagate half-closes only after buffered data is sent.
        if (client_eof && to_upstream.empty() && !upstream_write_closed) {
            shutdown(upstream_fd, SHUT_WR);
            upstream_write_closed = true;
        }

        if (upstream_eof && to_client.empty() && !client_write_closed) {
            shutdown(client_fd, SHUT_WR);
            client_write_closed = true;
        }
    }

cleanup:
    close(upstream_fd);
    close(client_fd);
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

    
    if (method == "CONNECT") {
        string host;
        int port = 443;

        size_t colon = url.rfind(':');

        if (colon == string::npos || colon == 0 || colon == url.size() - 1) {
            string response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Connection: close\r\n"
                "Content-Length: 0\r\n\r\n";

            send_all(client_fd, response);
            close(client_fd);
            return;
        }

        host = url.substr(0, colon);

        try {
            size_t pos;
            int parsed_port = stoi(url.substr(colon + 1), &pos);

            if (pos != url.size() - colon - 1 ||
                parsed_port < 1 || parsed_port > 65535) {
                throw invalid_argument("Invalid port");
            }

            port = parsed_port;
        } catch (...) {
            string response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Connection: close\r\n"
                "Content-Length: 0\r\n\r\n";

            send_all(client_fd, response);
            close(client_fd);
            return;
        }

        transform(host.begin(), host.end(), host.begin(),
                [](unsigned char c) {
                    return tolower(c);
                });

        if (is_domain_blocked(host, blocked_domains)) {
            string response =
                "HTTP/1.1 403 Forbidden\r\n"
                "Connection: close\r\n"
                "Content-Length: 0\r\n\r\n";

            send_all(client_fd, response);
            close(client_fd);
            return;
        }

        int upstream_fd = connect_to_host(host , to_string( port ) );

        if (upstream_fd == -1) {
            string response =
                "HTTP/1.1 502 Bad Gateway\r\n"
                "Connection: close\r\n"
                "Content-Length: 0\r\n\r\n";

            send_all(client_fd, response);
            close(client_fd);
            return;
        }

        string response =
            "HTTP/1.1 200 Connection Established\r\n\r\n";

        if (!send_all(client_fd, response)) {
            close(upstream_fd);
            close(client_fd);
            return;
        }

        relay_tunnel(client_fd, upstream_fd);
        return;
    }

    if (method != "GET" || url.substr(0, 7) != "http://") {
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
const size_t MAX_QUEUE_SIZE = 100;

bool stopping = false;
volatile sig_atomic_t stop_requested = 0;

void handle_signal(int signal_number) {
    stop_requested = 1;
}


void worker(const unordered_set<string>& blocked_domains) {
    while (true) {
        int client_fd;

        {
            unique_lock<mutex> lock(queue_mutex);

            queue_cv.wait(lock, [] {
                return stopping || !client_queue.empty();
            });

            if (stopping) {
                return;
            }

            client_fd = client_queue.front();
            client_queue.pop();
        }

        handle_client(client_fd, blocked_domains);
    }
}


int main() {

    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        perror("sigaction");
        return 1;
    }

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

    vector<thread> workers;

    for (int i = 0; i < 4; i++) {
        workers.emplace_back(worker, cref(blocked_domains));
    }

    while (!stop_requested) {
        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd == -1) {
            if (errno == EINTR && stop_requested) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            perror("accept");
            continue;
        }

        bool queued = false;

        {
            lock_guard<mutex> lock(queue_mutex);

            if (!stopping && client_queue.size() < MAX_QUEUE_SIZE) {
                client_queue.push(client_fd);
                queued = true;
            }
        }

        if (queued) {
            queue_cv.notify_one();
        } else {
            cerr << "Queue full or shutting down; rejecting connection\n";
            close(client_fd);
        }
    }

    // Begin shutdown.
    {
        lock_guard<mutex> lock(queue_mutex);
        stopping = true;

        while (!client_queue.empty()) {
            close(client_queue.front());
            client_queue.pop();
        }
    }

    queue_cv.notify_all();

    close(server_fd);

    for (thread& t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }

    cout << "\nProxy shut down cleanly.\n";
    return 0;

}

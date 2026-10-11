#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>
#include <netinet/in.h>
#include <unordered_set>
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
        // Remove leading and trailing whitespace.
        size_t start = domain.find_first_not_of(" \t\r\n");

        if (start == string::npos) continue;

        size_t end = domain.find_last_not_of(" \t\r\n");
        domain = domain.substr(start, end - start + 1);

        // Make matching case-insensitive.
        transform(domain.begin(), domain.end(), domain.begin(),
            [](unsigned char c) {
                return static_cast<char>(tolower(c));
            });

        domains.insert(domain);
    }

    return domains;
}


bool is_domain_blocked( const string& host , const unordered_set<string>& blocked_domains) {
    string domain = host;

    transform(domain.begin(), domain.end(), domain.begin(),
        [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });

    while (!domain.empty() && domain.back() == '.') {
        domain.pop_back();
    }

    size_t pos = 0;

    while (true) {
        if (blocked_domains.count(domain)) {
            return true;
        }

        pos = domain.find('.');

        if (pos == string::npos) {
            break;
        }

        domain = domain.substr(pos + 1);
    }

    return false;
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

    unordered_set<string> blocked_domains = load_blocked_domains("config/blocked_domains.txt");

    cout << "Loaded " << blocked_domains.size() << " blocked domains\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        string request;
        char buffer[4096];

        while (request.find("\r\n\r\n") == string::npos &&
               request.size() < 16384) {
            ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);

            if (n <= 0) break;

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
            continue;
        }

        string destination = url.substr(7);
        size_t slash = destination.find('/');
        string host = destination.substr(0, slash);
        string path = (slash == string::npos) ? "/" : destination.substr(slash);

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
        
        string blocked_body = "Domain blocked\n";
        if (is_domain_blocked(host, blocked_domains)) {
            string response =
                "HTTP/1.1 403 Forbidden\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n"
                "Content-Length: "+ to_string((int)blocked_body.size()) +
                "\r\n\r\n" + 
                blocked_body;

            send_all(client_fd, response);
            close(client_fd);
            continue;
        }

        int upstream_fd = connect_to_host(host, port);

        if (upstream_fd == -1) {
            string response =
                "HTTP/1.1 502 Bad Gateway\r\n"
                "Connection: close\r\n"
                "Content-Length: 0\r\n\r\n";

            send_all(client_fd, response);
            close(client_fd);
            continue;
        }

        string upstream_request =
            method + " " + path + " HTTP/1.1\r\n"
            "Host: " + host + "\r\n"
            "Connection: close\r\n\r\n";

        if (send_all(upstream_fd, upstream_request)) {
            while (true) {
                ssize_t n = recv(upstream_fd, buffer, sizeof(buffer), 0);

                if (n <= 0) break;

                string chunk(buffer, static_cast<size_t>(n));

                if (!send_all(client_fd, chunk)) break;
            }
        }

        close(upstream_fd);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}

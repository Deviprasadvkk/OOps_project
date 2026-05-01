#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

    struct BenchOptions
    {
        std::string host = "127.0.0.1";
        int port = 9090;
        int clients = 200;
        int requests_per_client = 200;
        std::string mode = "echo";
        std::string payload = "hello";
    };

    void usage()
    {
        std::cout << "Usage: ./benchmark_client --mode echo|http --host 127.0.0.1 --port 9090 --clients 200 --requests 200 --payload hello\n";
    }

    BenchOptions parse_args(int argc, char **argv)
    {
        BenchOptions opts;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--host" && i + 1 < argc)
            {
                opts.host = argv[++i];
            }
            else if (arg == "--port" && i + 1 < argc)
            {
                opts.port = std::stoi(argv[++i]);
            }
            else if (arg == "--clients" && i + 1 < argc)
            {
                opts.clients = std::stoi(argv[++i]);
            }
            else if (arg == "--requests" && i + 1 < argc)
            {
                opts.requests_per_client = std::stoi(argv[++i]);
            }
            else if (arg == "--mode" && i + 1 < argc)
            {
                opts.mode = argv[++i];
            }
            else if (arg == "--payload" && i + 1 < argc)
            {
                opts.payload = argv[++i];
            }
            else if (arg == "--help")
            {
                usage();
                std::exit(0);
            }
            else
            {
                throw std::invalid_argument("Unknown arg: " + arg);
            }
        }

        if (opts.clients <= 0 || opts.requests_per_client <= 0)
        {
            throw std::invalid_argument("clients and requests must be > 0");
        }
        if (opts.mode != "echo" && opts.mode != "http")
        {
            throw std::invalid_argument("mode must be echo or http");
        }

        return opts;
    }

    int connect_to(const std::string &host, int port)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            throw std::runtime_error("socket failed");
        }

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
        {
            ::close(fd);
            throw std::runtime_error("inet_pton failed");
        }

        if (::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            ::close(fd);
            throw std::runtime_error("connect failed");
        }

        return fd;
    }

    void write_all(int fd, const std::string &data)
    {
        std::size_t sent = 0;
        while (sent < data.size())
        {
            ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::runtime_error("send failed");
            }
            sent += static_cast<std::size_t>(n);
        }
    }

    std::string read_line(int fd)
    {
        std::string out;
        char c = 0;
        while (true)
        {
            ssize_t n = ::recv(fd, &c, 1, 0);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::runtime_error("recv failed");
            }
            if (n == 0)
            {
                throw std::runtime_error("connection closed");
            }
            out.push_back(c);
            if (c == '\n')
            {
                return out;
            }
        }
    }

    std::string read_http_response(int fd)
    {
        std::string response;
        char buf[4096];

        while (response.find("\r\n\r\n") == std::string::npos)
        {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::runtime_error("recv headers failed");
            }
            if (n == 0)
            {
                throw std::runtime_error("connection closed while reading headers");
            }
            response.append(buf, static_cast<std::size_t>(n));
        }

        std::size_t header_end = response.find("\r\n\r\n");
        std::string headers = response.substr(0, header_end + 4);

        std::size_t content_length = 0;
        std::size_t cl_pos = headers.find("Content-Length:");
        if (cl_pos != std::string::npos)
        {
            std::size_t val_start = cl_pos + std::strlen("Content-Length:");
            while (val_start < headers.size() && headers[val_start] == ' ')
            {
                ++val_start;
            }
            std::size_t val_end = headers.find("\r\n", val_start);
            content_length = static_cast<std::size_t>(std::stoul(headers.substr(val_start, val_end - val_start)));
        }

        std::size_t body_have = response.size() - (header_end + 4);
        while (body_have < content_length)
        {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::runtime_error("recv body failed");
            }
            if (n == 0)
            {
                throw std::runtime_error("connection closed while reading body");
            }
            response.append(buf, static_cast<std::size_t>(n));
            body_have += static_cast<std::size_t>(n);
        }

        return response;
    }

    double percentile_ms(std::vector<double> data, double p)
    {
        if (data.empty())
        {
            return 0.0;
        }
        std::sort(data.begin(), data.end());
        double idx = p * static_cast<double>(data.size() - 1);
        std::size_t lo = static_cast<std::size_t>(idx);
        std::size_t hi = std::min(lo + 1, data.size() - 1);
        double frac = idx - static_cast<double>(lo);
        return data[lo] * (1.0 - frac) + data[hi] * frac;
    }

} // namespace

int main(int argc, char **argv)
{
    try
    {
        BenchOptions opts = parse_args(argc, argv);

        std::vector<std::vector<double>> latencies(static_cast<std::size_t>(opts.clients));
        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(opts.clients));

        auto bench_start = std::chrono::steady_clock::now();

        for (int c = 0; c < opts.clients; ++c)
        {
            threads.emplace_back([c, &opts, &latencies]()
                                 {
                int fd = connect_to(opts.host, opts.port);

                for (int i = 0; i < opts.requests_per_client; ++i) {
                    auto t0 = std::chrono::steady_clock::now();

                    if (opts.mode == "echo") {
                        std::string req = opts.payload + "\n";
                        write_all(fd, req);
                        (void)read_line(fd);
                    } else {
                        std::string req = "GET / HTTP/1.1\r\nHost: benchmark\r\nConnection: keep-alive\r\n\r\n";
                        write_all(fd, req);
                        (void)read_http_response(fd);
                    }

                    auto t1 = std::chrono::steady_clock::now();
                    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                    latencies[static_cast<std::size_t>(c)].push_back(ms);
                }

                ::close(fd); });
        }

        for (std::thread &t : threads)
        {
            t.join();
        }

        auto bench_end = std::chrono::steady_clock::now();
        double total_s = std::chrono::duration<double>(bench_end - bench_start).count();

        std::vector<double> all;
        std::size_t total_req = 0;
        for (std::size_t i = 0; i < latencies.size(); ++i)
        {
            total_req += latencies[i].size();
            all.insert(all.end(), latencies[i].begin(), latencies[i].end());
        }

        double req_per_sec = total_req / total_s;
        double p50 = percentile_ms(all, 0.50);
        double p99 = percentile_ms(all, 0.99);

        std::cout << "mode=" << opts.mode << " clients=" << opts.clients
                  << " requests_per_client=" << opts.requests_per_client << "\n";
        std::cout << "total_requests=" << total_req << " total_time_s=" << total_s << "\n";
        std::cout << "throughput_req_per_s=" << req_per_sec << "\n";
        std::cout << "p50_latency_ms=" << p50 << "\n";
        std::cout << "p99_latency_ms=" << p99 << "\n";
    }
    catch (const std::exception &ex)
    {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}

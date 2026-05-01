#ifndef __linux__
#include <iostream>

int main()
{
    std::cerr << "This server uses epoll and POSIX sockets. Build and run on Linux.\n";
    return 1;
}

#else

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

    class ScopedFd
    {
    public:
        ScopedFd() : fd_(-1) {}
        explicit ScopedFd(int fd) : fd_(fd) {}

        ~ScopedFd()
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
            }
        }

        ScopedFd(const ScopedFd &) = delete;
        ScopedFd &operator=(const ScopedFd &) = delete;

        ScopedFd(ScopedFd &&other) noexcept : fd_(other.fd_)
        {
            other.fd_ = -1;
        }

        ScopedFd &operator=(ScopedFd &&other) noexcept
        {
            if (this != &other)
            {
                reset();
                fd_ = other.fd_;
                other.fd_ = -1;
            }
            return *this;
        }

        int get() const { return fd_; }

        int release()
        {
            int tmp = fd_;
            fd_ = -1;
            return tmp;
        }

        void reset(int fd = -1)
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
            }
            fd_ = fd;
        }

    private:
        int fd_;
    };

    void throw_errno(const std::string &message)
    {
        throw std::runtime_error(message + ": " + std::strerror(errno));
    }

    void set_nonblocking(int fd)
    {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0)
        {
            throw_errno("fcntl(F_GETFL) failed");
        }
        if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            throw_errno("fcntl(F_SETFL) failed");
        }
    }

    class ThreadPool
    {
    public:
        explicit ThreadPool(std::size_t thread_count) : stop_(false)
        {
            workers_.reserve(thread_count);
            for (std::size_t i = 0; i < thread_count; ++i)
            {
                workers_.emplace_back([this]()
                                      {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                } });
            }
        }

        ~ThreadPool()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
            }
            cv_.notify_all();
            for (std::thread &t : workers_)
            {
                if (t.joinable())
                {
                    t.join();
                }
            }
        }

        template <typename F>
        void enqueue(F &&task)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tasks_.push(std::function<void()>(std::forward<F>(task)));
            }
            cv_.notify_one();
        }

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool stop_;
    };

    enum class ServerMode
    {
        Echo,
        Chat,
        Http
    };

    ServerMode parse_mode(const std::string &mode)
    {
        if (mode == "echo")
        {
            return ServerMode::Echo;
        }
        if (mode == "chat")
        {
            return ServerMode::Chat;
        }
        if (mode == "http")
        {
            return ServerMode::Http;
        }
        throw std::invalid_argument("Unknown mode. Use: echo | chat | http");
    }

    struct Job
    {
        int client_fd;
        std::string payload;
    };

    struct Completion
    {
        enum class Kind
        {
            Direct,
            Broadcast,
            CloseOnly
        };

        Kind kind;
        int client_fd;
        std::string payload;
        bool close_after_write;
    };

    class CompletionQueue
    {
    public:
        explicit CompletionQueue(int wake_fd) : wake_fd_(wake_fd) {}

        void push(Completion item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push(std::move(item));
            }
            uint64_t value = 1;
            ssize_t written = ::write(wake_fd_, &value, sizeof(value));
            (void)written;
        }

        bool try_pop(Completion &out)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty())
            {
                return false;
            }
            out = std::move(queue_.front());
            queue_.pop();
            return true;
        }

    private:
        int wake_fd_;
        std::queue<Completion> queue_;
        std::mutex mutex_;
    };

    struct Connection
    {
        explicit Connection(int fd_value) : fd(fd_value), wants_close(false) {}

        int fd;
        std::string in_buffer;
        std::string out_buffer;
        bool wants_close;
    };

    struct Options
    {
        uint16_t port = 9090;
        std::size_t thread_count = std::max<std::size_t>(2, std::thread::hardware_concurrency());
        int max_events = 1024;
        ServerMode mode = ServerMode::Echo;
    };

    Options parse_args(int argc, char **argv)
    {
        Options options;

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc)
            {
                options.port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
            else if (arg == "--threads" && i + 1 < argc)
            {
                options.thread_count = static_cast<std::size_t>(std::stoul(argv[++i]));
                if (options.thread_count == 0)
                {
                    throw std::invalid_argument("--threads must be >= 1");
                }
            }
            else if (arg == "--mode" && i + 1 < argc)
            {
                options.mode = parse_mode(argv[++i]);
            }
            else if (arg == "--max-events" && i + 1 < argc)
            {
                options.max_events = std::stoi(argv[++i]);
                if (options.max_events <= 0)
                {
                    throw std::invalid_argument("--max-events must be > 0");
                }
            }
            else if (arg == "--help")
            {
                std::cout << "Usage: ./concurrent_server --mode echo|chat|http --port 9090 --threads 8 [--max-events 1024]\n";
                std::exit(0);
            }
            else
            {
                throw std::invalid_argument("Unknown argument: " + arg);
            }
        }

        return options;
    }

    std::string build_http_response(const std::string &request, bool *close_after_write)
    {
        std::string body = "Hello from epoll C++17 server\n";
        body += "Request bytes: " + std::to_string(request.size()) + "\n";

        const bool should_close = request.find("Connection: close") != std::string::npos ||
                                  request.find("connection: close") != std::string::npos;
        *close_after_write = should_close;

        std::string response;
        response += "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/plain\r\n";
        response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        response += std::string("Connection: ") + (should_close ? "close" : "keep-alive") + "\r\n";
        response += "\r\n";
        response += body;
        return response;
    }

    class EpollServer
    {
    public:
        explicit EpollServer(const Options &options)
            : options_(options),
              listener_(create_listener(options.port)),
              epoll_fd_(create_epoll()),
              wake_fd_(create_wakefd()),
              worker_pool_(options.thread_count),
              completion_queue_(wake_fd_.get())
        {
            add_epoll_fd(listener_.get(), EPOLLIN | EPOLLET);
            add_epoll_fd(wake_fd_.get(), EPOLLIN | EPOLLET);
        }

        void run()
        {
            std::vector<epoll_event> events(static_cast<std::size_t>(options_.max_events));

            std::cout << "Server started: mode=" << mode_name()
                      << " port=" << options_.port
                      << " worker_threads=" << options_.thread_count
                      << "\n";

            while (true)
            {
                int n = ::epoll_wait(epoll_fd_.get(), events.data(), options_.max_events, -1);
                if (n < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    throw_errno("epoll_wait failed");
                }

                for (int i = 0; i < n; ++i)
                {
                    const epoll_event &ev = events[static_cast<std::size_t>(i)];
                    int fd = static_cast<int>(ev.data.fd);

                    if (fd == listener_.get())
                    {
                        accept_loop();
                        continue;
                    }

                    if (fd == wake_fd_.get())
                    {
                        drain_wakefd();
                        process_completions();
                        continue;
                    }

                    if ((ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0)
                    {
                        close_connection(fd);
                        continue;
                    }

                    if ((ev.events & EPOLLIN) != 0)
                    {
                        read_loop(fd);
                    }

                    if ((ev.events & EPOLLOUT) != 0)
                    {
                        flush_outbound(fd);
                    }
                }
            }
        }

    private:
        static ScopedFd create_listener(uint16_t port)
        {
            ScopedFd sock(::socket(AF_INET, SOCK_STREAM, 0));
            if (sock.get() < 0)
            {
                throw_errno("socket failed");
            }

            int opt = 1;
            if (::setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            {
                throw_errno("setsockopt(SO_REUSEADDR) failed");
            }

            set_nonblocking(sock.get());

            sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(port);

            if (::bind(sock.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0)
            {
                throw_errno("bind failed");
            }

            if (::listen(sock.get(), SOMAXCONN) < 0)
            {
                throw_errno("listen failed");
            }

            return sock;
        }

        static ScopedFd create_epoll()
        {
            ScopedFd ep(::epoll_create1(EPOLL_CLOEXEC));
            if (ep.get() < 0)
            {
                throw_errno("epoll_create1 failed");
            }
            return ep;
        }

        static ScopedFd create_wakefd()
        {
            ScopedFd ev(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
            if (ev.get() < 0)
            {
                throw_errno("eventfd failed");
            }
            return ev;
        }

        void add_epoll_fd(int fd, uint32_t flags)
        {
            epoll_event ev;
            std::memset(&ev, 0, sizeof(ev));
            ev.data.fd = fd;
            ev.events = flags;

            if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &ev) < 0)
            {
                throw_errno("epoll_ctl(ADD) failed");
            }
        }

        void mod_epoll_fd(int fd, uint32_t flags)
        {
            epoll_event ev;
            std::memset(&ev, 0, sizeof(ev));
            ev.data.fd = fd;
            ev.events = flags;

            if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &ev) < 0)
            {
                if (errno == ENOENT)
                {
                    return;
                }
                throw_errno("epoll_ctl(MOD) failed");
            }
        }

        void del_epoll_fd(int fd)
        {
            if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr) < 0)
            {
                if (errno == ENOENT || errno == EBADF)
                {
                    return;
                }
                throw_errno("epoll_ctl(DEL) failed");
            }
        }

        std::string mode_name() const
        {
            switch (options_.mode)
            {
            case ServerMode::Echo:
                return "echo";
            case ServerMode::Chat:
                return "chat";
            case ServerMode::Http:
                return "http";
            }
            return "unknown";
        }

        void accept_loop()
        {
            while (true)
            {
                sockaddr_in in_addr;
                socklen_t in_len = sizeof(in_addr);
                int client_fd = ::accept(listener_.get(), reinterpret_cast<sockaddr *>(&in_addr), &in_len);
                if (client_fd < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        break;
                    }
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    throw_errno("accept failed");
                }

                set_nonblocking(client_fd);
                connections_[client_fd] = std::make_unique<Connection>(client_fd);
                add_epoll_fd(client_fd, EPOLLIN | EPOLLRDHUP | EPOLLET);
            }
        }

        void close_connection(int fd)
        {
            auto it = connections_.find(fd);
            if (it == connections_.end())
            {
                return;
            }

            del_epoll_fd(fd);
            ::close(fd);
            connections_.erase(it);
        }

        void drain_wakefd()
        {
            uint64_t val = 0;
            while (true)
            {
                ssize_t n = ::read(wake_fd_.get(), &val, sizeof(val));
                if (n < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        break;
                    }
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    throw_errno("read(eventfd) failed");
                }
                if (n == 0)
                {
                    break;
                }
            }
        }

        void process_completions()
        {
            Completion done;
            while (completion_queue_.try_pop(done))
            {
                if (done.kind == Completion::Kind::CloseOnly)
                {
                    close_connection(done.client_fd);
                    continue;
                }

                if (done.kind == Completion::Kind::Broadcast)
                {
                    for (std::unordered_map<int, std::unique_ptr<Connection>>::iterator it = connections_.begin();
                         it != connections_.end();
                         ++it)
                    {
                        Connection &c = *it->second;
                        c.out_buffer.append(done.payload);
                        mod_interest(c);
                        flush_outbound(c.fd);
                    }
                    continue;
                }

                std::unordered_map<int, std::unique_ptr<Connection>>::iterator it = connections_.find(done.client_fd);
                if (it == connections_.end())
                {
                    continue;
                }

                Connection &c = *it->second;
                c.out_buffer.append(done.payload);
                if (done.close_after_write)
                {
                    c.wants_close = true;
                }
                mod_interest(c);
                flush_outbound(c.fd);
            }
        }

        void mod_interest(const Connection &c)
        {
            uint32_t flags = EPOLLIN | EPOLLRDHUP | EPOLLET;
            if (!c.out_buffer.empty())
            {
                flags |= EPOLLOUT;
            }
            mod_epoll_fd(c.fd, flags);
        }

        void read_loop(int fd)
        {
            std::unordered_map<int, std::unique_ptr<Connection>>::iterator it = connections_.find(fd);
            if (it == connections_.end())
            {
                return;
            }

            Connection &conn = *it->second;
            char buffer[8192];

            while (true)
            {
                ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
                if (n < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        break;
                    }
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    close_connection(fd);
                    return;
                }

                if (n == 0)
                {
                    close_connection(fd);
                    return;
                }

                conn.in_buffer.append(buffer, static_cast<std::size_t>(n));
                extract_requests(conn);
            }
        }

        void extract_requests(Connection &conn)
        {
            if (options_.mode == ServerMode::Http)
            {
                while (true)
                {
                    std::size_t pos = conn.in_buffer.find("\r\n\r\n");
                    if (pos == std::string::npos)
                    {
                        break;
                    }
                    std::string request = conn.in_buffer.substr(0, pos + 4);
                    conn.in_buffer.erase(0, pos + 4);
                    submit_job(conn.fd, std::move(request));
                }
                return;
            }

            while (true)
            {
                std::size_t pos = conn.in_buffer.find('\n');
                if (pos == std::string::npos)
                {
                    break;
                }

                std::string line = conn.in_buffer.substr(0, pos);
                conn.in_buffer.erase(0, pos + 1);
                if (!line.empty() && line[line.size() - 1] == '\r')
                {
                    line.erase(line.size() - 1);
                }
                submit_job(conn.fd, std::move(line));
            }
        }

        void submit_job(int fd, std::string payload)
        {
            std::unique_ptr<Job> job(new Job());
            job->client_fd = fd;
            job->payload = std::move(payload);

            worker_pool_.enqueue([this, job = std::move(job)]()
                                 {
            Completion done;
            done.client_fd = job->client_fd;
            done.close_after_write = false;

            if (options_.mode == ServerMode::Echo) {
                done.kind = Completion::Kind::Direct;
                done.payload = job->payload + "\n";
                completion_queue_.push(std::move(done));
                return;
            }

            if (options_.mode == ServerMode::Chat) {
                if (job->payload == "/quit") {
                    done.kind = Completion::Kind::Direct;
                    done.payload = "bye\n";
                    done.close_after_write = true;
                    completion_queue_.push(std::move(done));
                    return;
                }

                done.kind = Completion::Kind::Broadcast;
                done.payload = "client[" + std::to_string(job->client_fd) + "]: " + job->payload + "\n";
                completion_queue_.push(std::move(done));
                return;
            }

            done.kind = Completion::Kind::Direct;
            done.payload = build_http_response(job->payload, &done.close_after_write);
            completion_queue_.push(std::move(done)); });
        }

        void flush_outbound(int fd)
        {
            std::unordered_map<int, std::unique_ptr<Connection>>::iterator it = connections_.find(fd);
            if (it == connections_.end())
            {
                return;
            }

            Connection &conn = *it->second;
            while (!conn.out_buffer.empty())
            {
                ssize_t n = ::send(fd, conn.out_buffer.data(), conn.out_buffer.size(), MSG_NOSIGNAL);
                if (n < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        break;
                    }
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    close_connection(fd);
                    return;
                }

                if (n == 0)
                {
                    break;
                }

                conn.out_buffer.erase(0, static_cast<std::size_t>(n));
            }

            if (conn.out_buffer.empty())
            {
                if (conn.wants_close)
                {
                    close_connection(fd);
                    return;
                }
            }

            mod_interest(conn);
        }

    private:
        Options options_;
        ScopedFd listener_;
        ScopedFd epoll_fd_;
        ScopedFd wake_fd_;
        ThreadPool worker_pool_;
        CompletionQueue completion_queue_;
        std::unordered_map<int, std::unique_ptr<Connection>> connections_;
    };

} // namespace

int main(int argc, char **argv)
{
    ::signal(SIGPIPE, SIG_IGN);

    try
    {
        Options options = parse_args(argc, argv);
        EpollServer server(options);
        server.run();
    }
    catch (const std::exception &ex)
    {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}

#endif

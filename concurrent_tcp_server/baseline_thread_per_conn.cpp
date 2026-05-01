#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{

    void throw_errno(const std::string &message)
    {
        throw std::runtime_error(message + ": " + std::strerror(errno));
    }

    void handle_client(int client_fd)
    {
        std::string in_buffer;
        char buf[4096];

        while (true)
        {
            ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
            if (n == 0)
            {
                break;
            }

            in_buffer.append(buf, static_cast<std::size_t>(n));

            while (true)
            {
                std::size_t pos = in_buffer.find('\n');
                if (pos == std::string::npos)
                {
                    break;
                }
                std::string line = in_buffer.substr(0, pos + 1);
                in_buffer.erase(0, pos + 1);

                std::size_t sent = 0;
                while (sent < line.size())
                {
                    ssize_t w = ::send(client_fd, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);
                    if (w < 0)
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        ::close(client_fd);
                        return;
                    }
                    sent += static_cast<std::size_t>(w);
                }
            }
        }

        ::close(client_fd);
    }

} // namespace

int main(int argc, char **argv)
{
    ::signal(SIGPIPE, SIG_IGN);

    int port = 9091;
    if (argc > 1)
    {
        port = std::stoi(argv[1]);
    }

    try
    {
        int listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0)
        {
            throw_errno("socket failed");
        }

        int opt = 1;
        if (::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            ::close(listener);
            throw_errno("setsockopt failed");
        }

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (::bind(listener, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            ::close(listener);
            throw_errno("bind failed");
        }

        if (::listen(listener, SOMAXCONN) < 0)
        {
            ::close(listener);
            throw_errno("listen failed");
        }

        std::cout << "baseline thread-per-connection echo server listening on " << port << "\n";

        while (true)
        {
            int client_fd = ::accept(listener, nullptr, nullptr);
            if (client_fd < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw_errno("accept failed");
            }

            std::thread t(handle_client, client_fd);
            t.detach();
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}

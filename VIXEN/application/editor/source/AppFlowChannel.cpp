#include "AppFlowChannel.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#if !defined(_WIN32)
#  include <cerrno>
#endif

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <process.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <sys/stat.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
int LastSocketError() { return WSAGetLastError(); }
void CloseSocket(Socket socket) { if (socket != kInvalidSocket) closesocket(socket); }
bool WouldBlock(int error) { return error == WSAEWOULDBLOCK; }
bool MakeNonBlocking(Socket socket) {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
int ProcessId() { return _getpid(); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
int LastSocketError() { return errno; }
void CloseSocket(Socket socket) { if (socket != kInvalidSocket) ::close(socket); }
bool WouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK; }
bool MakeNonBlocking(Socket socket) {
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
int ProcessId() { return static_cast<int>(getpid()); }
#endif

std::string NewToken() {
    std::random_device random;
    std::mt19937_64 engine((static_cast<uint64_t>(random()) << 32) ^ random() ^
                           static_cast<uint64_t>(Clock::now().time_since_epoch().count()));
    constexpr char hex[] = "0123456789abcdef";
    std::string token(64, '0');
    for (size_t i = 0; i < token.size(); i += 2) {
        const uint8_t byte = static_cast<uint8_t>(engine());
        token[i] = hex[byte >> 4];
        token[i + 1] = hex[byte & 15];
    }
    return token;
}

bool ConstantTimeEqual(const std::string& left, const std::string& right) {
    size_t difference = left.size() ^ right.size();
    const size_t count = std::max(left.size(), right.size());
    for (size_t i = 0; i < count; ++i) {
        const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        difference |= static_cast<size_t>(a ^ b);
    }
    return difference == 0;
}

std::string DescriptorPath() {
    if (const char* overridePath = std::getenv("VIXEN_APPFLOW_DESCRIPTOR")) {
        if (*overridePath) return overridePath;
    }
    return (std::filesystem::temp_directory_path() /
            ("vixen-appflow-" + std::to_string(ProcessId()) + ".json")).string();
}
} // namespace

struct AppFlowChannel::Impl {
    struct Peer {
        Socket socket = kInvalidSocket;
        std::string input;
        std::string output;
        size_t outputOffset = 0;
        Json request;
        Json id;
        bool hasRequest = false;
        Clock::time_point activity = Clock::now();
    };

    Socket listener = kInvalidSocket;
    std::vector<Peer> peers;
};

AppFlowChannel::AppFlowChannel() : impl_(std::make_unique<Impl>()) {}

AppFlowChannel::~AppFlowChannel() {
    if (!impl_) return;
    for (auto& peer : impl_->peers) CloseSocket(peer.socket);
    CloseSocket(impl_->listener);
#if defined(_WIN32)
    WSACleanup();
#endif
    if (!descriptorPath_.empty()) {
        std::error_code ignored;
        std::filesystem::remove(descriptorPath_, ignored);
    }
}

bool AppFlowChannel::Start(std::string& error) {
    if (impl_->listener != kInvalidSocket) return true;
#if defined(_WIN32)
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        error = "WSAStartup failed";
        return false;
    }
#endif
    impl_->listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->listener == kInvalidSocket) {
        error = "socket creation failed: " + std::to_string(LastSocketError());
        return false;
    }
    int reuse = 1;
    setsockopt(impl_->listener, SOL_SOCKET, SO_REUSEADDR,
#if defined(_WIN32)
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
               &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(impl_->listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(impl_->listener, 16) != 0 || !MakeNonBlocking(impl_->listener)) {
        error = "loopback socket setup failed: " + std::to_string(LastSocketError());
        CloseSocket(impl_->listener);
        impl_->listener = kInvalidSocket;
        return false;
    }

    sockaddr_in bound{};
#if defined(_WIN32)
    int boundSize = sizeof(bound);
#else
    socklen_t boundSize = sizeof(bound);
#endif
    if (getsockname(impl_->listener, reinterpret_cast<sockaddr*>(&bound), &boundSize) != 0) {
        error = "getsockname failed: " + std::to_string(LastSocketError());
        CloseSocket(impl_->listener);
        impl_->listener = kInvalidSocket;
        return false;
    }

    token_ = NewToken();
    descriptorPath_ = ::DescriptorPath();
    Json descriptor{
        {"protocol", "vixen-appflow-jsonl/1"},
        {"transport", "tcp-loopback"},
        {"host", "127.0.0.1"},
        {"port", ntohs(bound.sin_port)},
        {"token", token_},
        {"pid", ProcessId()}
    };
    std::ofstream file(descriptorPath_, std::ios::binary | std::ios::trunc);
    if (!file || !(file << descriptor.dump() << '\n')) {
        error = "could not write AppFlow channel descriptor: " + descriptorPath_;
        CloseSocket(impl_->listener);
        impl_->listener = kInvalidSocket;
        return false;
    }
    file.close();
#if !defined(_WIN32)
    if (chmod(descriptorPath_.c_str(), S_IRUSR | S_IWUSR) != 0) {
        error = "could not restrict channel descriptor permissions: " + descriptorPath_;
        CloseSocket(impl_->listener);
        impl_->listener = kInvalidSocket;
        std::error_code ignored;
        std::filesystem::remove(descriptorPath_, ignored);
        return false;
    }
#endif
    return true;
}

void AppFlowChannel::Poll(const Handler& handler) {
    if (impl_->listener == kInvalidSocket) return;

    for (;;) {
        sockaddr_in remote{};
#if defined(_WIN32)
        int remoteSize = sizeof(remote);
#else
        socklen_t remoteSize = sizeof(remote);
#endif
        const Socket socket = ::accept(impl_->listener, reinterpret_cast<sockaddr*>(&remote), &remoteSize);
        if (socket == kInvalidSocket) {
            if (!WouldBlock(LastSocketError())) break;
            break;
        }
        if (ntohl(remote.sin_addr.s_addr) != INADDR_LOOPBACK || !MakeNonBlocking(socket) ||
            impl_->peers.size() >= 64) {
            CloseSocket(socket);
            continue;
        }
        Impl::Peer peer;
        peer.socket = socket;
        impl_->peers.push_back(std::move(peer));
    }

    for (auto it = impl_->peers.begin(); it != impl_->peers.end();) {
        auto& peer = *it;
        bool remove = false;
        if (!peer.hasRequest) {
            std::array<char, 8192> buffer{};
            for (;;) {
#if defined(_WIN32)
                const int got = ::recv(peer.socket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
                const ssize_t got = ::recv(peer.socket, buffer.data(), buffer.size(), 0);
#endif
                if (got > 0) {
                    peer.activity = Clock::now();
                    peer.input.append(buffer.data(), static_cast<size_t>(got));
                    if (peer.input.size() > 4 * 1024 * 1024) {
                        peer.output = R"({"ok":false,"error":"request exceeds 4 MiB"})";
                        peer.output.push_back('\n');
                        peer.hasRequest = true;
                        break;
                    }
                    if (peer.input.find('\n') != std::string::npos) break;
                    continue;
                }
                if (got == 0) {
                    remove = true;
                    break;
                }
                const int socketError = LastSocketError();
                if (!WouldBlock(socketError)) remove = true;
                break;
            }
            if (!remove && peer.output.empty()) {
                const size_t newline = peer.input.find('\n');
                if (newline != std::string::npos) {
                    const std::string line = peer.input.substr(0, newline);
                    const auto parsed = Json::parse(line, nullptr, false);
                    if (parsed.is_discarded() || !parsed.is_object()) {
                        peer.output = R"({"ok":false,"error":"invalid JSON request"})";
                        peer.output.push_back('\n');
                        peer.hasRequest = true;
                    } else {
                        peer.request = parsed;
                        peer.id = parsed.value("id", Json(nullptr));
                        peer.hasRequest = true;
                        const std::string supplied = parsed.value("token", std::string{});
                        if (!ConstantTimeEqual(supplied, token_)) {
                            peer.output = Json{{"id", peer.id}, {"ok", false},
                                               {"error", "unauthorized session token"}}.dump();
                            peer.output.push_back('\n');
                        }
                    }
                }
            }
        }

        if (!remove && peer.hasRequest && peer.output.empty()) {
            try {
                auto result = handler(peer.request.dump());
                if (result) {
                    const Json resultJson = Json::parse(*result, nullptr, false);
                    if (resultJson.is_discarded()) {
                        peer.output = Json{{"id", peer.id}, {"ok", false},
                                           {"error", "AppFlow handler returned invalid JSON"}}.dump();
                    } else {
                        peer.output = Json{{"id", peer.id}, {"ok", true}, {"result", resultJson}}.dump();
                    }
                    peer.output.push_back('\n');
                }
            } catch (const std::exception& exception) {
                peer.output = Json{{"id", peer.id}, {"ok", false}, {"error", exception.what()}}.dump();
                peer.output.push_back('\n');
            } catch (...) {
                peer.output = R"({"ok":false,"error":"unknown AppFlow handler error"})";
                peer.output.push_back('\n');
            }
        }

        if (!remove && !peer.output.empty()) {
            while (peer.outputOffset < peer.output.size()) {
#if defined(_WIN32)
                const int sent = ::send(peer.socket, peer.output.data() + peer.outputOffset,
                    static_cast<int>(std::min<size_t>(peer.output.size() - peer.outputOffset, 1u << 20)), 0);
#else
                const ssize_t sent = ::send(peer.socket, peer.output.data() + peer.outputOffset,
                    std::min<size_t>(peer.output.size() - peer.outputOffset, 1u << 20), MSG_NOSIGNAL);
#endif
                if (sent > 0) {
                    peer.outputOffset += static_cast<size_t>(sent);
                    peer.activity = Clock::now();
                    continue;
                }
                if (sent < 0 && WouldBlock(LastSocketError())) break;
                remove = true;
                break;
            }
            if (peer.outputOffset == peer.output.size()) remove = true;
        }
        if (Clock::now() - peer.activity > std::chrono::seconds(120)) remove = true;
        if (remove) {
            CloseSocket(peer.socket);
            it = impl_->peers.erase(it);
        } else {
            ++it;
        }
    }
}

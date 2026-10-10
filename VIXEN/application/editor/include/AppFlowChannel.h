#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Local, per-editor AppFlow JSON-lines endpoint. The editor owns the listener and pumps it on
// its frame thread so dispatch handlers never cross a thread boundary.
class AppFlowChannel {
public:
    using Handler = std::function<std::optional<std::string>(const std::string&)>;

    AppFlowChannel();
    ~AppFlowChannel();
    AppFlowChannel(const AppFlowChannel&) = delete;
    AppFlowChannel& operator=(const AppFlowChannel&) = delete;

    bool Start(std::string& error);
    void Poll(const Handler& handler);
    void SetPresentedFrame(uint64_t frame) { presentedFrame_ = frame; }
    uint64_t PresentedFrame() const { return presentedFrame_; }
    const std::string& DescriptorPath() const { return descriptorPath_; }
    const std::string& Token() const { return token_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string token_;
    std::string descriptorPath_;
    uint64_t presentedFrame_ = 0;
};

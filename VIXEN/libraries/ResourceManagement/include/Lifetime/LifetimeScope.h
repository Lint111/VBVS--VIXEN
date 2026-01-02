#pragma once

#include "Lifetime/SharedResource.h"
#include <vector>
#include <string>
#include <string_view>
#include <stack>
#include <cassert>

namespace ResourceManagement {

/**
 * @brief Groups resources for bulk lifetime management
 *
 * Resources created within a scope are tracked and can be released together
 * when the scope ends. Supports nested scopes through parent relationships.
 *
 * Thread-safety: NOT thread-safe. Use one scope per thread or external sync.
 *
 * Usage:
 * ```cpp
 * LifetimeScope passScope("ShadowPass", &factory);
 * auto buffer = passScope.CreateBuffer(request);
 * auto image = passScope.CreateImage(imageRequest);
 * // ... use resources ...
 * passScope.EndScope();  // All resources released
 * ```
 */
class LifetimeScope {
public:
    /**
     * @brief Create a new lifetime scope
     *
     * @param name Scope name for debugging
     * @param factory Factory for resource creation (required)
     * @param parent Optional parent scope for hierarchy
     */
    explicit LifetimeScope(
        std::string_view name,
        SharedResourceFactory* factory,
        LifetimeScope* parent = nullptr)
        : name_(name)
        , factory_(factory)
        , parent_(parent)
    {
        assert(factory && "LifetimeScope requires a SharedResourceFactory");
    }

    ~LifetimeScope() {
        // Auto-end scope on destruction if not already ended
        if (!ended_) {
            EndScope();
        }
    }

    // Non-copyable (owns resources)
    LifetimeScope(const LifetimeScope&) = delete;
    LifetimeScope& operator=(const LifetimeScope&) = delete;

    // Movable
    LifetimeScope(LifetimeScope&& other) noexcept
        : name_(std::move(other.name_))
        , factory_(other.factory_)
        , parent_(other.parent_)
        , buffers_(std::move(other.buffers_))
        , images_(std::move(other.images_))
        , ended_(other.ended_)
    {
        other.factory_ = nullptr;
        other.parent_ = nullptr;
        other.ended_ = true;
    }

    LifetimeScope& operator=(LifetimeScope&& other) noexcept {
        if (this != &other) {
            if (!ended_) {
                EndScope();
            }
            name_ = std::move(other.name_);
            factory_ = other.factory_;
            parent_ = other.parent_;
            buffers_ = std::move(other.buffers_);
            images_ = std::move(other.images_);
            ended_ = other.ended_;
            other.factory_ = nullptr;
            other.parent_ = nullptr;
            other.ended_ = true;
        }
        return *this;
    }

    // =========================================================================
    // Resource Creation
    // =========================================================================

    /**
     * @brief Create a buffer within this scope
     *
     * The buffer is tracked and will be released when EndScope() is called.
     *
     * @param request Buffer allocation parameters
     * @param scope Resource scope (default: Transient for scoped resources)
     * @return Shared pointer to buffer, or empty on failure
     */
    [[nodiscard]] SharedBufferPtr CreateBuffer(
        const BufferAllocationRequest& request,
        ResourceScope scope = ResourceScope::Transient)
    {
        assert(!ended_ && "Cannot create resources in ended scope");
        assert(factory_ && "Factory is null");

        auto buffer = factory_->CreateBuffer(request, scope);
        if (buffer) {
            buffers_.push_back(buffer);  // Copy adds reference
        }
        return buffer;
    }

    /**
     * @brief Create an image within this scope
     *
     * The image is tracked and will be released when EndScope() is called.
     */
    [[nodiscard]] SharedImagePtr CreateImage(
        const ImageAllocationRequest& request,
        ResourceScope scope = ResourceScope::Transient)
    {
        assert(!ended_ && "Cannot create resources in ended scope");
        assert(factory_ && "Factory is null");

        auto image = factory_->CreateImage(request, scope);
        if (image) {
            images_.push_back(image);  // Copy adds reference
        }
        return image;
    }

    /**
     * @brief Register an externally-created buffer with this scope
     *
     * Use when a buffer was created outside the scope but should be
     * released when this scope ends.
     */
    void TrackBuffer(const SharedBufferPtr& buffer) {
        assert(!ended_ && "Cannot track resources in ended scope");
        if (buffer) {
            buffers_.push_back(buffer);
        }
    }

    /**
     * @brief Register an externally-created image with this scope
     */
    void TrackImage(const SharedImagePtr& image) {
        assert(!ended_ && "Cannot track resources in ended scope");
        if (image) {
            images_.push_back(image);
        }
    }

    // =========================================================================
    // Scope Lifecycle
    // =========================================================================

    /**
     * @brief End the scope and release all tracked resources
     *
     * Releases this scope's reference to all tracked resources.
     * Resources with other references remain alive.
     * Safe to call multiple times (subsequent calls are no-ops).
     */
    void EndScope() {
        if (ended_) {
            return;
        }

        // Clear our vectors, which releases our references
        // Resources with refcount > 1 survive, others are queued for destruction
        buffers_.clear();
        images_.clear();
        ended_ = true;
    }

    /**
     * @brief Check if this scope has ended
     */
    [[nodiscard]] bool HasEnded() const noexcept {
        return ended_;
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    [[nodiscard]] std::string_view GetName() const noexcept {
        return name_;
    }

    [[nodiscard]] LifetimeScope* GetParent() const noexcept {
        return parent_;
    }

    [[nodiscard]] size_t GetBufferCount() const noexcept {
        return buffers_.size();
    }

    [[nodiscard]] size_t GetImageCount() const noexcept {
        return images_.size();
    }

    [[nodiscard]] size_t GetTotalResourceCount() const noexcept {
        return buffers_.size() + images_.size();
    }

    /**
     * @brief Calculate total memory held by this scope
     */
    [[nodiscard]] VkDeviceSize GetTotalMemoryBytes() const noexcept {
        VkDeviceSize total = 0;
        for (const auto& buffer : buffers_) {
            if (buffer) {
                total += buffer->GetSize();
            }
        }
        for (const auto& image : images_) {
            if (image) {
                total += image->GetSize();
            }
        }
        return total;
    }

private:
    std::string name_;
    SharedResourceFactory* factory_ = nullptr;
    LifetimeScope* parent_ = nullptr;

    std::vector<SharedBufferPtr> buffers_;
    std::vector<SharedImagePtr> images_;

    bool ended_ = false;
};

/**
 * @brief Manages hierarchical lifetime scopes
 *
 * Provides frame-based and nested scope management for resource lifetimes.
 * Typical usage:
 * - Frame scope: Transient resources that live for one frame
 * - Pass scopes: Resources needed only during a specific render pass
 * - Custom scopes: User-defined lifetime groups
 *
 * Thread-safety: NOT thread-safe. Use one manager per thread.
 *
 * Usage:
 * ```cpp
 * LifetimeScopeManager manager(&factory);
 *
 * // Frame loop
 * while (running) {
 *     manager.BeginFrame();
 *
 *     auto& frameScope = manager.GetFrameScope();
 *     auto frameBuffer = frameScope.CreateBuffer(request);
 *
 *     // Nested pass scope
 *     auto& shadowScope = manager.BeginScope("ShadowPass");
 *     auto shadowMap = shadowScope.CreateImage(imageRequest);
 *     // ... render shadow pass ...
 *     manager.EndScope();  // shadowMap released
 *
 *     manager.EndFrame();  // frameBuffer released
 * }
 * ```
 */
class LifetimeScopeManager {
public:
    /**
     * @brief Create scope manager
     *
     * @param factory Factory for resource creation (required, must outlive manager)
     */
    explicit LifetimeScopeManager(SharedResourceFactory* factory)
        : factory_(factory)
        , frameScope_("Frame", factory)
    {
        assert(factory && "LifetimeScopeManager requires a SharedResourceFactory");
    }

    ~LifetimeScopeManager() {
        // End any active scopes
        while (!scopeStack_.empty()) {
            EndScope();
        }
        frameScope_.EndScope();
    }

    // Non-copyable
    LifetimeScopeManager(const LifetimeScopeManager&) = delete;
    LifetimeScopeManager& operator=(const LifetimeScopeManager&) = delete;

    // =========================================================================
    // Frame Lifecycle
    // =========================================================================

    /**
     * @brief Begin a new frame
     *
     * Resets the frame scope for fresh resource tracking.
     * Must be called before creating frame-scoped resources.
     */
    void BeginFrame() {
        assert(scopeStack_.empty() && "Cannot begin frame with active nested scopes");

        // End previous frame scope if any
        if (!frameScope_.HasEnded()) {
            frameScope_.EndScope();
        }

        // Create fresh frame scope
        frameScope_ = LifetimeScope("Frame", factory_);
        frameNumber_++;
    }

    /**
     * @brief End the current frame
     *
     * Ends all nested scopes and the frame scope, releasing resources.
     */
    void EndFrame() {
        // End any remaining nested scopes
        while (!scopeStack_.empty()) {
            EndScope();
        }

        // End frame scope
        frameScope_.EndScope();
    }

    /**
     * @brief Get the current frame scope
     *
     * Use for resources that should live for the entire frame.
     */
    [[nodiscard]] LifetimeScope& GetFrameScope() noexcept {
        return frameScope_;
    }

    [[nodiscard]] const LifetimeScope& GetFrameScope() const noexcept {
        return frameScope_;
    }

    // =========================================================================
    // Nested Scope Management
    // =========================================================================

    /**
     * @brief Begin a new nested scope
     *
     * Creates a child scope with the current scope as parent.
     * Resources in this scope are released when EndScope() is called.
     *
     * @param name Scope name for debugging
     * @return Reference to the new scope
     */
    LifetimeScope& BeginScope(std::string_view name) {
        LifetimeScope* parent = scopeStack_.empty() ? &frameScope_ : scopeStack_.top().get();
        auto scope = std::make_unique<LifetimeScope>(name, factory_, parent);
        scopeStack_.push(std::move(scope));
        return *scopeStack_.top();
    }

    /**
     * @brief End the current nested scope
     *
     * Releases all resources in the topmost nested scope.
     * The scope is removed from the stack.
     */
    void EndScope() {
        if (scopeStack_.empty()) {
            return;
        }

        scopeStack_.top()->EndScope();
        scopeStack_.pop();
    }

    /**
     * @brief Get the current active scope
     *
     * Returns the topmost nested scope, or frame scope if no nested scopes.
     */
    [[nodiscard]] LifetimeScope& CurrentScope() noexcept {
        if (scopeStack_.empty()) {
            return frameScope_;
        }
        return *scopeStack_.top();
    }

    [[nodiscard]] const LifetimeScope& CurrentScope() const noexcept {
        if (scopeStack_.empty()) {
            return frameScope_;
        }
        return *scopeStack_.top();
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    [[nodiscard]] uint64_t GetFrameNumber() const noexcept {
        return frameNumber_;
    }

    [[nodiscard]] size_t GetNestedScopeDepth() const noexcept {
        return scopeStack_.size();
    }

    [[nodiscard]] bool HasNestedScopes() const noexcept {
        return !scopeStack_.empty();
    }

    /**
     * @brief Get total resources across all active scopes
     */
    [[nodiscard]] size_t GetTotalResourceCount() const noexcept {
        size_t total = frameScope_.GetTotalResourceCount();

        // Note: Can't iterate std::stack directly, but this gives frame scope count
        // For detailed reporting, track separately or use different container
        return total;
    }

private:
    SharedResourceFactory* factory_;
    LifetimeScope frameScope_;
    std::stack<std::unique_ptr<LifetimeScope>> scopeStack_;
    uint64_t frameNumber_ = 0;
};

/**
 * @brief RAII helper for automatic scope management
 *
 * Ensures scope is ended when guard goes out of scope.
 *
 * Usage:
 * ```cpp
 * {
 *     ScopeGuard guard(manager, "ShadowPass");
 *     auto& scope = guard.GetScope();
 *     auto buffer = scope.CreateBuffer(request);
 *     // ... use buffer ...
 * }  // Scope automatically ended here
 * ```
 */
class ScopeGuard {
public:
    explicit ScopeGuard(LifetimeScopeManager& manager, std::string_view name)
        : manager_(manager)
        , scope_(manager.BeginScope(name))
    {}

    ~ScopeGuard() {
        manager_.EndScope();
    }

    // Non-copyable, non-movable
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    [[nodiscard]] LifetimeScope& GetScope() noexcept {
        return scope_;
    }

private:
    LifetimeScopeManager& manager_;
    LifetimeScope& scope_;
};

} // namespace ResourceManagement

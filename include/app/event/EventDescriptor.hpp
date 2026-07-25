#pragma once

#include <chrono>
#include <optional>
#include <variant>

#include "app/event/EventConfig.hpp"
#include "app/event/EventId.hpp"
#include "app/event/EventMetrics.hpp"
#include "app/event/EventValue.hpp"
#include "app/event/IEventDescriptor.hpp"
#include "app/event/IEventSender.hpp"
#include "app/event/SpinLock.hpp"

namespace app::event
{

// Owns timing/debounce logic for a single monitored condition.
// Does NOT know its receiver (that's IEventSender's job) and does NOT
// decide what "true" means for the underlying condition — callers of
// Trigger() own that decision.
template <typename TValue>
class EventDescriptor final : public IEventDescriptor
{
public:
    EventDescriptor(EventId id, EventConfig config, IEventSender<TValue>& sender, TValue initial) noexcept
        : id_(id), config_(config), sender_(&sender), image_(initial), pending_(initial)
    {
    }

    // Called from any thread when the underlying raw condition changes.
    void Trigger(EventValue value) noexcept override
    {
        std::lock_guard<SpinLock> lock(spin_);
        pending_ = std::get<TValue>(value);
        metrics_.RecordTrigger();

        const bool immediate = config_.mode == EventMode::interval && config_.delay == std::chrono::milliseconds{0};

        if (immediate)
        {
            FireIfChangedLocked();
            armedAt_ = lastNow_ + config_.interval;
            phase_ = Phase::Heartbeat;
            return;
        }

        // delay > 0 (oneShot or interval): every real trigger re-enters the
        // debounce phase and (re)arms the delay timer, even if we were
        // already in the Heartbeat phase.
        armedAt_ = lastNow_ + config_.delay;
        phase_ = Phase::Debounce;
    }

    // Called periodically by the supervisor thread.
    void Tick(std::chrono::milliseconds now) noexcept override
    {
        std::lock_guard<SpinLock> lock(spin_);
        lastNow_ = now;

        if (!armedAt_.has_value() || now < *armedAt_)
        {
            return;
        }

        if (config_.mode == EventMode::interval && phase_ == Phase::Heartbeat)
        {
            // Heartbeat: unconditional resend of the current image.
            // Protects the remote controller against losing state on its
            // own reset — it will get the current value again within one
            // interval, without needing a new physical trigger.
            sender_->Send(id_, image_);
            metrics_.RecordRaised();
            armedAt_ = now + config_.interval;
        }
        else
        {
            // Debounce phase (or oneShot): only send if the value actually
            // changed since the last sent image. This is what absorbs short
            // flapping that resolves back to the prior state within delay.
            FireIfChangedLocked();

            if (config_.mode == EventMode::interval)
            {
                armedAt_ = now + config_.interval;
                phase_ = Phase::Heartbeat;
            }
            else
            {
                armedAt_.reset();
            }
        }
    }

    // Sends the current image immediately, bypassing delay/debounce.
    void EmitSnapshot() noexcept override
    {
        std::lock_guard<SpinLock> lock(spin_);
        sender_->Send(id_, image_);
        metrics_.RecordRaised();
    }

    [[nodiscard]] EventId             Id() const noexcept override { return id_; }
    [[nodiscard]] TValue              Image() const noexcept { return image_; }
    [[nodiscard]] const EventMetrics& Metrics() const noexcept { return metrics_; }

private:
    // Debounce: waiting to see if a triggered change persists past delay.
    // Heartbeat: periodic unconditional resend of the current image
    // (interval mode only, entered after the first debounce cycle settles).
    enum class Phase
    {
        Debounce,
        Heartbeat
    };

    void FireIfChangedLocked() noexcept
    {
        if (pending_ != image_)
        {
            image_ = pending_;
            sender_->Send(id_, image_);
            metrics_.RecordRaised();
        }
        else
        {
            metrics_.RecordSuppressed();
        }
    }

    EventId                                  id_;
    EventConfig                              config_;
    IEventSender<TValue>*                    sender_;
    TValue                                   image_;
    TValue                                   pending_;
    std::optional<std::chrono::milliseconds> armedAt_;
    std::chrono::milliseconds                lastNow_{0};
    EventMetrics                             metrics_;
    mutable SpinLock                         spin_;
    Phase                                    phase_{Phase::Debounce};
};

} // namespace app::event

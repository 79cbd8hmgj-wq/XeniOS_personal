/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/metal/metal_gpu_completion_timeline.h"

#include <chrono>
#include <limits>

#include "third_party/metal-cpp/Foundation/NSString.hpp"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"

DEFINE_bool(metal_gpu_completion_telemetry, false,
            "Log timing summaries for CPU waits on the Metal GPU completion "
            "timeline.",
            "Metal");
DEFINE_int32(
    metal_gpu_completion_telemetry_interval, 120,
    "Number of blocking Metal GPU completion waits between timing summaries. "
    "Set to 0 to log only on shutdown.",
    "Metal");

namespace xe {
namespace ui {
namespace metal {

std::unique_ptr<MetalGPUCompletionTimeline> MetalGPUCompletionTimeline::Create(
    MTL::Device* device) {
  if (!device) {
    return nullptr;
  }
  MTL::SharedEvent* shared_event = device->newSharedEvent();
  if (shared_event) {
    shared_event->setLabel(
        NS::String::string("XeniaSubmissionTimeline", NS::UTF8StringEncoding));
  } else {
    XELOGW(
        "MetalGPUCompletionTimeline: SharedEvent unavailable; "
        "falling back to completion handlers");
  }
  return std::unique_ptr<MetalGPUCompletionTimeline>(
      new MetalGPUCompletionTimeline(shared_event));
}

MetalGPUCompletionTimeline::MetalGPUCompletionTimeline(
    MTL::SharedEvent* shared_event)
    : shared_event_(shared_event) {}

MetalGPUCompletionTimeline::~MetalGPUCompletionTimeline() {
  const uint64_t last_submission = GetUpcomingSubmission() - 1;
  if (last_submission) {
    if (shared_event_) {
      shared_event_->waitUntilSignaledValue(
          last_submission, std::numeric_limits<uint64_t>::max());
    } else {
      std::unique_lock<std::mutex> lock(fallback_mutex_);
      fallback_cv_.wait(lock, [this, last_submission] {
        return completed_fallback_.load(std::memory_order_acquire) >=
               last_submission;
      });
    }
  }
  if (shared_event_) {
    shared_event_->release();
    shared_event_ = nullptr;
  }
  MaybeDumpTelemetry(true);
}

bool MetalGPUCompletionTimeline::SignalAndAdvance(
    MTL::CommandBuffer* command_buffer) {
  if (!command_buffer) {
    return false;
  }
  const uint64_t submission = GetUpcomingSubmission();
  if (shared_event_) {
    command_buffer->encodeSignalEvent(shared_event_, submission);
  } else {
    command_buffer->addCompletedHandler([this,
                                         submission](MTL::CommandBuffer*) {
      uint64_t previous = completed_fallback_.load(std::memory_order_relaxed);
      while (submission > previous &&
             !completed_fallback_.compare_exchange_weak(
                 previous, submission, std::memory_order_release,
                 std::memory_order_relaxed)) {
      }
      fallback_cv_.notify_all();
    });
  }
  IncrementUpcomingSubmission();
  return true;
}

void MetalGPUCompletionTimeline::UpdateCompletedSubmission() {
  uint64_t completed = 0;
  if (shared_event_) {
    completed = shared_event_->signaledValue();
  } else {
    completed = completed_fallback_.load(std::memory_order_acquire);
  }
  const uint64_t max_completed = GetUpcomingSubmission() - 1;
  if (completed > max_completed) {
    completed = max_completed;
  }
  if (completed > GetCompletedSubmissionFromLastUpdate()) {
    SetCompletedSubmission(completed);
  }
}

void MetalGPUCompletionTimeline::AwaitSubmissionImpl(
    uint64_t awaited_submission) {
  const bool collect_telemetry = ::cvars::metal_gpu_completion_telemetry;
  std::chrono::steady_clock::time_point wait_start;
  if (collect_telemetry) {
    wait_start = std::chrono::steady_clock::now();
  }

  if (shared_event_) {
    shared_event_->waitUntilSignaledValue(awaited_submission,
                                          std::numeric_limits<uint64_t>::max());
  } else {
    std::unique_lock<std::mutex> lock(fallback_mutex_);
    fallback_cv_.wait(lock, [this, awaited_submission] {
      return completed_fallback_.load(std::memory_order_acquire) >=
             awaited_submission;
    });
  }

  if (collect_telemetry) {
    const auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - wait_start)
                             .count();
    RecordAwaitSubmissionWait(wait_us > 0 ? static_cast<uint64_t>(wait_us)
                                          : uint64_t(0));
    MaybeDumpTelemetry(false);
  }
}

void MetalGPUCompletionTimeline::RecordAwaitSubmissionWait(uint64_t wait_us) {
  std::lock_guard<std::mutex> lock(telemetry_mutex_);
  await_submission_wait_us_.Add(wait_us);
}

void MetalGPUCompletionTimeline::MaybeDumpTelemetry(bool force) {
  if (!::cvars::metal_gpu_completion_telemetry) {
    return;
  }

  MetalTelemetryAccumulator snapshot;
  {
    std::lock_guard<std::mutex> lock(telemetry_mutex_);
    const int32_t interval_config =
        ::cvars::metal_gpu_completion_telemetry_interval;
    const uint64_t interval =
        interval_config > 0 ? static_cast<uint64_t>(interval_config) : 0;
    if (await_submission_wait_us_.empty() ||
        (!force && (!interval || await_submission_wait_us_.count < interval))) {
      return;
    }
    snapshot = await_submission_wait_us_;
    await_submission_wait_us_.Reset();
  }

  XELOGI(
      "Metal GPU completion waits: count={} total_us={} avg_us={} min_us={} "
      "max_us={}",
      snapshot.count, snapshot.total,
      snapshot.count ? snapshot.total / snapshot.count : uint64_t(0),
      snapshot.min, snapshot.max);
}

}  // namespace metal
}  // namespace ui
}  // namespace xe

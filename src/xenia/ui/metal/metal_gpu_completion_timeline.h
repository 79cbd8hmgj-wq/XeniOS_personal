/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_METAL_METAL_GPU_COMPLETION_TIMELINE_H_
#define XENIA_UI_METAL_METAL_GPU_COMPLETION_TIMELINE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

#include "third_party/metal-cpp/Metal/Metal.hpp"
#include "xenia/ui/gpu_completion_timeline.h"
#include "xenia/ui/metal/metal_telemetry.h"

namespace xe {
namespace ui {
namespace metal {

class MetalGPUCompletionTimeline : public GPUCompletionTimeline {
 public:
  static std::unique_ptr<MetalGPUCompletionTimeline> Create(
      MTL::Device* device);

  MetalGPUCompletionTimeline(const MetalGPUCompletionTimeline&) = delete;
  MetalGPUCompletionTimeline& operator=(const MetalGPUCompletionTimeline&) =
      delete;
  MetalGPUCompletionTimeline(MetalGPUCompletionTimeline&&) = delete;
  MetalGPUCompletionTimeline& operator=(MetalGPUCompletionTimeline&&) = delete;

  ~MetalGPUCompletionTimeline();

  MTL::SharedEvent* shared_event() const { return shared_event_; }

  // Returns whether signaling was scheduled successfully.
  bool SignalAndAdvance(MTL::CommandBuffer* command_buffer);

  void UpdateCompletedSubmission() override;

 protected:
  void AwaitSubmissionImpl(uint64_t awaited_submission) override;

 private:
  explicit MetalGPUCompletionTimeline(MTL::SharedEvent* shared_event);

  void RecordAwaitSubmissionWait(uint64_t wait_us);
  void MaybeDumpTelemetry(bool force);

  MTL::SharedEvent* shared_event_ = nullptr;

  std::atomic<uint64_t> completed_fallback_{0};
  std::mutex fallback_mutex_;
  std::condition_variable fallback_cv_;

  std::mutex telemetry_mutex_;
  MetalTelemetryAccumulator await_submission_wait_us_;
};

}  // namespace metal
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_METAL_METAL_GPU_COMPLETION_TIMELINE_H_

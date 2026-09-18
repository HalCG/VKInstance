#ifndef PERF_STATS_HPP
#define PERF_STATS_HPP

// =============================================================================
// PerfStats — CPU 侧各渲染阶段耗时（std::chrono）
// =============================================================================
//
// 【计时范围】
//   beginFrame() 在 vkBeginCommandBuffer 之后、Renderer::render 之前
//   endFrame()   在 vkEndCommandBuffer 之前
//   因此 totalFrameMs ≈ 录制命令缓冲 + UBO memcpy，不含 vkQueueSubmit 后 GPU 执行时间
//
// 【各 Renderer 写入的字段】
//   Forward:      forwardPassMs
//   Deferred:     geometryPassMs + lightingPassMs
//   Forward+:     cullPassMs + shadingPassMs
//
// 【与 GPU Timestamp Query 的区别】
//   本 Demo 未启用 VK_QUERY_TYPE_TIMESTAMP；控制台数字反映 CPU 录制开销为主。
//   灯数增多时 Forward 的 forwardPassMs 仍会因驱动/验证层而上升，但不如 GPU 片元负载直观。
//
// =============================================================================

#include "RenderTypes.hpp"

#include <chrono>

class CpuTimer {
public:
    void begin() { start_ = std::chrono::steady_clock::now(); }
    float endMs() {
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<float, std::milli>(end - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

class PerfStats {
public:
    void beginFrame() { frameStart_ = std::chrono::steady_clock::now(); }

    void endFrame() {
        const auto end = std::chrono::steady_clock::now();
        const float ms = std::chrono::duration<float, std::milli>(end - frameStart_).count();
        latest_.totalFrameMs = ms;
        latest_.fps = ms > 0.0f ? 1000.0f / ms : 0.0f;
    }

    FrameStats &frameStats() { return latest_; }
    const FrameStats &latest() const { return latest_; }

    CpuTimer forwardPass;
    CpuTimer geometryPass;
    CpuTimer lightingPass;
    CpuTimer cullPass;
    CpuTimer shadingPass;

private:
    FrameStats latest_;
    std::chrono::steady_clock::time_point frameStart_;
};

#endif

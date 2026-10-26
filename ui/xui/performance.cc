/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include "debug.hh"

extern "C" {
#include "hw/xbox/performance.h"
}

bool performance_overlay_open;

static void DrawOsdText(ImDrawList *draw, ImFont *font, float size, ImVec2 pos,
                        ImU32 color, const char *text)
{
    float shadow = size / 13.0f;
    draw->AddText(font, size, ImVec2(pos.x + shadow, pos.y + shadow),
                  IM_COL32(0, 0, 0, 255), text);
    draw->AddText(font, size, pos, color, text);
}

void DrawPerformanceOverlay()
{
    xemu_perf_set_enabled(performance_overlay_open);
    if (!performance_overlay_open) {
        return;
    }
    XemuPerfResult stats = xemu_perf_sample();
    bool running = stats.current.generation != 0;
    enum XemuPerfRateBand band = xemu_perf_audio_rate_band(&stats);
    const ImU32 neutral = IM_COL32(210, 210, 210, 255);
    ImU32 rate_color = neutral;
    switch (band) {
    case XEMU_PERF_RATE_SLOW:
        rate_color = IM_COL32(255, 0, 0, 255);
        break;
    case XEMU_PERF_RATE_NORMAL:
        rate_color = IM_COL32(255, 255, 255, 255);
        break;
    case XEMU_PERF_RATE_FAST:
        rate_color = IM_COL32(0, 255, 0, 255);
        break;
    default:
        break;
    }

    const char *labels[] = { "CPU speed", "NV2A speed", "APU @48kHz" };
    char rates[XEMU_PERF_THREADS][32] = { "N/D", "N/D", "N/D" };
    char host[XEMU_PERF_THREADS][48];
    if (band != XEMU_PERF_RATE_UNAVAILABLE) {
        snprintf(rates[XEMU_PERF_APU], sizeof(rates[0]), "%.1f%%",
                 stats.audio_percent);
    }
    for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
        if (running && stats.ready && stats.cpu_valid[i]) {
            snprintf(host[i], sizeof(host[i]), "Host %.1f%%",
                     stats.cpu_percent[i]);
        } else {
            snprintf(host[i], sizeof(host[i]), "Host N/D");
        }
    }
    const char *status = "APU cadence only";
    if (!running) {
        status = "Paused";
    } else if (!stats.ready) {
        status = "Collecting...";
    } else if (!stats.current.audio_active) {
        status = "APU inactive";
    } else if (stats.audio_stale) {
        status = "APU data stale";
    } else if (!stats.current.gp_enabled || !stats.current.ep_enabled ||
               !stats.current.gp_realtime || !stats.current.ep_realtime) {
        status = "APU DSP limited/off";
    } else if (!stats.audio_valid) {
        status = "APU collecting...";
    }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImFont *font = ImGui::GetFont();
    float size = ImGui::GetFontSize();
    float label_width = 0, rate_width = 0, host_width = 0;
    for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
        label_width = std::max(label_width, ImGui::CalcTextSize(labels[i]).x);
        rate_width = std::max(rate_width, ImGui::CalcTextSize(rates[i]).x);
        host_width = std::max(host_width, ImGui::CalcTextSize(host[i]).x);
    }
    float width = std::max(label_width + rate_width + host_width + 2 * size,
                           ImGui::CalcTextSize(status).x);
    float margin = std::min(size, viewport->Size.x / 8.0f);
    if (viewport->Size.x <= 0 || viewport->Size.y <= 0) {
        return;
    }
    float scale = std::min(1.0f, (viewport->Size.x - 2 * margin) / width);
    ImVec2 pos(viewport->Pos.x + viewport->Size.x - margin - width * scale,
               viewport->Pos.y + ImGui::GetFrameHeight() + size);
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
        DrawOsdText(draw, font, size * scale, pos, neutral, labels[i]);
        DrawOsdText(draw, font, size * scale,
                    ImVec2(pos.x + (label_width + size) * scale, pos.y),
                    i == XEMU_PERF_APU ? rate_color : neutral, rates[i]);
        DrawOsdText(draw, font, size * scale,
                    ImVec2(pos.x + (label_width + rate_width + 2 * size) * scale,
                           pos.y), neutral, host[i]);
        pos.y += (size + 3) * scale;
    }
    DrawOsdText(draw, font, size * scale, pos, neutral, status);
}

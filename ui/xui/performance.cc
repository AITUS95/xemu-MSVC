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
    const ImU32 neutral = IM_COL32(210, 210, 210, 255);
    ImU32 rate_colors[XEMU_PERF_THREADS] = { neutral, neutral, neutral };
    const char *labels[] = { "CPU speed", "NV2A speed", "APU @48kHz" };
    char rates[XEMU_PERF_THREADS][32] = { "N/D", "N/D", "N/D" };
    char host[XEMU_PERF_THREADS][48];
    for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
        enum XemuPerfRateBand band =
            xemu_perf_speed_band(&stats, (enum XemuPerfThread)i);
        if (band != XEMU_PERF_RATE_UNAVAILABLE) {
            snprintf(rates[i], sizeof(rates[i]), "%.1f%%",
                     stats.speed_percent[i]);
            rate_colors[i] = band == XEMU_PERF_RATE_SLOW ?
                IM_COL32(255, 0, 0, 255) : band == XEMU_PERF_RATE_FAST ?
                IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 255, 255);
        }
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

    char work[96] = "PFIFO N/D | flips N/D";
    char stalls[96] = "Stalls/s F/N/C/A: N/D";
    if (running && stats.nv2a_valid) {
        snprintf(work, sizeof(work), "PFIFO %.2f Mword/s | flips %.1f/s",
                 stats.nv2a_words_per_second / 1000000.0,
                 stats.nv2a_flips_per_second);
        snprintf(stalls, sizeof(stalls),
                 "Stalls/s F/N/C/A: %.1f/%.1f/%.1f/%.1f",
                 stats.nv2a_stalls_per_second[XEMU_PERF_STALL_FLIP],
                 stats.nv2a_stalls_per_second[XEMU_PERF_STALL_NOP],
                 stats.nv2a_stalls_per_second[XEMU_PERF_STALL_CONTEXT],
                 stats.nv2a_stalls_per_second[XEMU_PERF_STALL_FIFO]);
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
    width = std::max(width, ImGui::CalcTextSize(work).x);
    width = std::max(width, ImGui::CalcTextSize(stalls).x);
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
                    rate_colors[i], rates[i]);
        DrawOsdText(draw, font, size * scale,
                    ImVec2(pos.x + (label_width + rate_width + 2 * size) * scale,
                           pos.y), neutral, host[i]);
        pos.y += (size + 3) * scale;
    }
    DrawOsdText(draw, font, size * scale, pos, neutral, work);
    pos.y += (size + 3) * scale;
    DrawOsdText(draw, font, size * scale, pos, neutral, stalls);
    pos.y += (size + 3) * scale;
    DrawOsdText(draw, font, size * scale, pos, neutral, status);
}

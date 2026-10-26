/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

/* Rate bands are reused for visual states; trend/pressure is not guest speed. */
static ImU32 RowTextColor(enum XemuPerfRateBand band)
{
    switch (band) {
    case XEMU_PERF_RATE_SLOW: return IM_COL32(255, 112, 112, 255);
    case XEMU_PERF_RATE_FAST: return IM_COL32(128, 255, 159, 255);
    default: return IM_COL32(210, 210, 210, 255);
    }
}

static void DrawRowGlow(ImDrawList *draw, ImVec2 pos, float width,
                        float height, float scale, enum XemuPerfRateBand band)
{
    if (band != XEMU_PERF_RATE_SLOW && band != XEMU_PERF_RATE_FAST) {
        return;
    }
    bool red = band == XEMU_PERF_RATE_SLOW;
    float pulse = 0.88f + 0.12f * (0.5f + 0.5f *
                                  std::sin((float)ImGui::GetTime() * 3.0f));
    int r = red ? 255 : 39;
    int g = red ? 55 : 205;
    int b = red ? 55 : 88;
    float radius = 4.0f * scale;

    /* Two translucent passes illuminate the complete line without a window. */
    draw->AddRectFilled(
        ImVec2(pos.x - 5.0f * scale, pos.y - 2.0f * scale),
        ImVec2(pos.x + width + 5.0f * scale,
               pos.y + height + 2.0f * scale),
        IM_COL32(r, g, b, (int)(24.0f * pulse)), radius + scale);
    draw->AddRectFilled(
        ImVec2(pos.x - 2.0f * scale, pos.y),
        ImVec2(pos.x + width + 2.0f * scale, pos.y + height),
        IM_COL32(r, g, b, (int)(43.0f * pulse)), radius);
}

void DrawPerformanceOverlay()
{
    xemu_perf_set_enabled(performance_overlay_open);
    if (!performance_overlay_open) {
        return;
    }
    XemuPerfResult stats = xemu_perf_sample();
    bool running = stats.current.generation != 0;

    const char *labels[] = { "CPU speed", "NV2A speed", "APU @48kHz" };
    char rates[XEMU_PERF_THREADS][32] = { "N/D", "N/D", "N/D" };
    char host[XEMU_PERF_THREADS][48];
    enum XemuPerfRateBand row_bands[XEMU_PERF_THREADS];
    bool host_fallback = false;

    for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
        enum XemuPerfRateBand speed_band =
            xemu_perf_speed_band(&stats, (enum XemuPerfThread)i);
        row_bands[i] = speed_band;
        if (speed_band != XEMU_PERF_RATE_UNAVAILABLE) {
            snprintf(rates[i], sizeof(rates[i]), "%.1f%%",
                     stats.speed_percent[i]);
            /* A validated rate at or above 95% glows green. */
            if (row_bands[i] == XEMU_PERF_RATE_NORMAL) {
                row_bands[i] = XEMU_PERF_RATE_FAST;
            }
        } else if (i != XEMU_PERF_APU) {
            /* A fallback visual cue, *only* for host thread pressure. */
            row_bands[i] = xemu_perf_host_pressure_band(
                &stats, (enum XemuPerfThread)i);
            host_fallback |= row_bands[i] != XEMU_PERF_RATE_UNAVAILABLE;
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

    char pfifo[64] = "PFIFO N/D";
    char flips[64] = "Flips N/D";
    char stalls[96] = "Stalls/s F/N/C/A: N/D";
    if (running && stats.nv2a_valid) {
        snprintf(pfifo, sizeof(pfifo), "PFIFO %.2f Mword/s",
                 stats.nv2a_words_per_second / 1000000.0);
        snprintf(flips, sizeof(flips), "Flips %.1f/s",
                 stats.nv2a_flips_per_second);
        snprintf(stalls, sizeof(stalls),
                 "Stalls/s F/N/C/A: %.1f/%.1f/%.1f/%.1f",
                 stats.nv2a_stalls_per_second[XEMU_PERF_STALL_FLIP],
                 stats.nv2a_stalls_per_second[XEMU_PERF_STALL_NOP],
                 stats.nv2a_stalls_per_second[XEMU_PERF_STALL_CONTEXT],
                 stats.nv2a_stalls_per_second[XEMU_PERF_STALL_FIFO]);
    }

    /* Only update the rolling reference on a newly collected 250ms sample. */
    static unsigned last_generation;
    static int64_t last_sample_time;
    static XemuPerfTrend trends[XEMU_PERF_TREND_COUNT];
    static enum XemuPerfRateBand trend_bands[XEMU_PERF_TREND_COUNT];
    if (last_generation != stats.current.generation || !stats.ready) {
        memset(trends, 0, sizeof(trends));
        for (unsigned i = 0; i < XEMU_PERF_TREND_COUNT; i++) {
            trend_bands[i] = XEMU_PERF_RATE_UNAVAILABLE;
        }
        last_generation = stats.current.generation;
        last_sample_time = 0;
    }
    if (running && stats.ready &&
        stats.current.time_us != last_sample_time) {
        last_sample_time = stats.current.time_us;
        bool valid = stats.nv2a_valid;
        double total_stalls = 0.0;
        for (unsigned i = 0; i < XEMU_PERF_STALL_COUNT; i++) {
            total_stalls += stats.nv2a_stalls_per_second[i];
        }
        const double values[] = { stats.nv2a_words_per_second,
                                  stats.nv2a_flips_per_second, total_stalls };
        for (unsigned i = 0; i < XEMU_PERF_TREND_COUNT; i++) {
            trend_bands[i] = xemu_perf_trend_band(
                &trends[i], (enum XemuPerfTrendKind)i, valid, values[i]);
        }
    }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (viewport->Size.x <= 0 || viewport->Size.y <= 0) {
        return;
    }
    ImFont *font = ImGui::GetFont();
    float size = ImGui::GetFontSize();
    float label_width = 0, rate_width = 0, host_width = 0;
    for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
        label_width = std::max(label_width, ImGui::CalcTextSize(labels[i]).x);
        rate_width = std::max(rate_width, ImGui::CalcTextSize(rates[i]).x);
        host_width = std::max(host_width, ImGui::CalcTextSize(host[i]).x);
    }
    const char *hint = host_fallback ?
        "CPU/NV2A glow: Host load only (speed N/D)" : nullptr;
    float width = std::max(label_width + rate_width + host_width + 2 * size,
                           ImGui::CalcTextSize(status).x);
    width = std::max(width, ImGui::CalcTextSize(pfifo).x);
    width = std::max(width, ImGui::CalcTextSize(flips).x);
    width = std::max(width, ImGui::CalcTextSize(stalls).x);
    if (hint) {
        width = std::max(width, ImGui::CalcTextSize(hint).x);
    }

    float margin = std::min(size, viewport->Size.x / 8.0f);
    float scale = std::min(1.0f, (viewport->Size.x - 2 * margin) / width);
    ImVec2 pos(viewport->Pos.x + viewport->Size.x - margin - width * scale,
               viewport->Pos.y + ImGui::GetFrameHeight() + size);
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const float row_height = (size + 3.0f) * scale;

    for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
        enum XemuPerfRateBand band = row_bands[i];
        ImU32 color = RowTextColor(band);
        DrawRowGlow(draw, pos, width * scale, row_height, scale, band);
        DrawOsdText(draw, font, size * scale, pos, color, labels[i]);
        DrawOsdText(draw, font, size * scale,
                    ImVec2(pos.x + (label_width + size) * scale, pos.y),
                    color, rates[i]);
        DrawOsdText(draw, font, size * scale,
                    ImVec2(pos.x + (label_width + rate_width + 2 * size) * scale,
                           pos.y), color, host[i]);
        pos.y += row_height;
    }
    const char *lines[] = { pfifo, flips, stalls };
    for (unsigned i = 0; i < XEMU_PERF_TREND_COUNT; i++) {
        enum XemuPerfRateBand band = trend_bands[i];
        DrawRowGlow(draw, pos, width * scale, row_height, scale, band);
        DrawOsdText(draw, font, size * scale, pos, RowTextColor(band), lines[i]);
        pos.y += row_height;
    }
    DrawOsdText(draw, font, size * scale, pos,
                IM_COL32(210, 210, 210, 255), status);
    pos.y += row_height;
    if (hint) {
        DrawOsdText(draw, font, size * scale, pos,
                    IM_COL32(210, 210, 210, 255), hint);
    }
}

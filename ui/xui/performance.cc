/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "common.hh"
#include "debug.hh"
#include "viewport-manager.hh"

extern "C" {
#include "hw/xbox/performance.h"
}

bool performance_overlay_open;

static void PercentageRow(const char *name, bool valid, double value,
                          const char *unavailable)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(name);
    ImGui::TableNextColumn();
    if (valid) {
        ImGui::Text("%.1f%%", value);
    } else {
        ImGui::TextDisabled("%s", unavailable);
    }
}

void DrawPerformanceOverlay()
{
    xemu_perf_set_enabled(performance_overlay_open);
    if (!performance_overlay_open) {
        return;
    }
    XemuPerfResult stats = xemu_perf_sample();
    bool running = runstate_is_running();
    const char *unavailable = !running     ? "Paused" :
                              !stats.ready ? "Collecting..." :
                                             "N/A";
    float scale = g_viewport_mgr.m_scale;
    ImGui::SetNextWindowPos(ImVec2(15 * scale, 50 * scale),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("Component performance", &performance_overlay_open,
                     ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted("Host CPU time / wall time (100% = one core)");
        if (ImGui::BeginTable("Host threads", 2)) {
            static const char *names[] = { "CPU", "NV2A worker", "APU worker" };
            for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
                PercentageRow(names[i], running && stats.cpu_valid[i],
                              stats.cpu_percent[i], unavailable);
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        const char *audio_status = unavailable;
        if (running && stats.ready) {
            audio_status = !stats.current.audio_active ? "Inactive" :
                           stats.audio_stale ? "Stale / no completions" :
                                               "Collecting...";
        }
        bool audio_valid = running && stats.audio_valid;
        if (ImGui::BeginTable("Audio pipeline", 2)) {
            PercentageRow("Audio cadence (48 kHz = 100%)", audio_valid,
                          stats.audio_percent, audio_status);
            PercentageRow("Audio budget used", audio_valid,
                          stats.audio_budget_percent, audio_status);
            ImGui::EndTable();
        }
        ImGui::TextUnformatted("Audio phase time / wall time (includes waits)");
        if (ImGui::BeginTable("Audio phases", 2)) {
            PercentageRow("VP", audio_valid, stats.phase_percent[XEMU_PERF_VP],
                          audio_status);
            PercentageRow("GP", audio_valid && stats.current.gp_enabled,
                          stats.phase_percent[XEMU_PERF_GP],
                          !running || !stats.ready  ? unavailable :
                          !stats.current.gp_enabled ? "Disabled" :
                                                      audio_status);
            PercentageRow("EP", audio_valid && stats.current.ep_enabled,
                          stats.phase_percent[XEMU_PERF_EP],
                          !running || !stats.ready  ? unavailable :
                          !stats.current.ep_enabled ? "Disabled" :
                                                      audio_status);
            ImGui::EndTable();
        }
        if (running && stats.ready && stats.current.audio_active) {
            ImGui::Text("DSP mode: GP %s, EP %s",
                        !stats.current.gp_enabled ? "off" :
                        stats.current.gp_realtime ? "realtime" :
                                                    "limited",
                        !stats.current.ep_enabled ? "off" :
                        stats.current.ep_realtime ? "realtime" :
                                                    "limited");
        }
        ImGui::Separator();
        ImGui::TextDisabled("GPU hardware / HDD / DVD / network / USB: N/A");
        ImGui::TextDisabled(
            "Host load and audio cadence are not Xbox accuracy.");
        ImGui::TextDisabled("Do not add thread and nested phase percentages.");
    }
    ImGui::End();
    xemu_perf_set_enabled(performance_overlay_open);
}

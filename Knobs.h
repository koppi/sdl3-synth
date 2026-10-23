#pragma once

#include "imgui.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include <functional>
#include <algorithm>

struct KnobParam {
    enum Type {
        DIRECT,
        VOICE_FREQ,
        VOICE_AMP,
        VOICE_MIX,
        VCO_MIX,
        VCO_PITCH,
        VCO_DETUNE,
        VCO_PHASE,
        VCO_PW,
        VCO_PAN,
        ATTACK,
        DECAY,
        SUSTAIN,
        RELEASE
    };

    float* value;
    float minVal;
    float maxVal;
    int voiceIndex;
    int vcoIndex;
    Type type;
    int midiCC;
    int parameterIndex; // index in SynthEngine::parameters, -1 when not automatable
    std::function<void(float)> applyCallback;
    bool dragging = false; // editor-only drag state, used to bracket host edits

    KnobParam()
        : value(nullptr), minVal(0), maxVal(1), voiceIndex(-1), vcoIndex(-1),
          type(DIRECT), midiCC(-1), parameterIndex(-1) {}

    KnobParam(float* v, float mn, float mx, int vi, int vci, Type t, int cc,
              int paramIndex = -1)
        : value(v), minVal(mn), maxVal(mx), voiceIndex(vi), vcoIndex(vci),
          type(t), midiCC(cc), parameterIndex(paramIndex) {}
};

// Per-engine knob state. Keeping this out of a global makes multiple plug-in
// instances (and the standalone app) independent of each other.
struct KnobContext {
    std::vector<KnobParam> params;
    bool midiLearnMode = false;
    int midiLearnKnobIdx = -1;
    std::function<void(int)> onParameterChanged;

    // Host automation bridges. The standalone application leaves these empty.
    // onEditBegin/onEditEnd bracket a user drag so the host can record it as a
    // single edit gesture; onGuiParameterChanged reports new values produced by
    // the editor itself (never by host automation or MIDI CC) so they can be
    // written back to the host.
    std::function<void(int)> onEditBegin;
    std::function<void(int)> onEditEnd;
    std::function<void(int)> onGuiParameterChanged;

    void applyMidiCCValue(int cc, int value) {
        for (auto& p : params) {
            if (p.midiCC == cc) {
                float normalized = value / 127.0f;
                *p.value = p.minVal + normalized * (p.maxVal - p.minVal);
                if (p.applyCallback) p.applyCallback(*p.value);
                if (onParameterChanged && p.parameterIndex >= 0)
                    onParameterChanged(p.parameterIndex);
            }
        }
    }

    void notifyChanged(const KnobParam& p) {
        if (p.applyCallback) p.applyCallback(*p.value);
        if (onParameterChanged && p.parameterIndex >= 0)
            onParameterChanged(p.parameterIndex);
        if (onGuiParameterChanged && p.parameterIndex >= 0)
            onGuiParameterChanged(p.parameterIndex);
    }
};

inline bool DrawKnob(KnobContext& context, const char* label, KnobParam& param) {
    float radius = 20.0f;
    ImVec2 totalSize(radius * 2, radius * 2 + 24);
    ImVec2 pos = ImGui::GetCursorScreenPos();

    char hiddenId[64];
    snprintf(hiddenId, sizeof(hiddenId), "##knob_%s", label);
    ImGui::InvisibleButton(hiddenId, totalSize);

    bool changed = false;
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    // Report a drag gesture to the host as a single automation edit. Only the
    // editor path can reach here; host automation never goes through DrawKnob.
    if (active && !param.dragging && param.parameterIndex >= 0 && context.onEditBegin)
        context.onEditBegin(param.parameterIndex);
    if (!active && param.dragging && param.parameterIndex >= 0 && context.onEditEnd)
        context.onEditEnd(param.parameterIndex);
    param.dragging = active;

    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    // MIDI learn: on click, select this knob
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (context.midiLearnMode) {
            for (int i = 0; i < (int)context.params.size(); ++i) {
                if (&context.params[i] == &param) {
                    context.midiLearnKnobIdx = i;
                    break;
                }
            }
        }
    }

    // Drag to adjust value
    if (active && !context.midiLearnMode) {
        float delta = ImGui::GetIO().MouseDelta.y;
        if (delta != 0.0f) {
            float range = param.maxVal - param.minVal;
            float sensitivity = ImGui::GetIO().KeyShift ? 3000.0f : 300.0f;
            float v = *param.value - delta * range / sensitivity;
            *param.value = v < param.minVal ? param.minVal : (v > param.maxVal ? param.maxVal : v);
            changed = true;
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 center(pos.x + radius, pos.y + radius);

    float startAngle = 0.75f * 3.14159f;
    float endAngle = 2.25f * 3.14159f;
    float valueAngle;
    if (param.maxVal > param.minVal)
        valueAngle = startAngle + ((*param.value - param.minVal) / (param.maxVal - param.minVal)) * (endAngle - startAngle);
    else
        valueAngle = (startAngle + endAngle) * 0.5f;

    // Background arc
    int segments = 40;
    ImVec2 bgPoints[41];
    for (int i = 0; i <= segments; ++i) {
        float t = (float)i / (float)segments;
        float angle = startAngle + t * (endAngle - startAngle);
        bgPoints[i] = ImVec2(center.x + cosf(angle) * radius, center.y + sinf(angle) * radius);
    }
    drawList->AddPolyline(bgPoints, segments + 1, IM_COL32(60, 60, 60, 255), 0, 3.0f);

    // Value arc (only if MIDI CC assigned or learn target)
    if (param.midiCC >= 0) {
        int valueSegments = (int)((valueAngle - startAngle) / (endAngle - startAngle) * segments + 0.5f);
        if (valueSegments < 0) valueSegments = 0;
        if (valueSegments > segments) valueSegments = segments;
        ImVec2 valuePoints[41];
        for (int i = 0; i <= valueSegments; ++i) {
            float t = (float)i / (float)segments;
            float angle = startAngle + t * (endAngle - startAngle);
            valuePoints[i] = ImVec2(center.x + cosf(angle) * radius, center.y + sinf(angle) * radius);
        }
        ImU32 arcColor = (context.midiLearnMode && context.midiLearnKnobIdx >= 0 &&
                          &param == &context.params[context.midiLearnKnobIdx])
                         ? IM_COL32(255, 200, 0, 255)
                         : IM_COL32(80, 180, 255, 255);
        drawList->AddPolyline(valuePoints, valueSegments + 1, arcColor, 0, 3.0f);
    } else if (context.midiLearnMode && context.midiLearnKnobIdx >= 0 &&
               &param == &context.params[context.midiLearnKnobIdx]) {
        // Learn target highlight (no CC yet)
        int valueSegments = (int)((valueAngle - startAngle) / (endAngle - startAngle) * segments + 0.5f);
        if (valueSegments < 0) valueSegments = 0;
        if (valueSegments > segments) valueSegments = segments;
        ImVec2 valuePoints[41];
        for (int i = 0; i <= valueSegments; ++i) {
            float t = (float)i / (float)segments;
            float angle = startAngle + t * (endAngle - startAngle);
            valuePoints[i] = ImVec2(center.x + cosf(angle) * radius, center.y + sinf(angle) * radius);
        }
        drawList->AddPolyline(valuePoints, valueSegments + 1, IM_COL32(255, 200, 0, 255), 0, 3.0f);
    }

    // Pointer line
    float pointerLen = radius * 0.7f;
    ImVec2 pointerEnd(center.x + cosf(valueAngle) * pointerLen,
                      center.y + sinf(valueAngle) * pointerLen);
    drawList->AddLine(center, pointerEnd, IM_COL32(255, 255, 255, 200), 2.0f);
    drawList->AddCircleFilled(center, 3.0f, IM_COL32(255, 255, 255, 200));

    // Label text
    ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, pos.y + radius * 2 + 2),
                      IM_COL32(200, 200, 200, 255), label);

    // Value text
    char valueText[32];
    float displayVal = *param.value;
    if (param.maxVal - param.minVal > 100.0f)
        snprintf(valueText, sizeof(valueText), "%.0f", displayVal);
    else if (param.maxVal - param.minVal > 10.0f)
        snprintf(valueText, sizeof(valueText), "%.1f", displayVal);
    else
        snprintf(valueText, sizeof(valueText), "%.2f", displayVal);

    ImVec2 valSize = ImGui::CalcTextSize(valueText);
    ImVec2 valPos(center.x - valSize.x * 0.5f, center.y - valSize.y * 0.5f);
    drawList->AddRectFilled(ImVec2(valPos.x - 4, valPos.y - 2),
                            ImVec2(valPos.x + valSize.x + 4, valPos.y + valSize.y + 2),
                            IM_COL32(30, 30, 30, 230));
    drawList->AddText(valPos, IM_COL32(255, 255, 255, 220), valueText);

    if (changed) context.notifyChanged(param);
    return changed;
}

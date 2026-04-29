#include "PresetsPanel.h"

#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "AudioEngine.h"
#include "ControlsPanel.h"
#include "anatomy/VocalTract.h"
#include "glottis/Glottis.h"
#include "imgui.h"

namespace live {

namespace {

// Splits a speaker shape name into a group key + a short button label
// suitable for tight per-button real estate.
//
//   "a"                            → ("vowels",                  "a")
//   "6_low"                        → ("vowels",                  "6_low")
//   "i-raw"                        → ("vowels-raw",              "i")
//   "tt-alveolar-fricative(a)"     → ("tt-alveolar-fricative",   "a")
//   "tt-alveolar-fricative(a)-raw" → ("tt-alveolar-fricative-raw","a")
//   "ll-labial-closure(u)"         → ("ll-labial-closure",       "u")
//
// The `-raw` suffix is stripped *first*, then the remainder is parsed
// for the (vowel-context) parens. That way M01-style names like
// "ll-labial-closure(a)-raw" group with their non-raw cousins under a
// dedicated raw heading rather than spilling into vowels-raw.
struct ParsedShape {
  std::string groupKey;
  std::string buttonLabel;
};

ParsedShape parseShapeName(const std::string& name) {
  static const std::string rawSuffix = "-raw";
  std::string base = name;
  bool isRaw = false;
  if (base.size() > rawSuffix.size() &&
      base.compare(base.size() - rawSuffix.size(), rawSuffix.size(),
                   rawSuffix) == 0) {
    isRaw = true;
    base = base.substr(0, base.size() - rawSuffix.size());
  }
  size_t lparen = base.find('(');
  if (lparen != std::string::npos && !base.empty() && base.back() == ')') {
    std::string groupKey = base.substr(0, lparen);
    std::string label = base.substr(lparen + 1, base.size() - lparen - 2);
    if (isRaw) groupKey += "-raw";
    return {std::move(groupKey), std::move(label)};
  }
  return {isRaw ? "vowels-raw" : "vowels", std::move(base)};
}

// Look up the closest voiceless-consonant IPA glyph for a gestural
// shape key like "tt-alveolar-fricative". Returns nullptr when the
// shape has no clean single-glyph equivalent (e.g. postalveolar
// closure / lateral, where IPA only has diacritic-decorated forms).
//
// The shapes describe vocal-tract configurations, not full
// phonemes — voicing is a glottis property — so the suffix is meant
// as a "what would this gesture sound like with a voiceless source?"
// hint rather than an exact mapping.
const char* ipaForShape(const std::string& base) {
  static const std::unordered_map<std::string, const char*> map = {
      {"ll-labial-closure",        "p"},
      {"ll-dental-fricative",      "f"},
      {"tt-dental-fricative",      "θ"},
      {"tt-alveolar-closure",      "t"},
      {"tt-alveolar-fricative",    "s"},
      {"tt-alveolar-lateral",      "l"},
      {"tt-postalveolar-fricative","ʃ"},
      {"tb-palatal-fricative",     "ç"},
      {"tb-velar-closure",         "k"},
      {"tb-uvular-fricative",      "χ"},
  };
  auto it = map.find(base);
  return (it != map.end()) ? it->second : nullptr;
}

// Friendly heading from a group key. Vowel groups get nice labels;
// articulator-prefixed keys get the prefix expanded ("tt-" → "Tongue
// tip") and a voiceless-IPA hint appended where there's a clean
// equivalent ("[s]", "[p]", …); a trailing "-raw" becomes a "(raw)"
// suffix; otherwise dashes become spaces and the first letter is
// capitalised.
std::string formatGroupHeading(const std::string& key) {
  if (key == "vowels") return "Vowels";
  if (key == "vowels-raw") return "Vowels (raw)";

  static const std::string rawSuffix = "-raw";
  std::string base = key;
  bool isRaw = false;
  if (base.size() > rawSuffix.size() &&
      base.compare(base.size() - rawSuffix.size(), rawSuffix.size(),
                   rawSuffix) == 0) {
    isRaw = true;
    base = base.substr(0, base.size() - rawSuffix.size());
  }

  struct Prefix {
    const char* code;
    const char* label;
  };
  // Articulator codes used by the JD2 / M01 speaker files. See the
  // upstream VocalTractLab gestural-score docs for the full list.
  static const Prefix prefixes[] = {
      {"ll-", "Lower lip"},
      {"ul-", "Upper lip"},
      {"tt-", "Tongue tip"},
      {"tb-", "Tongue body"},
      {"tm-", "Tongue middle"},
      {"td-", "Tongue dorsum"},
  };
  std::string heading;
  bool matched = false;
  for (const Prefix& p : prefixes) {
    size_t plen = std::strlen(p.code);
    if (base.size() > plen && base.compare(0, plen, p.code) == 0) {
      std::string rest = base.substr(plen);
      for (char& c : rest) {
        if (c == '-') c = ' ';
      }
      heading = std::string(p.label) + " · " + rest;
      matched = true;
      break;
    }
  }
  if (!matched) {
    heading = base;
    for (char& c : heading) {
      if (c == '-') c = ' ';
    }
    if (!heading.empty()) {
      heading[0] = (char)std::toupper((unsigned char)heading[0]);
    }
  }
  if (const char* ipa = ipaForShape(base)) {
    heading += "  [";
    heading += ipa;
    heading += "]";
  }
  if (isRaw) heading += " (raw)";
  return heading;
}

// Map a (X-)SAMPA-style vowel label to its IPA equivalent. Speaker
// files use SAMPA-ish identifiers ("E", "U", "@", "6_low" …); the
// rendered button text uses IPA glyphs (Noto Sans covers the IPA
// Extensions block we need). A trailing ":" is treated as a SAMPA
// length marker and rewritten to the IPA triangular colon "ː".
// Unknown labels pass through unchanged so e.g. "a", "e", "i" stay
// as-is and any speaker-defined extension we haven't seen still
// renders something readable.
std::string toIpa(const std::string& label) {
  bool isLong = !label.empty() && label.back() == ':';
  std::string base = isLong ? label.substr(0, label.size() - 1) : label;
  static const std::unordered_map<std::string, const char*> map = {
      {"a", "a"},      {"e", "e"},      {"i", "i"},      {"o", "o"},
      {"u", "u"},      {"y", "y"},
      {"I", "ɪ"},      {"E", "ɛ"},      {"O", "ɔ"},      {"U", "ʊ"},
      {"Y", "ʏ"},      {"2", "ø"},      {"9", "œ"},      {"@", "ə"},
      // 6 in SAMPA is the near-open central vowel ɐ. The JD2 speaker
      // file distinguishes a mid and low variant — render with a
      // raised / lowered diacritic so both glyphs read at a glance.
      {"6_mid", "ɐ̝"}, {"6_low", "ɐ̞"},
  };
  auto it = map.find(base);
  std::string ipa = (it != map.end()) ? std::string(it->second) : base;
  if (isLong) ipa += "ː";
  return ipa;
}

// Tinted-when-selected SmallButton. Stand-in for ImGui's missing
// segmented control — same idiom used in the 3D panel's solid/wire
// toggle.
bool segmentedButton(const char* label, bool selected) {
  if (selected) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetColorU32(ImGuiCol_ButtonActive));
  }
  bool clicked = ImGui::SmallButton(label);
  if (selected) ImGui::PopStyleColor();
  return clicked;
}

}  // namespace

// Animated transition between tract shapes. A click captures the
// current params as `from` and the preset's params as `to`; subsequent
// frames blend toward `to` over kInterpDuration with a smoothstep
// curve so the 2D outline glides instead of snapping. Back-to-back
// clicks chain because the new `from` is sampled *after* the in-flight
// blend has been applied for the current frame.
struct ShapeInterpolation {
  bool active = false;
  double startTime = 0.0;
  std::vector<double> from;
  std::vector<double> to;
};

constexpr double kInterpDuration = 0.10;  // seconds

// =====================================================================
// Voice-quality presets (Stage 5).
//
// A voice-quality preset writes a small set of supraglottal tract
// params (the "core" set: AES, TT, PW) and optionally a "reference
// posture" set (HY, JA, LD, TCY, LP) when the user has enabled the
// Lock-reference-posture toggle. It also writes the matching glottal
// source coloration via a named glottis shape (looked up in the
// active speaker's glottis-shape list).
//
// The presets are intentionally a thin layer over the orthogonal
// model atoms — this layer doesn't introduce any new state outside
// the panel; everything goes through snap.tractParams /
// snap.glottisParams. Calibration values are educated estimates from
// the literature collected in docs/voice-quality-gestures.md
// (Höglund/Lindblad/Sundberg 2024 for Edge / Kulning / Squillo,
// Echternach 2016 for Soprano F1-tune, CVT / EVT pedagogy for the
// Necessary / Distinct twang scale).
// =====================================================================
struct VoiceQualityPreset {
  const char* name;
  const char* tooltip;
  // Core (always applied).
  double aes;
  double tt;
  double pw;
  // Reference posture (delta from neutral, applied when locked).
  double hyDelta_cm;
  double jaDelta_deg;
  double ldDelta_cm;
  double tcyDelta;
  double lpDelta;
  // Glottis-shape name to look up in the active speaker (skipped if
  // the speaker doesn't define a shape with this name).
  const char* glottisShape;
};

constexpr VoiceQualityPreset kVoiceQualityPresets[] = {
    // name,              tooltip,
    //                                                  AES   TT   PW   ΔHY   ΔJA   ΔLD   ΔTCY  ΔLP   glottis
    {"Neutral",           "Clear voice quality (no twang, no PW).",
                                                        0.0,  0.0,  0.0,  0.0,  0.0,  0.00,  0.0,  0.0, "modal"},
    {"Necessary",         "CVT necessary twang — healthy AES baseline.",
                                                        0.3,  0.0, -0.1,  0.0,  0.0,  0.00,  0.0,  0.0, "modal"},
    {"Distinct",          "CVT distinct twang — strong AES + tilt for ring.",
                                                        0.8,  0.5, -0.2,  0.4, -1.0,  0.15, -0.3,  0.0, "pressed"},
    {"Edge",              "CCM Edge / Belt — Aph≈154 mm², ratio 2.7.",
                                                        0.7,  0.0, -0.4,  0.5, -2.0,  0.20, -0.3,  0.0, "pressed"},
    {"Kulning",           "Swedish herding call — Aph≈186 mm², ratio 2.9.",
                                                        0.6,  0.2, -0.3,  0.3, -2.0,  0.20, -0.2,  0.0, "pressed"},
    {"Squillo",           "Operatic squillo — Aph≈441 mm², ratio 4.6.",
                                                        0.5,  0.7,  0.7, -0.8, -3.0,  0.10, -0.1,  0.0, "modal"},
    {"Soprano F1-tune",   "Sopranic F1 lift for high F0 (jaw drop + lip spread).",
                                                        0.3,  0.5,  0.5,  0.2, -5.0,  0.30, -0.5,  0.0, "modal"},
    {"SOVTE",             "Semi-occluded posture (passive widening).",
                                                        0.0,  0.2,  0.4, -0.2,  0.0,  0.00,  0.0,  0.0, "breathy"},
};
constexpr int kNumVoiceQualityPresets =
    sizeof(kVoiceQualityPresets) / sizeof(kVoiceQualityPresets[0]);

// Reference-posture set: tract param indices written by a voice-
// quality preset *only* when the Lock-reference-posture toggle is on.
// Order matches the per-preset delta fields above.
struct ReferencePostureField {
  int paramIndex;
  size_t deltaOffsetBytes;  // unused; kept for clarity
};
constexpr int kReferencePostureParams[] = {
    VocalTract::HY, VocalTract::JA, VocalTract::LD,
    VocalTract::TCY, VocalTract::LP,
};
constexpr int kNumReferencePostureParams =
    sizeof(kReferencePostureParams) / sizeof(kReferencePostureParams[0]);

// Oral / Nasal sub-presets — written via VS / VO when the user picks
// a sub-mode. Index 0 = Oral (sealed velum), 1 = Nasal (open VP port,
// ~10 mm gap per the imaging literature).
struct OralNasalPreset {
  double vs;
  double vo_cm2;
  const char* label;
};
constexpr OralNasalPreset kOralNasalPresets[2] = {
    {0.95, 0.0, "Oral"},
    {0.50, 0.5, "Nasal"},
};

// Voice-quality state, persistent across frames inside the panel.
struct VoiceQualityState {
  int activePreset = 0;        // index into kVoiceQualityPresets, 0 = Neutral
  int oralNasalMode = -1;      // -1 = none, 0 = Oral, 1 = Nasal
  bool lockReferencePosture = false;
};

// Returns true if `paramIndex` (a VocalTract::ParamIndex value) is
// "owned" by the active voice-quality preset and should NOT be
// overwritten by a vowel / consonant click.
bool isParamOwnedByVoiceQuality(int paramIndex,
                                const VoiceQualityState& vq) {
  if (vq.activePreset > 0) {
    if (paramIndex == VocalTract::AES) return true;
    if (paramIndex == VocalTract::TT)  return true;
    if (paramIndex == VocalTract::PW)  return true;
  }
  if (vq.oralNasalMode >= 0) {
    if (paramIndex == VocalTract::VS) return true;
    if (paramIndex == VocalTract::VO) return true;
  }
  if (vq.lockReferencePosture && vq.activePreset > 0) {
    for (int i = 0; i < kNumReferencePostureParams; ++i) {
      if (kReferencePostureParams[i] == paramIndex) return true;
    }
  }
  return false;
}

// Build the tract-param target for a voice-quality preset, starting
// from `current` and overriding only the params the preset owns.
void buildVoiceQualityTractTarget(const VoiceQualityPreset& preset,
                                  const VoiceQualityState& vq,
                                  const VocalTract::Param* paramInfo,
                                  const std::vector<double>& current,
                                  std::vector<double>& target) {
  target = current;
  // Core (always written).
  target[VocalTract::AES] = preset.aes;
  target[VocalTract::TT]  = preset.tt;
  target[VocalTract::PW]  = preset.pw;
  // Reference posture (deltas from each param's neutral).
  if (vq.lockReferencePosture && vq.activePreset > 0) {
    target[VocalTract::HY]  = paramInfo[VocalTract::HY].neutral  + preset.hyDelta_cm;
    target[VocalTract::JA]  = paramInfo[VocalTract::JA].neutral  + preset.jaDelta_deg;
    target[VocalTract::LD]  = paramInfo[VocalTract::LD].neutral  + preset.ldDelta_cm;
    target[VocalTract::TCY] = paramInfo[VocalTract::TCY].neutral + preset.tcyDelta;
    target[VocalTract::LP]  = paramInfo[VocalTract::LP].neutral  + preset.lpDelta;
  }
  // Velum (Oral / Nasal sub-mode).
  if (vq.oralNasalMode >= 0 &&
      vq.oralNasalMode < (int)(sizeof(kOralNasalPresets) /
                               sizeof(kOralNasalPresets[0]))) {
    target[VocalTract::VS] = kOralNasalPresets[vq.oralNasalMode].vs;
    target[VocalTract::VO] = kOralNasalPresets[vq.oralNasalMode].vo_cm2;
  }
  // Clamp every overridden param to its declared min/max.
  auto clampOne = [&](int idx) {
    double& v = target[idx];
    if (v < paramInfo[idx].min) v = paramInfo[idx].min;
    if (v > paramInfo[idx].max) v = paramInfo[idx].max;
  };
  clampOne(VocalTract::AES);
  clampOne(VocalTract::TT);
  clampOne(VocalTract::PW);
  if (vq.lockReferencePosture && vq.activePreset > 0) {
    for (int i = 0; i < kNumReferencePostureParams; ++i) {
      clampOne(kReferencePostureParams[i]);
    }
  }
  if (vq.oralNasalMode >= 0) {
    clampOne(VocalTract::VS);
    clampOne(VocalTract::VO);
  }
}

// Apply the glottis-side coloration: copy controlParams from the
// named shape (excluding FREQUENCY and PRESSURE so the user keeps
// pitch and lung pressure under their dedicated sliders). Silently
// no-ops if the active speaker doesn't define a shape with that
// name. This is the "(custom)" cause for the Glottis-shape combo
// in ControlsPanel.cpp:155–177 — expected, not a bug.
void applyGlottisShape(AudioEngine& engine, FrameSnapshot& snap,
                       const char* shapeName) {
  if (shapeName == nullptr || *shapeName == '\0') return;
  const auto& gShapes = engine.glottisShapes();
  for (const auto& shape : gShapes) {
    if (shape.name == shapeName) {
      const int n = std::min((int)shape.controlParam.size(),
                             (int)snap.glottisParams.size());
      for (int p = 0; p < n; ++p) {
        if (p == Glottis::FREQUENCY || p == Glottis::PRESSURE) continue;
        snap.glottisParams[p] = shape.controlParam[p];
      }
      return;
    }
  }
}

void renderTractShapesPanel(AudioEngine& engine, FrameSnapshot& snap,
                            const std::vector<SpeakerOption>& speakers,
                            ImFont* buttonFont) {
  // The visible title is "Presets" (Stage 5 broadened the panel
  // beyond just tract shapes), but the ID after `###` stays as the
  // legacy "Tract Shapes" so existing user imgui.ini docking layouts
  // and the DockBuilderDockWindow call in main.cpp keep matching.
  ImGui::Begin("Presets###Tract Shapes");

  static ShapeInterpolation interp;
  static VoiceQualityState vqState;

  // ---- Speaker switcher ----------------------------------------------------
  if (!speakers.empty()) {
    ImGui::SeparatorText("Speaker");

    // Track the active speaker by matching engine.currentSpeakerPath()
    // against the option list. Falls back to "no match" when the
    // running speaker isn't in the list (shouldn't happen with the
    // current flow, but be defensive).
    const std::string& current = engine.currentSpeakerPath();
    int activeIdx = -1;
    for (int i = 0; i < (int)speakers.size(); ++i) {
      if (speakers[i].path == current) {
        activeIdx = i;
        break;
      }
    }

    ImGui::PushID("speaker-switch");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    for (int i = 0; i < (int)speakers.size(); ++i) {
      if (i > 0) ImGui::SameLine();
      if (segmentedButton(speakers[i].displayName.c_str(), i == activeIdx) &&
          i != activeIdx) {
        if (engine.restart(speakers[i].path)) {
          // Refresh in place so the rest of the frame's
          // writeFrameSnapshot doesn't clobber the new engine's
          // defaults with stale params from the old speaker.
          snap = readFrameSnapshot(engine);
          // Drop any in-flight interpolation: its `from`/`to` belong
          // to the previous speaker and would yank the new defaults.
          interp.active = false;
          // Reset voice quality back to Neutral too — the new
          // speaker has its own param neutrals and the preset deltas
          // would compose against the wrong baseline. The lock toggle
          // is a UI preference, so it persists.
          vqState.activePreset = 0;
          vqState.oralNasalMode = -1;
        }
      }
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    ImGui::Spacing();
  }

  // ---- Voice quality (Stage 5) --------------------------------------------
  // Row of segmented buttons for the supraglottal style, an
  // Oral / Nasal sub-row that's only meaningful for non-Neutral
  // presets, and a "Lock reference posture" checkbox that controls
  // whether the preset also writes HY / JA / LD / TCY / LP.
  //
  // Manual flex-wrap: the panel is left-docked and the labels are
  // wide ("Soprano F1-tune"), so eight buttons don't fit on one row
  // in the default layout. Same idiom used further down for the
  // tract-shape grid: SmallButton width = textWidth + 2 * padding;
  // chain SameLine while the next button still fits, otherwise let
  // ImGui drop to a fresh row.
  const ImGuiStyle& vqStyle = ImGui::GetStyle();
  const float vqWindowEnd = ImGui::GetWindowPos().x +
                            ImGui::GetWindowContentRegionMax().x;
  auto smallButtonWidth = [&](const char* label) {
    return ImGui::CalcTextSize(label).x + 2.0f * vqStyle.FramePadding.x;
  };

  ImGui::SeparatorText("Voice quality");
  ImGui::PushID("voice-quality");
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
  for (int i = 0; i < kNumVoiceQualityPresets; ++i) {
    const VoiceQualityPreset& preset = kVoiceQualityPresets[i];
    if (i > 0) {
      float lastX2 = ImGui::GetItemRectMax().x;
      float nextX2 = lastX2 + vqStyle.ItemSpacing.x +
                     smallButtonWidth(preset.name);
      if (nextX2 < vqWindowEnd) ImGui::SameLine();
    }
    bool selected = (i == vqState.activePreset);
    if (segmentedButton(preset.name, selected)) {
      vqState.activePreset = i;
      // Build the tract target. Use the running engine's param info
      // so neutrals come from the actual loaded speaker, not the
      // built-in defaults.
      VocalTract* tract = engine.uiTract();
      std::vector<double> target;
      buildVoiceQualityTractTarget(preset, vqState, tract->params,
                                   snap.tractParams, target);
      interp.from.assign(snap.tractParams.begin(),
                         snap.tractParams.end());
      interp.to = std::move(target);
      interp.startTime = ImGui::GetTime();
      interp.active = true;
      // Glottis writes apply immediately (no interp on the source
      // side; the vowel-click loop already doesn't touch
      // glottisParams). The Glottis-shape combo in
      // ControlsPanel.cpp:155–177 will read "(custom)" after this.
      applyGlottisShape(engine, snap, preset.glottisShape);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", preset.tooltip);
    }
  }
  ImGui::PopStyleVar();
  ImGui::PopID();

  // Oral / Nasal sub-row + Lock toggle. Same wrap pattern.
  ImGui::PushID("voice-quality-sub");
  bool isNeutral = (vqState.activePreset == 0);
  ImGui::BeginDisabled(isNeutral);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
  for (int i = 0; i < 2; ++i) {
    if (i > 0) {
      float lastX2 = ImGui::GetItemRectMax().x;
      float nextX2 = lastX2 + vqStyle.ItemSpacing.x +
                     smallButtonWidth(kOralNasalPresets[i].label);
      if (nextX2 < vqWindowEnd) ImGui::SameLine();
    }
    bool selected = (vqState.oralNasalMode == i);
    if (segmentedButton(kOralNasalPresets[i].label, selected)) {
      vqState.oralNasalMode = selected ? -1 : i;  // toggle off if same
      VocalTract* tract = engine.uiTract();
      const VoiceQualityPreset& preset =
          kVoiceQualityPresets[vqState.activePreset];
      std::vector<double> target;
      buildVoiceQualityTractTarget(preset, vqState, tract->params,
                                   snap.tractParams, target);
      interp.from.assign(snap.tractParams.begin(),
                         snap.tractParams.end());
      interp.to = std::move(target);
      interp.startTime = ImGui::GetTime();
      interp.active = true;
    }
  }
  ImGui::PopStyleVar();
  ImGui::EndDisabled();
  // Lock toggle: changing it doesn't re-apply on its own; the user
  // re-clicks a preset (or it takes effect on the next vowel click,
  // whose skip-list grows / shrinks accordingly). Inline on the same
  // row when there's room, otherwise wrap.
  {
    float lastX2 = ImGui::GetItemRectMax().x;
    const char* lockLabel = "Lock reference posture";
    // Checkbox width = box + label.
    float checkboxW = ImGui::GetFrameHeight() +
                      vqStyle.ItemInnerSpacing.x +
                      ImGui::CalcTextSize(lockLabel).x;
    float nextX2 = lastX2 + vqStyle.ItemSpacing.x + checkboxW;
    if (nextX2 < vqWindowEnd) ImGui::SameLine();
  }
  ImGui::Checkbox("Lock reference posture", &vqState.lockReferencePosture);
  ImGui::PopID();
  ImGui::Spacing();

  // ---- Apply in-flight interpolation --------------------------------------
  // Runs before the shape buttons so a click in this frame captures the
  // current eased value as its new `from`, letting back-to-back clicks
  // chain smoothly.
  if (interp.active) {
    double elapsed = ImGui::GetTime() - interp.startTime;
    double t = elapsed / kInterpDuration;
    if (t >= 1.0) {
      for (int p = 0; p < VocalTract::NUM_PARAMS; ++p) {
        snap.tractParams[p] = interp.to[p];
      }
      interp.active = false;
    } else {
      if (t < 0.0) t = 0.0;
      double e = t * t * (3.0 - 2.0 * t);  // smoothstep
      for (int p = 0; p < VocalTract::NUM_PARAMS; ++p) {
        snap.tractParams[p] =
            interp.from[p] + (interp.to[p] - interp.from[p]) * e;
      }
    }
  }

  // ---- Tract shapes grouped by phonetic category ---------------------------
  const auto& shapes = engine.tractShapes();

  struct Item {
    int shapeIndex;
    std::string label;
  };
  std::vector<std::string> groupOrder;
  std::map<std::string, std::vector<Item>> groups;
  for (int i = 0; i < (int)shapes.size(); ++i) {
    ParsedShape p = parseShapeName(shapes[i].name);
    if (groups.find(p.groupKey) == groups.end()) {
      groupOrder.push_back(p.groupKey);
    }
    groups[p.groupKey].push_back({i, std::move(p.buttonLabel)});
  }

  // Fixed button width keeps the grid neat regardless of label length.
  // Height defaults to the standard frame height (auto-grows when
  // buttonFont is taller than the default).
  constexpr float kButtonWidth = 64.0f;
  const ImGuiStyle& style = ImGui::GetStyle();
  for (const std::string& key : groupOrder) {
    ImGui::SeparatorText(formatGroupHeading(key).c_str());

    // PushID(key) disambiguates duplicate button labels across groups
    // (e.g. "a" appears under "Vowels", "tt-alveolar-fricative", and
    // many others — they'd collide on the default-from-label ID).
    ImGui::PushID(key.c_str());
    const std::vector<Item>& items = groups[key];

    // Manual flex-wrap: SameLine when the next fixed-width button
    // still fits in the row, otherwise drop to a fresh row. The
    // SeparatorText above advances the cursor to a new line, so the
    // first button always starts at column 0.
    const float windowEnd = ImGui::GetWindowPos().x +
                            ImGui::GetWindowContentRegionMax().x;
    for (size_t j = 0; j < items.size(); ++j) {
      const Item& item = items[j];
      if (j > 0) {
        float lastX2 = ImGui::GetItemRectMax().x;
        float nextX2 = lastX2 + style.ItemSpacing.x + kButtonWidth;
        if (nextX2 < windowEnd) ImGui::SameLine();
      }
      // Push the original SAMPA-ish label as the ID, then render the
      // IPA glyph as the visible button text. Keeps the widget IDs
      // stable across IPA mapping tweaks (no ImGui state churn from
      // a relabel) and avoids any worry about IPA glyph variants
      // colliding through ImGui's default label-derived IDs.
      ImGui::PushID(item.label.c_str());
      if (buttonFont) ImGui::PushFont(buttonFont);
      std::string ipa = toIpa(item.label);
      bool clicked = ImGui::Button(ipa.c_str(), ImVec2(kButtonWidth, 0.0f));
      if (buttonFont) ImGui::PopFont();
      if (clicked) {
        // Capture the current (possibly mid-interpolation) params as
        // the new `from` and the preset as the new `to`; the per-frame
        // update above will glide snap.tractParams toward `to`.
        //
        // Skip params owned by the active voice-quality preset
        // (Stage 5): AES / TT / PW always when non-Neutral, VS / VO
        // when Oral / Nasal sub-mode is active, and HY / JA / LD /
        // TCY / LP when the user has locked the reference posture.
        // Owned params keep their `from` value so the interp is a
        // no-op for them and the voice quality stays sticky across
        // vowel changes.
        interp.from.assign(snap.tractParams.begin(), snap.tractParams.end());
        interp.to.assign(VocalTract::NUM_PARAMS, 0.0);
        for (int p = 0; p < VocalTract::NUM_PARAMS; ++p) {
          if (isParamOwnedByVoiceQuality(p, vqState)) {
            interp.to[p] = interp.from[p];
          } else {
            interp.to[p] = shapes[item.shapeIndex].param[p];
          }
        }
        interp.startTime = ImGui::GetTime();
        interp.active = true;
      }
      ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::Spacing();
  }

  ImGui::End();
}

}  // namespace live

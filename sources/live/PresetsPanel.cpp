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

void renderTractShapesPanel(AudioEngine& engine, FrameSnapshot& snap,
                            const std::vector<SpeakerOption>& speakers) {
  ImGui::Begin("Tract Shapes");

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
        }
      }
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    ImGui::Spacing();
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

  // Fixed button width keeps the grid neat regardless of label length
  // — the longest natural label is "6_mid"/"6_low" at 5 chars, plus
  // some padding. Height defaults to the standard frame height.
  constexpr float kButtonWidth = 56.0f;
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
      std::string ipa = toIpa(item.label);
      if (ImGui::Button(ipa.c_str(), ImVec2(kButtonWidth, 0.0f))) {
        for (int p = 0; p < VocalTract::NUM_PARAMS; ++p) {
          snap.tractParams[p] = shapes[item.shapeIndex].param[p];
        }
      }
      ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::Spacing();
  }

  ImGui::End();
}

}  // namespace live

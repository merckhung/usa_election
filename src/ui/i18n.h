// UI localisation: English (default), Spanish, Traditional Chinese, Japanese.
//
// UI strings live in a key -> {en, es, zh-TW, ja} table (i18n.cc). Data
// names come from the data files: state names in all four languages
// (states.json), party names in all four (parties.json); county and
// candidate names are shown as published (English).
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "src/election/model.h"
#include "src/geo/region_tree.h"

namespace usa::ui {

enum class Lang { kEn = 0, kEs = 1, kZhTW = 2, kJa = 3, kCount = 4 };

// Accepts "en", "en-US", "es", "es-US", "zh", "zh-TW", "tw", "ja", "jp"
// (case-insensitive).
bool ParseLang(std::string_view s, Lang* out);
const char* LangCode(Lang l);        // "en", "es", "zh-TW", "ja"
const char* LangNativeName(Lang l);  // "English", "Español", "繁體中文", "日本語"
Lang NextLang(Lang l);
bool IsCjk(Lang l);

// Looks up a UI string; falls back to English, then the key.
const char* Tr(Lang lang, std::string_view key);

// Substitutes {0}, {1}, ... in `pattern`.
std::string Fmt(std::string_view pattern, std::initializer_list<std::string> args);

class Localizer {
 public:
  explicit Localizer(Lang lang = Lang::kEn) : lang_(lang) {}
  Lang lang() const { return lang_; }
  const char* T(std::string_view key) const { return Tr(lang_, key); }

  std::string RegionName(const geo::Region& r) const;
  std::string StateName(const election::StateInfo& s) const;
  std::string CandidateName(const election::Candidate& c) const;
  // Secondary line under the name: the party (or a minor party's own label).
  std::string CandidateAltName(const election::Candidate& c,
                               const election::ElectionData& data) const;
  std::string PartyShort(const election::Party& p) const;
  std::string PartyName(const election::Party& p) const;
  // "Georgia Senate" / "Senado · Georgia" / "喬治亞州 參議員" / "ジョージア州 上院".
  std::string RaceTitle(const election::Race& r) const;
  // "Senate", "Governors", "House" (office tab / chamber names).
  std::string OfficeName(std::string_view office) const;
  std::string ListSeparator() const;  // "、" or ", "

 private:
  Lang lang_;
};

}  // namespace usa::ui

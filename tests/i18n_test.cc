#include <set>
#include <string>

#include "gtest/gtest.h"
#include "src/ui/i18n.h"

namespace usa::ui {
namespace {

TEST(I18n, DefaultsToEnglish) {
  Localizer l;
  EXPECT_EQ(l.lang(), Lang::kEn);
  EXPECT_STREQ(l.T("status.counting"), "Counting");
}

TEST(I18n, ParsesLanguageFlags) {
  Lang l = Lang::kEn;
  ASSERT_TRUE(ParseLang("ja", &l));
  EXPECT_EQ(l, Lang::kJa);
  ASSERT_TRUE(ParseLang("ES", &l));
  EXPECT_EQ(l, Lang::kEs);
  ASSERT_TRUE(ParseLang("zh-TW", &l));
  EXPECT_EQ(l, Lang::kZhTW);
  ASSERT_TRUE(ParseLang("en-US", &l));
  EXPECT_EQ(l, Lang::kEn);
  EXPECT_FALSE(ParseLang("fr", &l));
  EXPECT_EQ(NextLang(Lang::kEn), Lang::kEs);
  EXPECT_EQ(NextLang(Lang::kEs), Lang::kZhTW);
  EXPECT_EQ(NextLang(Lang::kZhTW), Lang::kJa);
  EXPECT_EQ(NextLang(Lang::kJa), Lang::kEn);
  EXPECT_TRUE(IsCjk(Lang::kJa));
  EXPECT_FALSE(IsCjk(Lang::kEs));
}

TEST(I18n, EveryKeyTranslatedInAllLanguages) {
  const char* keys[] = {"app.title",        "status.countdown",      "nation.title",
                        "footer.controls",  "help.9",                "office.senate",
                        "office.governor",  "office.house",          "bop.title",
                        "chart.trend",      "chart.seats",           "chart.margins",
                        "chart.parties",    "chart.grid",            "trend.title",
                        "seats.title.senate", "seats.title.house",   "pip.title",
                        "ev.lead_change.text", "ev.victory.text",    "ev.flip.text",
                        "ev.polls_closed.text", "ev.control.text",   "news.totals",
                        "race.senate",      "race.governor",         "house.called"};
  for (const char* k : keys) {
    std::set<std::string> distinct;
    for (Lang l : {Lang::kEn, Lang::kEs, Lang::kZhTW, Lang::kJa}) {
      const std::string s = Tr(l, k);
      EXPECT_NE(s, k) << "missing key " << k;
      distinct.insert(s);
    }
    EXPECT_GE(distinct.size(), 2u) << k;
  }
  EXPECT_STREQ(Tr(Lang::kEn, "no.such.key"), "no.such.key");
}

TEST(I18n, Formats) {
  EXPECT_EQ(Fmt("{0} days to Election Day", {"35"}), "35 days to Election Day");
  EXPECT_EQ(Fmt("{1}/{0}", {"a", "b"}), "b/a");
  EXPECT_EQ(Fmt("{5}", {"a"}), "");
}

TEST(I18n, LocalizedNames) {
  election::Race race;
  race.office = election::kSenate;
  race.state_name = "Georgia";
  race.state_name_es = "Georgia";
  race.state_name_zh = "喬治亞州";
  race.state_name_ja = "ジョージア州";
  EXPECT_EQ(Localizer(Lang::kEn).RaceTitle(race), "Georgia Senate");
  EXPECT_EQ(Localizer(Lang::kEs).RaceTitle(race), "Senado · Georgia");
  EXPECT_EQ(Localizer(Lang::kZhTW).RaceTitle(race), "喬治亞州 參議員");
  EXPECT_EQ(Localizer(Lang::kJa).RaceTitle(race), "ジョージア州 上院");
  race.special = true;
  race.state_name = "Ohio";
  EXPECT_EQ(Localizer(Lang::kEn).RaceTitle(race), "Ohio Senate (special)");
  race.office = election::kGovernor;
  race.state_name = "Arizona";
  EXPECT_EQ(Localizer(Lang::kEn).RaceTitle(race), "Arizona Governor");
  EXPECT_EQ(Localizer(Lang::kEn).OfficeName(election::kHouse), "House");
  geo::Region r;
  r.level = geo::Level::kState;
  r.name_en = "Georgia";
  r.name_zh = "喬治亞州";
  EXPECT_EQ(Localizer(Lang::kZhTW).RegionName(r), "喬治亞州");
  EXPECT_EQ(Localizer(Lang::kJa).RegionName(r), "Georgia");  // falls back to English
  EXPECT_EQ(Localizer(Lang::kZhTW).ListSeparator(), "、");
}

}  // namespace
}  // namespace usa::ui

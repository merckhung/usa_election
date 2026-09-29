#include "src/news/mock_news.h"

#include "src/election/events.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace usa::news {
namespace {

uint64_t Mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

double Unit(uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

struct Template {
  store::Sentiment sentiment;
  const char* title;
  const char* body;
};

// {0} = candidate, {1} = race title, {2} = rival.
const Template kTemplates[] = {
    {store::Sentiment::kGood, "Crowds pack {0} rally as {1} race heats up",
     "{0} drew a larger-than-expected crowd at a rally last night; the campaign thanked "
     "supporters for their energy."},
    {store::Sentiment::kGood, "Local leaders endorse {0}",
     "Several local officials and community leaders endorsed {0} in the {1} race; the campaign "
     "says momentum is building."},
    {store::Sentiment::kGood, "New poll: {0} gains ground",
     "A new poll of the {1} race shows {0} gaining ground since last month; the campaign "
     "welcomed the numbers."},
    {store::Sentiment::kGood, "{0} praised for infrastructure plan",
     "Several policy experts praised the infrastructure plan released by {0} as detailed and "
     "workable."},
    {store::Sentiment::kBad, "{2} campaign criticizes {0} over past promises",
     "The {2} campaign held a press conference criticizing {0} over past campaign promises; "
     "the {0} campaign said it would respond."},
    {store::Sentiment::kBad, "New poll: {0} slips",
     "A new poll of the {1} race shows {0} slipping since last month; aides concede the race "
     "is tight."},
    {store::Sentiment::kBad, "{0} criticized after debate performance",
     "After the {1} debate, commentators criticized {0}'s performance; the campaign says it "
     "will clarify its positions."},
    {store::Sentiment::kBad, "Turnout at {0} event falls short",
     "Attendance at the weekend event for {0} fell short of expectations; the campaign blamed "
     "the weather."},
    {store::Sentiment::kNeutral, "{0} visits diners on the campaign trail",
     "{0} spent the day visiting diners and small businesses, shaking hands with voters."},
    {store::Sentiment::kNeutral, "{0} and {2} attend the same community forum",
     "{0} and {2} both attended a community forum today and exchanged greetings."},
    {store::Sentiment::kNeutral, "{0} releases new campaign ad",
     "The {0} campaign released a new ad introducing its vision for the {1} seat."},
};

std::string Fill(const char* pattern, const std::string& a, const std::string& b,
                 const std::string& c) {
  std::string out;
  for (const char* p = pattern; *p; ++p) {
    if (p[0] == '{' && p[1] >= '0' && p[1] <= '2' && p[2] == '}') {
      out += p[1] == '0' ? a : p[1] == '1' ? b : c;
      p += 2;
    } else {
      out += *p;
    }
  }
  return out;
}

}  // namespace

MockNewsGenerator::MockNewsGenerator(const election::ElectionData* data, uint64_t seed)
    : data_(data), seed_(seed) {
  // Main candidates get more coverage: weight by party size and incumbency.
  const auto& races = data_->races();
  for (size_t r = 0; r < races.size(); ++r) {
    for (size_t c = 0; c < races[r].candidates.size(); ++c) {
      const auto& cand = races[r].candidates[c];
      int weight = 1;
      if (cand.party == "DEM" || cand.party == "REP") weight += 3;
      if (cand.incumbent || !cand.endorsed_by.empty()) weight += 2;
      for (int w = 0; w < weight; ++w) pool_.push_back({static_cast<int>(r), static_cast<int>(c)});
    }
  }
}

MockArticle MockNewsGenerator::Make(uint64_t h, double minute) {
  const auto& races = data_->races();
  const auto [ri, ci] = pool_[Mix(h + 1) % pool_.size()];
  const election::Race& race = races[ri];
  const election::Candidate& cand = race.candidates[ci];
  int rival = static_cast<int>(Mix(h + 2) % race.candidates.size());
  if (rival == ci) rival = (rival + 1) % static_cast<int>(race.candidates.size());
  const Template& t = kTemplates[Mix(h + 3) % (sizeof(kTemplates) / sizeof(kTemplates[0]))];
  const std::string& rival_name = race.candidates[rival].name;

  MockArticle m;
  m.candidate_id = cand.id;
  m.intended = t.sentiment;
  m.article.title = "[SIMULATED] " + Fill(t.title, cand.name, race.Title(), rival_name);
  m.article.summary = Fill(t.body, cand.name, race.Title(), rival_name) +
                      " (Simulated item generated for testing; not a real news report.)";
  m.article.source = "MOCK";
  char url[96];
  std::snprintf(url, sizeof url, "mock://news/%016llx", static_cast<unsigned long long>(h));
  m.article.url = url;
  // Election night: 18:00 ET + minute (negative = earlier that day).
  m.article.published_at = election::NightTimestamp(std::max(-18 * 60.0, minute));
  m.article.simulated = true;
  return m;
}

std::vector<MockArticle> MockNewsGenerator::Between(double from, double to, double per_hour) {
  std::vector<MockArticle> out;
  // One potential item slot per simulated minute; deterministic per minute.
  for (int minute = static_cast<int>(std::ceil(from)); minute < to; ++minute) {
    const uint64_t h = Mix(seed_ ^ (static_cast<uint64_t>(minute + 100000) * 0x51ED27));
    if (Unit(h) < per_hour / 60.0) out.push_back(Make(h, minute));
  }
  return out;
}

std::vector<MockArticle> MockNewsGenerator::Batch(int n) {
  std::vector<MockArticle> out;
  for (int i = 0; i < n; ++i) out.push_back(Make(Mix(seed_ + 7919ull * (i + 1)), -120));
  return out;
}

}  // namespace usa::news

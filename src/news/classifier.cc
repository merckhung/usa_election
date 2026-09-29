#include "src/news/classifier.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "nlohmann/json.hpp"
#include "src/news/http.h"

namespace usa::news {

using nlohmann::json;

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Name variants: "Dan S. Sullivan" -> {"dan s. sullivan", "dan sullivan"};
// "Tommy Tuberville Jr." -> {"tommy tuberville jr.", "tommy tuberville"}.
std::vector<std::string> NameVariants(const std::string& name) {
  std::vector<std::string> out = {Lower(name)};
  std::vector<std::string> words;
  size_t i = 0;
  while (i < name.size()) {
    while (i < name.size() && name[i] == ' ') ++i;
    const size_t start = i;
    while (i < name.size() && name[i] != ' ') ++i;
    if (i > start) words.push_back(Lower(name.substr(start, i - start)));
  }
  while (words.size() > 2 && (words.back() == "jr." || words.back() == "sr." ||
                              words.back() == "ii" || words.back() == "iii")) {
    words.pop_back();
  }
  if (words.size() > 2) {
    const std::string short_name = words.front() + " " + words.back();
    if (short_name != out.front()) out.push_back(short_name);
  }
  return out;
}

std::string ArticleText(const store::Article& a) { return a.title + "\n" + a.summary; }

// Sentence splitter (also handles CJK punctuation in translated feeds).
std::vector<std::string> Sentences(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = text[i];
    const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
    const std::string ch = text.substr(i, n);
    cur += ch;
    i += n;
    if (ch == "。" || ch == "！" || ch == "？" || ch == "；" || ch == "\n" || ch == "!" ||
        ch == "?" || (ch == "." && (i >= text.size() || text[i] == ' '))) {
      out.push_back(cur);
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

const std::vector<std::string>& PositiveWords() {
  static const auto* w = new std::vector<std::string>{
      "lead", "leads", "leading", "wins", "won", "victory", "victorious", "endorse", "endorsed",
      "endorsement", "surge", "surges", "surging", "praised", "ahead", "momentum", "record haul",
      "outraised", "boost", "rally", "rallies", "gains", "gaining", "favored", "projected winner",
      "declares victory", "front-runner", "frontrunner", "strong debate", "bipartisan support"};
  return *w;
}

const std::vector<std::string>& NegativeWords() {
  static const auto* w = new std::vector<std::string>{
      "trails", "trailing", "behind", "scandal", "indicted", "indictment", "criticized",
      "criticised", "controversy", "loses", "lost", "concedes", "conceded", "investigation",
      "probe", "accused", "defeat", "defeated", "gaffe", "backlash", "slump", "slips", "falls",
      "drops", "lawsuit", "sued", "ethics complaint", "under fire", "struggles", "withdraws",
      "outspent", "attack ad", "falsely"};
  return *w;
}

// "A leads B" style phrases: good for the name before, bad for the one after.
const std::vector<std::string>& OvertakeWords() {
  static const auto* w = new std::vector<std::string>{"leads", "overtakes", "beats", "defeats",
                                                      "outpaces", "tops", "edges"};
  return *w;
}

int Count(const std::string& s, const std::vector<std::string>& words) {
  const std::string lower = Lower(s);
  int n = 0;
  for (const std::string& w : words) {
    for (size_t p = lower.find(w); p != std::string::npos; p = lower.find(w, p + w.size())) ++n;
  }
  return n;
}

size_t FindName(const std::string& sentence, const CandidateRef& c) {
  const std::string lower = Lower(sentence);
  for (const std::string& n : NameVariants(c.name)) {
    if (n.size() >= 5) {
      const size_t p = lower.find(n);
      if (p != std::string::npos) return p;
    }
  }
  return std::string::npos;
}

}  // namespace

std::vector<CandidateRef> CandidateRefs(const election::ElectionData& data) {
  std::vector<CandidateRef> out;
  for (const election::Race& r : data.races()) {
    for (const election::Candidate& c : r.candidates) {
      out.push_back({c.id, c.name, data.party(c.party).short_en, r.Title()});
    }
  }
  return out;
}

std::vector<CandidateRef> MentionedCandidates(const store::Article& a,
                                              const std::vector<CandidateRef>& all) {
  const std::string text = ArticleText(a);
  std::vector<CandidateRef> out;
  for (const CandidateRef& c : all) {
    // Full names only: surnames alone are ambiguous (two Dan Sullivans in Alaska).
    if (FindName(text, c) != std::string::npos) out.push_back(c);
  }
  return out;
}

bool HeuristicClassifier::Classify(const store::Article& a,
                                   const std::vector<CandidateRef>& candidates,
                                   ClassifyResult* out, std::string*) {
  out->model = Name();
  out->digest = a.title;
  out->assessments.clear();
  const std::vector<std::string> sentences = Sentences(ArticleText(a));
  for (const CandidateRef& c : candidates) {
    int score = 0;
    for (const std::string& s : sentences) {
      const size_t at = FindName(s, c);
      if (at == std::string::npos) continue;
      // Comparative phrase with another candidate in the same sentence.
      bool comparative = false;
      for (const CandidateRef& other : candidates) {
        if (other.id == c.id) continue;
        const size_t ot = FindName(s, other);
        if (ot == std::string::npos) continue;
        const std::string lower = Lower(s);
        const size_t first = std::min(at, ot), second = std::max(at, ot);
        auto between = [&](const std::string& w) {
          const size_t wp = lower.find(w, first);
          return wp != std::string::npos && wp < second;
        };
        const bool ahead = std::any_of(OvertakeWords().begin(), OvertakeWords().end(), between);
        const bool behind = between("trails") || between("behind");
        if (ahead || behind) {
          // "A leads B": good for A (named first), bad for B; "A trails B" the reverse.
          const bool c_first = at < ot;
          score += (ahead != c_first) ? -2 : 2;
          comparative = true;
        }
      }
      if (!comparative) score += Count(s, PositiveWords()) - Count(s, NegativeWords());
    }
    store::Assessment as;
    as.candidate_id = c.id;
    as.sentiment = score > 0 ? store::Sentiment::kGood
                   : score < 0 ? store::Sentiment::kBad
                               : store::Sentiment::kNeutral;
    as.reason = "keyword score " + std::to_string(score);
    out->assessments.push_back(std::move(as));
  }
  return true;
}

void ApplyLlmEnvironment(LlmConfig* config) {
  auto env = [](const char* name) -> std::string {
    const char* v = std::getenv(name);
    return v ? v : "";
  };
  if (config->api_key.empty()) config->api_key = env("USA_LLM_API_KEY");
  if (config->api_key.empty()) config->api_key = env("OPENAI_API_KEY");
  // Environment only replaces defaults: explicit flags win.
  const LlmConfig defaults;
  if (config->base_url == defaults.base_url) {
    if (!env("USA_LLM_BASE_URL").empty()) config->base_url = env("USA_LLM_BASE_URL");
    else if (!env("OPENAI_BASE_URL").empty()) config->base_url = env("OPENAI_BASE_URL");
  }
  if (config->model == defaults.model && !env("USA_LLM_MODEL").empty()) {
    config->model = env("USA_LLM_MODEL");
  }
}

std::string LlmClassifier::BuildRequest(const LlmConfig& config, const store::Article& a,
                                        const std::vector<CandidateRef>& candidates) {
  std::string list;
  for (const CandidateRef& c : candidates) {
    list += "- " + c.id + " | " + c.name + " | " + c.party + " | " + c.race_title + "\n";
  }
  const std::string system =
      "You analyse US news for the 2026-11-03 midterm elections (Senate and governor races). "
      "For each "
      "listed candidate the article is about, decide whether the news is good, bad or "
      "neutral for that candidate's electoral prospects. Judge the facts reported, not the "
      "outlet's tone; routine campaign activity is neutral. Use only candidate ids from the "
      "list. Reply with a single JSON object and nothing else:\n"
      "{\"summary\": \"<one sentence in English, <= 120 characters>\", "
      "\"assessments\": [{\"candidate_id\": \"<id>\", \"sentiment\": \"good|bad|neutral\", "
      "\"reason\": \"<English, <= 60 characters>\"}]}";
  const std::string user = "Candidates (id | name | party | race):\n" + list +
                           "\nArticle source: " + a.source + "\nPublished: " + a.published_at +
                           "\nTitle: " + a.title + "\nText: " + a.summary;
  json req = {{"model", config.model},
              {"temperature", config.temperature},
              {"messages",
               json::array({{{"role", "system"}, {"content", system}},
                            {{"role", "user"}, {"content", user}}})}};
  if (config.json_mode) req["response_format"] = {{"type", "json_object"}};
  return req.dump();
}

bool LlmClassifier::ParseResponse(const std::string& body,
                                  const std::vector<CandidateRef>& candidates,
                                  ClassifyResult* out, std::string* error) {
  const json resp = json::parse(body, nullptr, false);
  if (resp.is_discarded()) {
    *error = "LLM response is not JSON";
    return false;
  }
  if (resp.contains("error")) {
    *error = "LLM error: " + resp["error"].dump();
    return false;
  }
  std::string content;
  try {
    content = resp.at("choices").at(0).at("message").at("content").get<std::string>();
  } catch (const json::exception&) {
    *error = "unexpected LLM response shape";
    return false;
  }
  // Tolerate ```json fences and prose around the object.
  const size_t b = content.find('{'), e = content.rfind('}');
  if (b == std::string::npos || e == std::string::npos || e < b) {
    *error = "no JSON object in LLM answer";
    return false;
  }
  const json answer = json::parse(content.substr(b, e - b + 1), nullptr, false);
  if (answer.is_discarded() || !answer.is_object()) {
    *error = "LLM answer is not valid JSON";
    return false;
  }
  if (resp.contains("model") && resp["model"].is_string()) {
    out->model = resp["model"].get<std::string>();
  }
  out->digest = answer.value("summary", "");
  out->assessments.clear();
  for (const json& item : answer.value("assessments", json::array())) {
    const std::string id = item.value("candidate_id", "");
    const bool known = std::any_of(candidates.begin(), candidates.end(),
                                   [&](const CandidateRef& c) { return c.id == id; });
    store::Sentiment s;
    if (!known || !store::ParseSentiment(Lower(item.value("sentiment", "")), &s)) continue;
    out->assessments.push_back({id, s, item.value("reason", "")});
  }
  return true;
}

bool LlmClassifier::Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                             ClassifyResult* out, std::string* error) {
  out->model = config_.model;
  std::string url = config_.base_url;
  while (!url.empty() && url.back() == '/') url.pop_back();
  url += "/chat/completions";
  std::vector<std::string> headers;
  if (!config_.api_key.empty()) headers.push_back("Authorization: Bearer " + config_.api_key);
  const HttpResponse r =
      HttpPostJson(url, BuildRequest(config_, a, candidates), headers, config_.timeout_s);
  if (!r.error.empty()) {
    *error = "LLM request failed: " + r.error;
    return false;
  }
  if (r.status != 200) {
    *error = "LLM HTTP " + std::to_string(r.status) + ": " + r.body.substr(0, 300);
    return false;
  }
  return ParseResponse(r.body, candidates, out, error);
}

}  // namespace usa::news

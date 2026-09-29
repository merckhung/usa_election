// Where live tallies come from.
//
//  * FileResultsSource: polls a results JSON file (see results.h for the
//    format) and reloads it whenever it changes. Point any feed/scraper at
//    that file, e.g. the converter in tools/.
//  * SimulatedResultsSource: a deterministic, clearly-labelled DEMO that
//    fakes election night so the UI can be exercised before 2026-11-03.
//    Candidate strengths are random and party-blind; they are not forecasts.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "src/election/model.h"
#include "src/election/results.h"
#include "src/geo/region_tree.h"

namespace usa::election {

class ResultsSource {
 public:
  virtual ~ResultsSource() = default;
  // Returns a new snapshot when the data changed since the last call.
  virtual std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) = 0;
  virtual std::string Describe() const = 0;
};

class FileResultsSource : public ResultsSource {
 public:
  FileResultsSource(const ElectionData* data, std::string path, double poll_interval_s = 1.0);
  std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) override;
  std::string Describe() const override;
  const std::string& last_error() const { return last_error_; }

 private:
  const ElectionData* data_;
  std::string path_;
  double poll_interval_s_;
  double last_poll_ = -1e9;
  std::filesystem::file_time_type last_mtime_{};
  bool loaded_once_ = false;
  uint64_t version_ = 0;
  std::string last_error_;
};

// Simulated election night, 18:00 ET -> 06:00 ET. Each state starts
// reporting when its last polls close (19:00 ET in Georgia ... 01:00 ET in
// Alaska). Every county reports in several batches following a skewed curve
// (early/mail ballots first in some states, election-day votes first in
// others, so leads can change: the "red mirage" / "blue shift"). Mail-heavy
// western states are still counting at 06:00. House seats are called state
// by state from the sitting delegations with a few random flips. Numbers are
// random and party-blind for Senate/Governor races: this is a demo of the
// counting process, not a forecast.
class SimulatedResultsSource : public ResultsSource {
 public:
  static constexpr double kCountMinutes = 720;  // 18:00 ET -> 06:00 ET

  static constexpr double kMinSpeed = 1;     // real time: 12 hours
  static constexpr double kMaxSpeed = 4096;  // ~10 seconds for the night

  // `speed`: simulated seconds per wall-clock second (x1 = real time).
  SimulatedResultsSource(const ElectionData* data, const geo::RegionTree* tree, uint64_t seed,
                         double speed = 64);

  std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) override;
  std::string Describe() const override;

  // Snapshot at `minutes` after 18:00 ET (clamped to [0, kCountMinutes]).
  std::shared_ptr<ResultsSnapshot> SnapshotAtClock(double minutes) const;
  // Snapshot at counting progress `p` in [0, 1] of the evening (time-based).
  std::shared_ptr<ResultsSnapshot> SnapshotAt(double p) const;

  void set_paused(bool paused) { paused_ = paused; }
  bool paused() const { return paused_; }
  void SeekClock(double minutes);  // also re-arms snapshot emission
  void SeekProgress(double p) { SeekClock(p * kCountMinutes); }
  double clock_minutes() const { return clock_; }
  double progress() const { return clock_ / kCountMinutes; }
  // Simulated seconds per real second, snapped to a power of two in
  // [kMinSpeed, kMaxSpeed]: x1, x2, x4, ... x4096.
  void set_speed(double s);
  double speed() const { return speed_; }
  void SpeedUp() { set_speed(speed_ * 2); }
  void SlowDown() { set_speed(speed_ / 2); }
  int station_count() const { return static_cast<int>(stations_.size()); }
  // Minute (after 18:00 ET) by which every station has reported.
  double last_report_minute() const { return last_report_; }

  // Votes reported per `bucket_minutes` bucket from 18:00 ET up to `until`
  // (used to back-fill the inflow chart when starting mid-evening).
  std::vector<int64_t> InflowHistory(double bucket_minutes, double until) const;

  // "18:42" for `minutes` after 18:00 ET.
  static std::string ClockLabel(double minutes);

 private:
  struct Station {
    float minute;       // report time, minutes after 18:00 ET
    int race;           // index into data->races()
    int village;        // region id of the county
    int32_t eligible;
    int32_t cast;
    uint32_t votes_at;  // offset into votes_
  };
  struct HouseCall {
    float minute;
    int state;  // index into data->states()
    std::string party;
  };
  std::vector<HouseCall> house_calls_;  // sorted by minute
  void Build();
  void PlanDeclarations();

  struct PlannedDeclaration {
    float minute;
    int race;
    int candidate;
    Declaration::Type type;
  };
  std::vector<PlannedDeclaration> declarations_;

  const ElectionData* data_;
  const geo::RegionTree* tree_;
  uint64_t seed_;
  bool paused_ = false;
  double speed_ = 64;
  double clock_ = 0;
  double last_report_ = 0;
  double last_now_ = -1;
  double last_emit_ = -1e9;
  size_t emitted_reported_ = SIZE_MAX;
  size_t emitted_declared_ = SIZE_MAX;
  size_t emitted_called_ = SIZE_MAX;
  uint64_t version_ = 0;
  std::vector<Station> stations_;  // sorted by report time
  std::vector<int32_t> votes_;
  std::vector<int> village_units_;  // region id -> stations in that village
};

}  // namespace usa::election

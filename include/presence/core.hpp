#pragma once
#include "presence/domain.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace presence {
struct Job {
  std::string runtime;
  std::uint64_t generation = 0, id = 0;
  Window window;
};
struct Outcome {
  std::string runtime;
  std::uint64_t generation = 0, job_id = 0, window_id = 0;
  Estimate estimate = Estimate::unknown;
  bool success = true;
};
struct Result {
  Estimate estimate = Estimate::unknown;
  Assessment assessment{Validity::unknown, Eligibility::unknown, {}, {}};
  std::vector<std::string> reasons{"service_starting"};
  std::optional<Window> window;
};
struct Diagnostics {
  std::uint64_t rejected = 0, skipped = 0, stale = 0;
  std::string reason;
  Validity evidence = Validity::unknown;
  Eligibility domain = Eligibility::unknown;
  std::vector<std::string> evidence_reasons, domain_reasons;
};
struct Step {
  std::optional<Job> job;
  bool publish = false;
};
class PresenceMonitor {
public:
  PresenceMonitor(Contract, std::string runtime);
  Step observe(Observation, Time now);
  Step reject(std::string reason, Time now);
  Step tick(Time now);
  Step complete(const Outcome &, Time now);
  Step stop(Time now);
  Step fail_startup(Time now);
  const Result &current() const { return current_; }
  const std::optional<Result> &last_known() const { return history_; }
  const Diagnostics &diagnostics() const { return diagnostics_; }
  const std::string &runtime() const { return runtime_; }
  std::uint64_t generation() const { return generation_; }

private:
  struct SourceState {
    std::optional<Observation> last;
    std::vector<Observation> records;
  };
  Contract contract_;
  std::string runtime_;
  std::map<std::string, SourceState> sources_;
  Result current_;
  std::optional<Result> history_;
  Diagnostics diagnostics_;
  std::optional<Job> running_;
  std::optional<Window> pending_;
  std::uint64_t generation_ = 1, next_window_ = 1, next_job_ = 1;
  bool stopped_ = false;
  void clear_assembly();
  void invalidate(const std::string &, const std::string &,
                  const Observation &);
  void withdraw(const std::string &, Assessment);
  Step advance(Time, bool changed);
  void diagnostic(std::string,
                  Assessment = {
                      Validity::invalid, Eligibility::unknown, {}, {}});
};

} // namespace presence

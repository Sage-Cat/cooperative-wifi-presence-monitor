#include "presence/core.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace presence;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
Contract contract() {
  Contract c;
  c.sources = {"rx-a", "rx-b"};
  c.features = {"synthetic"};
  c.units = {"unitless"};
  c.metadata = {{"scenario", "synthetic"}};
  c.pipeline_hash = "synthetic-pipeline";
  c.samples_per_source = 2;
  c.max_age = 100;
  c.max_window_span = 20;
  c.max_alignment = 10;
  c.max_uncertainty = 5;
  return c;
}
Observation record(const Contract &c, const std::string &source,
                   std::uint64_t sequence, Time time) {
  Observation o;
  o.source = source;
  o.boot_epoch = "boot-1";
  o.config_epoch = "config-1";
  o.pipeline_hash = c.pipeline_hash;
  o.sequence = sequence;
  o.capture = time;
  o.features = c.features;
  o.units = c.units;
  o.values = {1};
  o.metadata = c.metadata;
  return o;
}
Job window(PresenceMonitor &monitor, const Contract &c, std::uint64_t seq,
           Time capture, Time now) {
  Step step;
  for (const auto &source : c.sources)
    for (std::uint64_t i = 0; i < 2; ++i)
      step = monitor.observe(
          record(c, source, seq + i, capture + static_cast<Time>(i)), now);
  check(step.job.has_value(), "expected eligible inference job");
  return *step.job;
}
Outcome outcome(const Job &job, Estimate estimate = Estimate::present) {
  return {job.runtime, job.generation, job.id, job.window.id, estimate, true};
}
void boundaries() {
  auto c = contract();
  PresenceMonitor m(c, "runtime-a");
  auto job = window(m, c, 1, 100, 101);
  m.complete(outcome(job), 101);
  check(m.current().estimate == Estimate::present, "present accepted");
  check(job.window.deadline == 201, "capture end fixes deadline");
  m.tick(201);
  check(m.current().estimate == Estimate::present,
        "expiry equality must be inclusive");
  m.tick(202);
  check(m.current().estimate == Estimate::unknown,
        "first tick after expiry withdraws");
  check(m.last_known() && m.last_known()->window->deadline == 201,
        "history keeps original deadline");
  auto w = job.window;
  for (auto &source : w.sources)
    source.uncertainty = 5;
  check(qualify(w, c, 196).accepted(), "uncertainty equality");
  check(!qualify(w, c, 197).accepted(), "uncertainty subtracts validity");
  check(qualify(w, c, 96).accepted(), "future mapping at uncertainty equality");
  check(!qualify(w, c, 95).accepted(), "future mapping beyond uncertainty");
}
void retention_and_failures() {
  auto c = contract();
  PresenceMonitor m(c, "runtime-b");
  auto job = window(m, c, 1, 100, 101);
  m.complete(outcome(job), 101);
  auto reject = [&](Observation o) {
    m.observe(std::move(o), 110);
    check(m.current().estimate == Estimate::present,
          "candidate rejection must retain eligible current");
    check(m.current().window->deadline == 201,
          "candidate cannot renew deadline");
  };
  auto o = record(c, "rx-a", 3, 110);
  o.integrity = false;
  reject(o);
  o = record(c, "rx-a", 3, 110);
  o.features = {"mismatch"};
  reject(o);
  o = record(c, "rx-a", 3, 110);
  o.values = {NAN};
  reject(o);
  o = record(c, "rx-a", 3, 110);
  o.uncertainty = 6;
  reject(o);
  o = record(c, "rx-a", 3, 120);
  reject(o);
  // Loss of rx-b can never silently select an undeclared single-node model.
  check(!m.observe(record(c, "rx-a", 3, 110), 110).job,
        "partial subset cannot infer");
  check(!m.observe(record(c, "rx-a", 4, 111), 111).job,
        "partial subset cannot infer");
  m.tick(202);
  check(m.current().estimate == Estimate::unknown,
        "node loss expires original evidence");
}
void pending_and_completions() {
  auto c = contract();
  PresenceMonitor m(c, "runtime-c");
  auto first = window(m, c, 1, 100, 101);
  for (std::uint64_t seq = 3; seq <= 9; seq += 2) {
    Step step;
    for (const auto &source : c.sources)
      for (std::uint64_t i = 0; i < 2; ++i)
        step = m.observe(
            record(c, source, seq + i, 100 + static_cast<Time>(seq + i)), 120);
    check(!step.job, "single running slot");
  }
  check(m.diagnostics().skipped == 3, "exactly one latest pending window");
  auto unexpected = outcome(first);
  ++unexpected.job_id;
  m.complete(unexpected, 120);
  check(m.current().estimate == Estimate::unknown,
        "unexpected completion cannot publish");
  auto next = m.complete(outcome(first), 120);
  check(next.job.has_value(), "completion dispatches pending");
  check(next.job->window.sources.front().first_sequence == 9,
        "latest pending retained");
  m.complete(outcome(first, Estimate::absent), 120);
  check(m.current().estimate == Estimate::present,
        "duplicate cannot replace current");
  m.complete(outcome(*next.job, Estimate::unknown), 120);
  check(m.current().estimate == Estimate::unknown,
        "accepted abstention withdraws class");
  check(m.current().assessment.accepted(),
        "abstention does not corrupt evidence qualification");
}
void invalidation_and_slow_result() {
  auto c = contract();
  PresenceMonitor m(c, "runtime-d");
  auto first = window(m, c, 1, 100, 101);
  m.complete(outcome(first), 101);
  auto running = window(m, c, 3, 110, 111);
  auto changed = record(c, "rx-a", 1, 112);
  changed.boot_epoch = "boot-2";
  m.observe(changed, 112);
  check(m.current().estimate == Estimate::unknown,
        "epoch invalidates immediately");
  m.complete(outcome(running), 113);
  check(m.current().estimate == Estimate::unknown,
        "obsolete generation cannot restore class");
  PresenceMonitor slow(c, "runtime-e");
  auto expired = window(slow, c, 1, 100, 101);
  slow.complete(outcome(expired), 202);
  check(slow.current().estimate == Estimate::unknown,
        "slow inference cannot refresh age");
  PresenceMonitor domain(c, "runtime-f");
  first = window(domain, c, 1, 100, 101);
  domain.complete(outcome(first), 101);
  changed = record(c, "rx-a", 3, 110);
  changed.metadata["scenario"] = "changed";
  domain.observe(changed, 110);
  check(domain.current().estimate == Estimate::unknown,
        "known environment mismatch invalidates");
  domain.stop(111);
  domain.complete(outcome(first), 112);
  check(domain.current().estimate == Estimate::unknown,
        "shutdown rejects late completion");
}
void extreme_times_and_epoch_contract_change() {
  auto c = contract();
  PresenceMonitor m(c, "runtime-g");
  auto first = window(m, c, 1, 100, 101);
  m.complete(outcome(first), 101);
  auto changed = record(c, "rx-a", 1, 110);
  changed.config_epoch = "config-2";
  changed.features = {"changed-feature"};
  m.observe(changed, 110);
  check(
      m.current().estimate == Estimate::unknown,
      "known epoch change must invalidate despite candidate feature mismatch");
  auto w = first.window;
  for (auto &source : w.sources) {
    source.start = std::numeric_limits<Time>::max() - 1;
    source.end = std::numeric_limits<Time>::max();
    source.uncertainty = 5;
  }
  check(!qualify(w, c, 101).accepted(), "extreme future invalid");
  for (auto &source : w.sources) {
    source.start = std::numeric_limits<Time>::min();
    source.end = std::numeric_limits<Time>::max();
    source.uncertainty = 0;
  }
  check(!qualify(w, c, 0).accepted(), "extreme span invalid without overflow");
  for (auto &source : w.sources) {
    source.start = 100;
    source.end = 101;
    source.uncertainty = 0;
  }
  check(!qualify(w, c, std::numeric_limits<Time>::min()).accepted(),
        "negative clock cannot justify mapping");
}
} // namespace
int main() {
  try {
    boundaries();
    std::cout << "PASS deterministic clock boundaries and uncertainty\n";
    retention_and_failures();
    std::cout << "PASS candidate retention and exact source subset\n";
    pending_and_completions();
    std::cout << "PASS bounded pending, unexpected and duplicate completion\n";
    invalidation_and_slow_result();
    std::cout << "PASS epoch/domain invalidation and slow inference\n";
    extreme_times_and_epoch_contract_change();
    std::cout << "PASS extreme clocks and epoch/feature simultaneous change\n";
    std::cout << "CORE TESTS: 5 groups passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}

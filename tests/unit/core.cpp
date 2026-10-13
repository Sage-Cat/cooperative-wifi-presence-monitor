#include "presence/core.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool value, const char *why) {
  if (!value)
    throw std::runtime_error(why);
}
presence::Contract contract() {
  presence::Contract c;
  c.sources = {"a"};
  c.features = {"f"};
  c.units = {"u"};
  c.metadata = {{"room", "one"}};
  c.pipeline_hash = "hash";
  c.max_age = 100;
  return c;
}
presence::Observation observation(std::uint64_t seq, presence::Time capture) {
  presence::Observation o;
  o.source = "a";
  o.boot_epoch = "boot";
  o.config_epoch = "cfg";
  o.pipeline_hash = "hash";
  o.sequence = seq;
  o.capture = capture;
  o.features = {"f"};
  o.units = {"u"};
  o.values = {1};
  o.metadata = {{"room", "one"}};
  return o;
}
presence::Outcome complete(const presence::Job &j, presence::Estimate e) {
  return {j.runtime, j.generation, j.id, j.window.id, e, true};
}
} // namespace
int main() {
  try {
    presence::PresenceMonitor monitor(contract(), "runtime");
    auto first = monitor.observe(observation(1, 1000), 1000);
    require(first.job.has_value(), "initial dispatch");
    monitor.complete(complete(*first.job, presence::Estimate::present), 1000);
    auto deadline = monitor.current().window->deadline;
    monitor.reject("bad_frame", 1010);
    require(monitor.current().estimate == presence::Estimate::present,
            "candidate retention");
    require(monitor.current().window->deadline == deadline,
            "immutable deadline");
    monitor.tick(1100);
    require(monitor.current().estimate == presence::Estimate::present,
            "inclusive expiry");
    monitor.tick(1101);
    require(monitor.current().estimate == presence::Estimate::unknown,
            "expiry");
    require(monitor.last_known()->estimate == presence::Estimate::present,
            "history");
    auto second = monitor.observe(observation(2, 1110), 1110);
    monitor.complete(complete(*second.job, presence::Estimate::present), 1110);
    auto abstain = monitor.observe(observation(3, 1120), 1120);
    monitor.complete(complete(*abstain.job, presence::Estimate::unknown), 1120);
    require(monitor.current().estimate == presence::Estimate::unknown,
            "abstain no fallback");
    require(monitor.current().assessment.evidence == presence::Validity::valid,
            "independent evidence");
    auto running = monitor.observe(observation(4, 1130), 1130);
    auto pending = monitor.observe(observation(5, 1140), 1140);
    require(!pending.job, "one running");
    monitor.observe(observation(6, 1150), 1150);
    require(monitor.diagnostics().skipped == 1, "latest pending");
    auto changed = observation(1, 1160);
    changed.config_epoch = "changed";
    changed.features = {"incompatible"};
    monitor.observe(changed, 1160);
    require(monitor.current().estimate == presence::Estimate::unknown,
            "epoch invalidation");
    auto old = monitor.complete(
        complete(*running.job, presence::Estimate::present), 1160);
    require(!old.job, "invalidation clears pending");
    require(monitor.current().estimate == presence::Estimate::unknown,
            "stale generation");
    auto cooperative = contract();
    cooperative.sources = {"a", "b"};
    cooperative.samples_per_source = 2;
    presence::PresenceMonitor recovery(cooperative, "recovery");
    for (const auto &id : cooperative.sources) {
      for (std::uint64_t seq : {1U, 2U}) {
        auto sample = observation(seq, 1000 + static_cast<presence::Time>(seq));
        sample.source = id;
        auto step = recovery.observe(sample, 1010);
        if (step.job)
          recovery.complete(complete(*step.job, presence::Estimate::present),
                            1010);
      }
    }
    auto domain_change = observation(3, 1020);
    domain_change.metadata["room"] = "changed";
    recovery.observe(domain_change, 1020);
    require(recovery.current().estimate == presence::Estimate::unknown,
            "domain withdrawal");
    bool replaced = false;
    for (const auto &id : cooperative.sources) {
      for (std::uint64_t seq : {4U, 5U}) {
        auto sample = observation(seq, 1030 + static_cast<presence::Time>(seq));
        sample.source = id;
        auto step = recovery.observe(sample, 1040);
        if (step.job) {
          recovery.complete(complete(*step.job, presence::Estimate::present),
                            1040);
          replaced = true;
        }
      }
    }
    require(replaced &&
                recovery.current().estimate == presence::Estimate::present,
            "source-local gap recovery");
    auto c = contract();
    presence::Window w;
    w.sources.push_back({"a",
                         "b",
                         "c",
                         1,
                         1,
                         0,
                         std::numeric_limits<presence::Time>::max(),
                         std::numeric_limits<presence::Time>::max(),
                         {1}});
    require(!presence::qualify(w, c, std::numeric_limits<presence::Time>::min())
                 .accepted(),
            "extreme time rejects");
    presence::Window nonfinite;
    nonfinite.sources.push_back({"a",
                                 "b",
                                 "c",
                                 1,
                                 1,
                                 1000,
                                 1000,
                                 0,
                                 {std::numeric_limits<double>::infinity()}});
    require(!presence::qualify(nonfinite, c, 1000).accepted(),
            "nonfinite domain mean");
    nonfinite.sources.front().means = {1};
    nonfinite.sources.front().first_sequence = 2;
    require(!presence::qualify(nonfinite, c, 1000).accepted(),
            "reversed sequence domain window");
    std::cout << "core lifecycle checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

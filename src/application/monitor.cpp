#include "presence/core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace presence {
namespace {
void increment(std::uint64_t &n) {
  if (n < std::numeric_limits<std::uint64_t>::max())
    ++n;
}
} // namespace
PresenceMonitor::PresenceMonitor(Contract c, std::string runtime)
    : contract_(std::move(c)), runtime_(std::move(runtime)) {
  if (contract_.sources.empty() || contract_.sources.size() > 16 ||
      contract_.features.empty() || contract_.features.size() > 64 ||
      contract_.samples_per_source == 0 || contract_.samples_per_source > 256 ||
      contract_.max_age <= 0 || contract_.max_age > 86400000 ||
      contract_.max_window_span < 0 || contract_.max_window_span > 86400000 ||
      contract_.max_alignment < 0 || contract_.max_alignment > 86400000 ||
      contract_.max_uncertainty < 0 || contract_.max_uncertainty > 86400000 ||
      contract_.units.size() != contract_.features.size() || runtime_.empty())
    throw std::invalid_argument("invalid core contract");
  for (const auto &id : contract_.sources)
    if (!sources_.emplace(id, SourceState{}).second)
      throw std::invalid_argument("duplicate source");
}
void PresenceMonitor::clear_assembly() {
  for (auto &s : sources_)
    s.second.records.clear();
}
void PresenceMonitor::diagnostic(std::string reason, Assessment a) {
  if (a.evidence == Validity::invalid && a.evidence_reasons.empty())
    a.evidence_reasons.push_back(reason);
  increment(diagnostics_.rejected);
  diagnostics_.reason = std::move(reason);
  diagnostics_.evidence = a.evidence;
  diagnostics_.domain = a.domain;
  diagnostics_.evidence_reasons = std::move(a.evidence_reasons);
  diagnostics_.domain_reasons = std::move(a.domain_reasons);
}
void PresenceMonitor::withdraw(const std::string &reason, Assessment a) {
  if (current_.estimate != Estimate::unknown)
    history_ = current_;
  current_.estimate = Estimate::unknown;
  current_.reasons = {reason};
  current_.assessment = std::move(a);
}
void PresenceMonitor::invalidate(const std::string &source,
                                 const std::string &reason,
                                 const Observation &o) {
  auto relevant = [&](const Window &w) {
    return std::any_of(
        w.sources.begin(), w.sources.end(), [&](const SourceWindow &s) {
          return s.source == source && (s.boot_epoch != o.boot_epoch ||
                                        s.config_epoch != o.config_epoch ||
                                        reason == "domain_ineligible");
        });
  };
  bool affected = (current_.window && relevant(*current_.window)) ||
                  (running_ && relevant(running_->window)) ||
                  (pending_ && relevant(*pending_));
  if (!affected)
    return;
  increment(generation_);
  pending_.reset();
  clear_assembly();
  if (current_.window && relevant(*current_.window)) {
    Assessment a = current_.assessment;
    if (reason == "domain_ineligible") {
      a.domain = Eligibility::ineligible;
      a.domain_reasons = {reason};
    } else {
      a.evidence = Validity::invalid;
      a.evidence_reasons = {reason};
    }
    withdraw(reason, std::move(a));
  }
}
Step PresenceMonitor::advance(Time now, bool changed) {
  if (current_.window && current_.assessment.evidence == Validity::valid) {
    auto a = qualify(*current_.window, contract_, now);
    if (!a.accepted()) {
      a.domain = current_.assessment.domain;
      a.domain_reasons = current_.assessment.domain_reasons;
      withdraw("evidence_expired", std::move(a));
      changed = true;
    }
  }
  Step step;
  step.publish = changed;
  if (!stopped_ && !running_ && pending_) {
    auto a = qualify(*pending_, contract_, now);
    if (a.accepted() &&
        next_job_ != std::numeric_limits<std::uint64_t>::max()) {
      running_ = Job{runtime_, generation_, next_job_++, std::move(*pending_)};
      step.job = running_;
    } else {
      diagnostic("pending_window_rejected", std::move(a));
      step.publish = true;
    }
    pending_.reset();
  }
  return step;
}
Step PresenceMonitor::reject(std::string reason, Time now) {
  clear_assembly();
  Assessment a;
  a.evidence = Validity::invalid;
  a.domain = Eligibility::unknown;
  a.evidence_reasons = {reason};
  diagnostic(std::move(reason), std::move(a));
  return advance(now, true);
}
Step PresenceMonitor::tick(Time now) { return advance(now, false); }
Step PresenceMonitor::observe(Observation o, Time now) {
  if (stopped_)
    return {};
  if (now < 0)
    return reject("clock_discontinuity", now);
  auto found = sources_.find(o.source);
  if (found == sources_.end())
    return reject("unsupported_source", now);
  if (!o.integrity)
    return reject("integrity_failed", now);
  if (!o.mapping_valid || o.capture < 0 || o.uncertainty < 0 ||
      o.uncertainty > contract_.max_uncertainty ||
      (o.capture > now && o.capture - now > o.uncertainty) ||
      o.capture < o.uncertainty ||
      o.capture > std::numeric_limits<Time>::max() - contract_.max_age)
    return reject("time_mapping_unknown", now);
  auto &state = found->second;
  bool changed = false;
  bool epoch = state.last && (state.last->boot_epoch != o.boot_epoch ||
                              state.last->config_epoch != o.config_epoch);
  if (state.last && o.capture < state.last->capture)
    return reject("sequence_rejected", now);
  if (epoch) {
    invalidate(o.source, "source_epoch_changed", o);
    clear_assembly();
    changed = true;
  } else if (state.last && (o.sequence <= state.last->sequence ||
                            o.capture < state.last->capture))
    return reject("sequence_rejected", now);
  else if (state.last &&
           (state.last->sequence == std::numeric_limits<std::uint64_t>::max() ||
            o.sequence != state.last->sequence + 1)) {
    state.records.clear();
    diagnostic("sequence_gap");
    changed = true;
  }
  if (o.features != contract_.features || o.units != contract_.units ||
      o.pipeline_hash != contract_.pipeline_hash ||
      o.values.size() != contract_.features.size() ||
      !std::all_of(o.values.begin(), o.values.end(),
                   [](double x) { return std::isfinite(x); })) {
    state.last = std::move(o);
    return reject("input_contract_mismatch", now);
  }
  Assessment domain;
  for (const auto &item : contract_.metadata) {
    auto value = o.metadata.find(item.first);
    if (value == o.metadata.end()) {
      if (domain.domain != Eligibility::ineligible)
        domain.domain = Eligibility::unknown;
      domain.domain_reasons.push_back("domain_unknown");
    } else if (value->second != item.second) {
      domain.domain = Eligibility::ineligible;
      domain.domain_reasons.push_back("domain_ineligible");
    }
  }
  if (domain.domain == Eligibility::ineligible) {
    invalidate(o.source, "domain_ineligible", o);
    changed = true;
  }
  state.last = o;
  if (!domain.accepted()) {
    clear_assembly();
    diagnostic(domain.domain == Eligibility::unknown ? "domain_unknown"
                                                     : "domain_ineligible",
               std::move(domain));
    return advance(now, true);
  }
  if (state.records.size() == contract_.samples_per_source) {
    clear_assembly();
    diagnostic("capacity_exceeded");
    changed = true;
  }
  state.records.push_back(std::move(o));
  if (!std::all_of(sources_.begin(), sources_.end(), [&](const auto &s) {
        return s.second.records.size() == contract_.samples_per_source;
      }))
    return advance(now, changed);
  if (next_window_ == std::numeric_limits<std::uint64_t>::max())
    return reject("capacity_exceeded", now);
  Window w;
  w.id = next_window_++;
  w.deadline = std::numeric_limits<Time>::max();
  for (const auto &id : contract_.sources) {
    const auto &records = sources_.at(id).records;
    const auto &first = records.front();
    const auto &last = records.back();
    SourceWindow s{id,
                   first.boot_epoch,
                   first.config_epoch,
                   first.sequence,
                   last.sequence,
                   first.capture,
                   last.capture,
                   0,
                   std::vector<double>(contract_.features.size(), 0.0)};
    for (const auto &record : records) {
      s.uncertainty = std::max(s.uncertainty, record.uncertainty);
      for (std::size_t i = 0; i < s.means.size(); ++i)
        s.means[i] += record.values[i] / static_cast<double>(records.size());
    }
    w.deadline =
        std::min(w.deadline, s.end - s.uncertainty + contract_.max_age);
    w.sources.push_back(std::move(s));
  }
  clear_assembly();
  auto a = qualify(w, contract_, now);
  if (!a.accepted()) {
    diagnostic("window_rejected", std::move(a));
    return advance(now, true);
  }
  if (pending_) {
    increment(diagnostics_.skipped);
    diagnostics_.reason = "window_skipped";
    changed = true;
  }
  pending_ = std::move(w);
  return advance(now, changed);
}
Step PresenceMonitor::complete(const Outcome &o, Time now) {
  if (!running_ || o.runtime != runtime_ ||
      o.generation != running_->generation || o.job_id != running_->id ||
      o.window_id != running_->window.id) {
    increment(diagnostics_.stale);
    diagnostics_.reason = "stale_completion";
    return advance(now, true);
  }
  Job finished = std::move(*running_);
  running_.reset();
  if (stopped_ || o.generation != generation_) {
    increment(diagnostics_.stale);
    diagnostics_.reason = "stale_completion";
    return advance(now, true);
  }
  auto a = qualify(finished.window, contract_, now);
  if (!o.success || !a.accepted()) {
    if (!o.success) {
      a.evidence = Validity::invalid;
      a.evidence_reasons.push_back("inference_failed");
    }
    diagnostic("completion_rejected", std::move(a));
    return advance(now, true);
  }
  if (current_.estimate != Estimate::unknown)
    history_ = current_;
  current_.estimate = o.estimate;
  current_.window = std::move(finished.window);
  current_.assessment = std::move(a);
  current_.reasons = o.estimate == Estimate::unknown
                         ? std::vector<std::string>{"model_abstained"}
                         : std::vector<std::string>{};
  return advance(now, true);
}
Step PresenceMonitor::fail_startup(Time now) {
  stopped_ = true;
  withdraw("configuration_invalid",
           {Validity::unknown, Eligibility::unknown, {}, {}});
  return advance(now, true);
}
Step PresenceMonitor::stop(Time now) {
  stopped_ = true;
  pending_.reset();
  increment(generation_);
  withdraw("service_stopped",
           {Validity::unknown, Eligibility::unknown, {}, {}});
  return advance(now, true);
}
} // namespace presence

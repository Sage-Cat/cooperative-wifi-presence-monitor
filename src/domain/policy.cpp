#include "presence/domain.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace presence {
const char *name(Estimate v) {
  return v == Estimate::present  ? "present"
         : v == Estimate::absent ? "absent"
                                 : "unknown";
}
const char *name(Validity v) {
  return v == Validity::valid     ? "valid"
         : v == Validity::invalid ? "invalid"
                                  : "unknown";
}
const char *name(Eligibility v) {
  return v == Eligibility::eligible     ? "eligible"
         : v == Eligibility::ineligible ? "ineligible"
                                        : "unknown";
}
Assessment qualify(const Window &w, const Contract &c, Time now) {
  Assessment a;
  auto bad = [&](const char *reason) {
    a.evidence = Validity::invalid;
    a.evidence_reasons.emplace_back(reason);
  };
  if (w.sources.size() != c.sources.size())
    bad("required_source_unavailable");
  if (c.sources.empty() || c.sources.size() > 16 || c.features.empty() ||
      c.features.size() > 64 || c.samples_per_source == 0 ||
      c.samples_per_source > 256 || w.sources.size() > 16) {
    bad("input_contract_mismatch");
    return a;
  }
  if (now < 0 || c.max_age <= 0 || c.max_age > 86400000 ||
      c.max_uncertainty < 0 || c.max_window_span < 0 || c.max_alignment < 0) {
    bad("time_mapping_unknown");
    return a;
  }
  Time earliest = std::numeric_limits<Time>::max(), latest = 0;
  for (std::size_t i = 0; i < w.sources.size(); ++i) {
    const auto &s = w.sources[i];
    if (i >= c.sources.size() || s.source != c.sources[i] ||
        s.means.size() != c.features.size())
      bad("input_contract_mismatch");
    if (s.boot_epoch.empty() || s.config_epoch.empty() ||
        s.last_sequence < s.first_sequence ||
        s.last_sequence - s.first_sequence != c.samples_per_source - 1 ||
        !std::all_of(s.means.begin(), s.means.end(),
                     [](double v) { return std::isfinite(v); }))
      bad("input_contract_mismatch");
    if (s.uncertainty < 0 || s.uncertainty > c.max_uncertainty || s.end < 0 ||
        s.start < 0 || s.start > s.end) {
      bad("time_mapping_unknown");
      continue;
    }
    if (s.end - s.start > c.max_window_span)
      bad("time_mapping_unknown");
    if (s.end > now && s.end - now > s.uncertainty)
      bad("clock_discontinuity");
    if (s.end > std::numeric_limits<Time>::max() - c.max_age ||
        s.end < s.uncertainty ||
        s.end > std::numeric_limits<Time>::max() - s.uncertainty) {
      bad("time_mapping_unknown");
      continue;
    }
    if (now > s.end - s.uncertainty + c.max_age)
      bad("evidence_expired");
    earliest = std::min(earliest, s.end - s.uncertainty);
    latest = std::max(latest, s.end + s.uncertainty);
  }
  if (w.sources.size() > 1 && earliest <= latest &&
      latest - earliest > c.max_alignment)
    bad("time_alignment_exceeded");
  return a;
}
} // namespace presence

#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace presence {
using Time = std::int64_t;
enum class Estimate { unknown, absent, present };
enum class Validity { unknown, invalid, valid };
enum class Eligibility { unknown, ineligible, eligible };
struct Contract {
  std::vector<std::string> sources, features, units;
  std::map<std::string, std::string> metadata;
  std::string pipeline_hash;
  std::size_t samples_per_source = 1;
  Time max_window_span = 1000, max_alignment = 100, max_age = 1000,
       max_uncertainty = 10;
};
struct Observation {
  std::string source, boot_epoch, config_epoch, pipeline_hash;
  std::uint64_t sequence = 0;
  Time capture = 0, uncertainty = 0;
  bool integrity = true, mapping_valid = true;
  std::vector<std::string> features, units;
  std::vector<double> values;
  std::map<std::string, std::string> metadata;
};
struct SourceWindow {
  std::string source, boot_epoch, config_epoch;
  std::uint64_t first_sequence = 0, last_sequence = 0;
  Time start = 0, end = 0, uncertainty = 0;
  std::vector<double> means;
};
struct Window {
  std::uint64_t id = 0;
  std::vector<SourceWindow> sources;
  Time deadline = 0;
};
struct Assessment {
  Validity evidence = Validity::valid;
  Eligibility domain = Eligibility::eligible;
  std::vector<std::string> evidence_reasons, domain_reasons;
  bool accepted() const {
    return evidence == Validity::valid && domain == Eligibility::eligible;
  }
};
Assessment qualify(const Window &, const Contract &, Time now);
const char *name(Estimate);
const char *name(Validity);
const char *name(Eligibility);
} // namespace presence

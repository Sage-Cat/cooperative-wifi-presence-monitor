#include "presence/adapters.hpp"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <openssl/evp.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace presence {
namespace {
[[noreturn]] void bad(const char *why) { throw std::runtime_error(why); }
void keys(const Json &j, std::initializer_list<const char *> required,
          std::initializer_list<const char *> optional = {}) {
  if (!j.is_object())
    bad("object_required");
  std::set<std::string> allowed;
  for (auto k : required) {
    allowed.insert(k);
    if (!j.contains(k))
      bad("missing_required_field");
  }
  for (auto k : optional)
    allowed.insert(k);
  for (auto it = j.begin(); it != j.end(); ++it)
    if (!allowed.count(it.key()))
      bad("unknown_field");
}
std::string str(const Json &j, std::size_t max = 128) {
  if (!j.is_string())
    bad("string_required");
  auto s = j.get<std::string>();
  if (s.empty() || s.size() > max || s.find('\0') != std::string::npos)
    bad("invalid_string");
  return s;
}
std::uint64_t uint(const Json &j, std::uint64_t max) {
  if (!j.is_number_unsigned() &&
      !(j.is_number_integer() && j.get<std::int64_t>() >= 0))
    bad("unsigned_integer_required");
  auto v = j.get<std::uint64_t>();
  if (v > max)
    bad("integer_out_of_range");
  return v;
}
Time time(const Json &j) { return static_cast<Time>(uint(j, 86400000)); }
double number(const Json &j) {
  if (!j.is_number())
    bad("number_required");
  double n = j.get<double>();
  if (!std::isfinite(n))
    bad("finite_number_required");
  return n;
}
std::vector<std::string> strings(const Json &j, std::size_t max,
                                 bool unique = false) {
  if (!j.is_array() || j.empty() || j.size() > max)
    bad("array_size_invalid");
  std::vector<std::string> v;
  std::set<std::string> seen;
  for (const auto &item : j) {
    auto s = str(item);
    if (unique && !seen.insert(s).second)
      bad("duplicate_identifier");
    v.push_back(std::move(s));
  }
  return v;
}
std::map<std::string, std::string> metadata(const Json &j) {
  if (!j.is_object() || j.size() > 32)
    bad("metadata_invalid");
  std::map<std::string, std::string> m;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (it.key().empty() || it.key().size() > 128)
      bad("metadata_invalid");
    m.emplace(it.key(), str(it.value()));
  }
  return m;
}
std::string hash(const Json &j) {
  auto s = str(j, 64);
  if (s.size() != 64 || !std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      }))
    bad("sha256_invalid");
  return s;
}
void schema(const Json &j, const char *expected) {
  if (str(j.at("schema_version")) != expected)
    bad("schema_unsupported");
}
std::string sibling(const std::string &manifest, const Json &j) {
  auto name = str(j, 255);
  if (name == "." || name == ".." || name.find('/') != std::string::npos)
    bad("bundle_path_invalid");
  return (std::filesystem::path(manifest).parent_path() / name).string();
}
} // namespace
Json parse_json(const std::string &input) {
  std::vector<std::set<std::string>> objects;
  auto callback = [&](int depth, Json::parse_event_t event, Json &parsed) {
    if (depth > 16)
      bad("json_depth_exceeded");
    if (event == Json::parse_event_t::object_start)
      objects.emplace_back();
    else if (event == Json::parse_event_t::key) {
      if (objects.empty() ||
          !objects.back().insert(parsed.get<std::string>()).second)
        bad("duplicate_json_key");
    } else if (event == Json::parse_event_t::object_end)
      objects.pop_back();
    return true;
  };
  return Json::parse(input, callback, true, false);
}
void check_absolute(const std::string &value) {
  std::filesystem::path path(value);
  if (value.empty() || value.size() > 4096 ||
      value.find('\0') != std::string::npos || !path.is_absolute() ||
      path.lexically_normal() != path || path.filename().empty())
    bad("path_invalid");
  std::filesystem::path current = "/";
  for (const auto &part : path.relative_path()) {
    current /= part;
    struct stat s {};
    if (lstat(current.c_str(), &s) == 0) {
      if (S_ISLNK(s.st_mode))
        bad("symlink_path_rejected");
    } else if (errno != ENOENT)
      bad("path_stat_failed");
  }
}
std::string read_regular(const std::string &path, std::size_t limit) {
  check_absolute(path);
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    bad("file_open_failed");
  struct Close {
    int fd;
    ~Close() { close(fd); }
  } close_fd{fd};
  struct stat s {};
  if (fstat(fd, &s) != 0 || !S_ISREG(s.st_mode) || s.st_size < 0 ||
      static_cast<std::uint64_t>(s.st_size) > limit)
    bad("regular_file_required");
  std::string data;
  data.reserve(static_cast<std::size_t>(s.st_size));
  char buffer[4096];
  for (;;) {
    ssize_t n = read(fd, buffer, sizeof buffer);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      bad("file_read_failed");
    }
    if (n == 0)
      break;
    if (data.size() + static_cast<std::size_t>(n) > limit)
      bad("file_size_exceeded");
    data.append(buffer, static_cast<std::size_t>(n));
  }
  return data;
}
std::string digest(const std::string &data) {
  unsigned char bytes[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  if (EVP_Digest(data.data(), data.size(), bytes, &size, EVP_sha256(),
                 nullptr) != 1)
    bad("sha256_failed");
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < size; ++i)
    out << std::setw(2) << static_cast<unsigned>(bytes[i]);
  return out.str();
}
Config load_config(const std::string &path) {
  auto raw = read_regular(path, 65536);
  auto j = parse_json(raw);
  keys(j,
       {"schema_version", "zone_id", "input_path", "output_path",
        "manifest_path", "manifest_sha256"},
       {"max_frame_bytes"});
  schema(j, "wifi-presence-config/1");
  Config c;
  c.zone = str(j.at("zone_id"));
  c.input = str(j.at("input_path"), 4096);
  c.output = str(j.at("output_path"), 4096);
  c.manifest = str(j.at("manifest_path"), 4096);
  c.manifest_hash = hash(j.at("manifest_sha256"));
  c.hash = digest(raw);
  if (j.contains("max_frame_bytes"))
    c.max_frame = uint(j.at("max_frame_bytes"), 65536);
  if (c.max_frame < 256)
    bad("frame_limit_invalid");
  check_absolute(c.output);
  check_absolute(c.manifest);
  if (c.input != "-")
    check_absolute(c.input);
  std::set<std::string> paths{path, c.output, c.output + ".lock", c.manifest};
  if (paths.size() != 4 || (c.input != "-" && !paths.insert(c.input).second))
    bad("path_overlap");
  return c;
}
Bundle load_bundle(const Config &c, bool allow_synthetic) {
  auto raw = read_regular(c.manifest);
  if (digest(raw) != c.manifest_hash)
    bad("manifest_integrity_failed");
  auto j = parse_json(raw);
  keys(j, {"schema_version",      "model_id",           "pipeline_id",
           "profile_id",          "provenance_kind",    "task",
           "stationary_presence", "negative_class",     "source_ids",
           "feature_names",       "feature_units",      "required_metadata",
           "samples_per_source",  "max_window_span_ms", "max_alignment_ms",
           "max_age_ms",          "max_uncertainty_ms", "artifact_path",
           "artifact_sha256",     "pipeline_path",      "pipeline_sha256"},
       {"review_reference"});
  schema(j, "wifi-presence-model/1");
  Bundle b;
  b.model_id = str(j.at("model_id"));
  b.pipeline_id = str(j.at("pipeline_id"));
  b.profile_id = str(j.at("profile_id"));
  b.kind = str(j.at("provenance_kind"));
  b.manifest_hash = c.manifest_hash;
  if (str(j.at("task")) != "zone-presence" ||
      !j.at("stationary_presence").is_boolean() ||
      !j.at("stationary_presence").get<bool>() ||
      str(j.at("negative_class")) != "absence")
    bad("presence_semantics_invalid");
  if (b.kind == "synthetic") {
    if (!allow_synthetic)
      bad("synthetic_opt_in_required");
  } else if (b.kind == "reviewed-presence")
    b.review = str(j.at("review_reference"), 1024);
  else
    bad("provenance_kind_invalid");
  auto &contract = b.contract;
  contract.sources = strings(j.at("source_ids"), 16, true);
  contract.features = strings(j.at("feature_names"), 64, true);
  contract.units = strings(j.at("feature_units"), 64);
  if (contract.units.size() != contract.features.size())
    bad("feature_contract_invalid");
  contract.metadata = metadata(j.at("required_metadata"));
  contract.samples_per_source = uint(j.at("samples_per_source"), 256);
  if (contract.samples_per_source == 0)
    bad("sample_count_invalid");
  contract.max_window_span = time(j.at("max_window_span_ms"));
  contract.max_alignment = time(j.at("max_alignment_ms"));
  contract.max_age = time(j.at("max_age_ms"));
  contract.max_uncertainty = time(j.at("max_uncertainty_ms"));
  if (contract.max_age == 0)
    bad("maximum_age_invalid");
  contract.pipeline_hash = hash(j.at("pipeline_sha256"));
  b.artifact_hash = hash(j.at("artifact_sha256"));
  auto artifact_path = sibling(c.manifest, j.at("artifact_path")),
       pipeline_path = sibling(c.manifest, j.at("pipeline_path"));
  std::set<std::string> paths{artifact_path, pipeline_path, c.manifest,
                              c.output, c.output + ".lock"};
  if (paths.size() != 5 || paths.count(c.input))
    bad("bundle_path_overlap");
  auto artifact = read_regular(artifact_path),
       pipeline = read_regular(pipeline_path);
  if (digest(artifact) != b.artifact_hash ||
      digest(pipeline) != contract.pipeline_hash)
    bad("artifact_integrity_failed");
  auto p = parse_json(pipeline);
  keys(p, {"schema_version", "aggregation", "feature_names", "feature_units"});
  schema(p, "wifi-presence-pipeline/1");
  if (str(p.at("aggregation")) != "per-source-mean" ||
      strings(p.at("feature_names"), 64, true) != contract.features ||
      strings(p.at("feature_units"), 64) != contract.units)
    bad("pipeline_contract_invalid");
  auto a = parse_json(artifact);
  keys(a, {"schema_version", "weights", "bias", "absent_threshold",
           "present_threshold"});
  schema(a, "wifi-presence-linear/1");
  if (!a.at("weights").is_array() ||
      a.at("weights").size() !=
          contract.sources.size() * contract.features.size())
    bad("weights_invalid");
  for (auto &w : a.at("weights"))
    b.weights.push_back(number(w));
  b.bias = number(a.at("bias"));
  b.absent = number(a.at("absent_threshold"));
  b.present = number(a.at("present_threshold"));
  if (b.absent >= b.present)
    bad("thresholds_invalid");
  return b;
}
Observation decode(const std::string &raw, const std::string &boot_id) {
  auto j = parse_json(raw);
  keys(j, {"schema_version", "source_id", "boot_epoch", "config_epoch",
           "sequence", "capture_ms", "uncertainty_ms", "clock_boot_id",
           "pipeline_sha256", "feature_names", "feature_units", "features",
           "metadata", "integrity", "parser_status"});
  schema(j, "wifi-presence-observation/1");
  Observation o;
  o.source = str(j.at("source_id"));
  o.boot_epoch = str(j.at("boot_epoch"));
  o.config_epoch = str(j.at("config_epoch"));
  o.sequence = uint(j.at("sequence"), UINT64_MAX);
  o.capture = static_cast<Time>(uint(j.at("capture_ms"), INT64_MAX - 86400000));
  o.uncertainty = time(j.at("uncertainty_ms"));
  o.mapping_valid = str(j.at("clock_boot_id")) == boot_id;
  o.pipeline_hash = hash(j.at("pipeline_sha256"));
  o.features = strings(j.at("feature_names"), 64, true);
  o.units = strings(j.at("feature_units"), 64);
  o.metadata = metadata(j.at("metadata"));
  if (!j.at("integrity").is_boolean())
    bad("integrity_invalid");
  o.integrity =
      j.at("integrity").get<bool>() && str(j.at("parser_status")) == "ok";
  if (!j.at("features").is_array() ||
      j.at("features").size() != o.features.size() ||
      o.units.size() != o.features.size())
    bad("features_invalid");
  for (auto &v : j.at("features"))
    o.values.push_back(number(v));
  return o;
}
Outcome infer(const Job &job, const Bundle &bundle) {
  Outcome o{job.runtime,   job.generation,    job.id,
            job.window.id, Estimate::unknown, true};
  double score = bundle.bias;
  std::size_t index = 0;
  for (const auto &source : job.window.sources)
    for (double v : source.means) {
      if (index >= bundle.weights.size()) {
        o.success = false;
        return o;
      }
      score += v * bundle.weights[index++];
    }
  if (index != bundle.weights.size() || !std::isfinite(score)) {
    o.success = false;
    return o;
  }
  if (score <= bundle.absent)
    o.estimate = Estimate::absent;
  else if (score >= bundle.present)
    o.estimate = Estimate::present;
  return o;
}
} // namespace presence

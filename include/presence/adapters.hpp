#pragma once
#include "presence/core.hpp"
#include <nlohmann/json.hpp>
#include <string>
namespace presence {
using Json = nlohmann::json;
struct Config {
  std::string zone, input, output, manifest, manifest_hash, hash, clock_domain;
  std::size_t max_frame = 16384;
};
struct Bundle {
  Contract contract;
  std::string model_id, pipeline_id, profile_id, manifest_hash, artifact_hash,
      kind, review;
  std::vector<double> weights;
  double bias = 0, absent = 0, present = 1;
};
Json parse_json(const std::string &);
Config load_config(const std::string &);
Bundle load_bundle(const Config &, bool allow_synthetic);
Observation decode(const std::string &, const std::string &boot_id);
Outcome infer(const Job &, const Bundle &);
Json encode(const PresenceMonitor &, const Config &, const Bundle *, Time,
            std::uint64_t);
std::string read_regular(const std::string &, std::size_t limit = 1048576);
void check_absolute(const std::string &);
std::string digest(const std::string &);
class AtomicWriter {
public:
  explicit AtomicWriter(const std::string &, const std::string &runtime);
  ~AtomicWriter();
  AtomicWriter(const AtomicWriter &) = delete;
  AtomicWriter &operator=(const AtomicWriter &) = delete;
  void write(const Json &);

private:
  int directory_ = -1, lock_ = -1;
  std::string filename_, temporary_;
};
} // namespace presence

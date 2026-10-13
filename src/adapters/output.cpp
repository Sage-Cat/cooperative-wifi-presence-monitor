#include "presence/adapters.hpp"
#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef PRESENCE_BUILD_ID
#define PRESENCE_BUILD_ID "0.1.0"
#endif
namespace presence {
namespace {
Json window(const std::optional<Window> &w) {
  if (!w)
    return nullptr;
  Json result = Json::array();
  for (const auto &s : w->sources)
    result.push_back({{"source_id", s.source},
                      {"boot_epoch", s.boot_epoch},
                      {"config_epoch", s.config_epoch},
                      {"first_sequence", s.first_sequence},
                      {"last_sequence", s.last_sequence},
                      {"capture_start_ms", s.start},
                      {"capture_end_ms", s.end},
                      {"uncertainty_ms", s.uncertainty},
                      {"mapping_status", "verified-local-boot"},
                      {"time_basis", "CLOCK_BOOTTIME"}});
  return result;
}
void regular_at(int directory, const std::string &name, bool missing) {
  struct stat s {};
  if (fstatat(directory, name.c_str(), &s, AT_SYMLINK_NOFOLLOW) != 0) {
    if (missing && errno == ENOENT)
      return;
    throw std::runtime_error("output_stat_failed");
  }
  if (!S_ISREG(s.st_mode) || s.st_uid != geteuid() || s.st_nlink != 1 ||
      (s.st_mode & 0077) != 0)
    throw std::runtime_error("unsafe_output_file");
}
} // namespace
Json encode(const PresenceMonitor &monitor, const Config &c, const Bundle *b,
            Time now, std::uint64_t serial) {
  const auto &current = monitor.current();
  const auto &d = monitor.diagnostics();
  Json age = nullptr, deadline = nullptr;
  if (current.window) {
    Time oldest = 0;
    for (const auto &s : current.window->sources)
      oldest = std::max(oldest, now - s.end + s.uncertainty);
    age = {{"upper_bound_ms", oldest},
           {"maximum_ms", b ? b->contract.max_age : 0},
           {"as_of_ms", now},
           {"time_basis", "CLOCK_BOOTTIME"}};
    deadline = current.window->deadline;
  }
  Json history = nullptr;
  if (monitor.last_known()) {
    const auto &h = *monitor.last_known();
    history = {
        {"estimate", name(h.estimate)},
        {"evidence_validity",
         {{"state", name(h.assessment.evidence)},
          {"reasons", h.assessment.evidence_reasons}}},
        {"domain_eligibility",
         {{"state", name(h.assessment.domain)},
          {"reasons", h.assessment.domain_reasons},
          {"profile_id", b ? Json(b->profile_id) : Json(nullptr)},
          {"profile_sha256", b ? Json(b->manifest_hash) : Json(nullptr)}}},
        {"evidence_window", window(h.window)},
        {"valid_until", h.window ? Json(h.window->deadline) : Json(nullptr)}};
  }
  return {{"schema_version", "wifi-presence-result/1"},
          {"result_id", monitor.runtime() + "-" + std::to_string(serial)},
          {"zone_id", c.zone},
          {"estimate", name(current.estimate)},
          {"decision_reasons", current.reasons},
          {"evidence_validity",
           {{"state", name(current.assessment.evidence)},
            {"reasons", current.assessment.evidence_reasons}}},
          {"domain_eligibility",
           {{"state", name(current.assessment.domain)},
            {"reasons", current.assessment.domain_reasons},
            {"profile_id", b ? Json(b->profile_id) : Json(nullptr)},
            {"profile_sha256", b ? Json(b->manifest_hash) : Json(nullptr)}}},
          {"evidence_window", window(current.window)},
          {"age", age},
          {"valid_until", deadline},
          {"provenance",
           {{"source_schema", "wifi-presence-observation/1"},
            {"model_id", b ? Json(b->model_id) : Json(nullptr)},
            {"model_sha256", b ? Json(b->artifact_hash) : Json(nullptr)},
            {"manifest_sha256", c.manifest_hash},
            {"pipeline_id", b ? Json(b->pipeline_id) : Json(nullptr)},
            {"pipeline_sha256",
             b ? Json(b->contract.pipeline_hash) : Json(nullptr)},
            {"config_sha256", c.hash},
            {"software_build_id", PRESENCE_BUILD_ID},
            {"runtime_session", monitor.runtime()},
            {"generation", monitor.generation()},
            {"kind", b ? Json(b->kind) : Json(nullptr)},
            {"synthetic", b ? b->kind == "synthetic" : false},
            {"review_reference", b ? Json(b->review) : Json(nullptr)},
            {"clock_domain",
             c.clock_domain.empty() ? Json(nullptr) : Json(c.clock_domain)},
            {"time_basis", "CLOCK_BOOTTIME"}}},
          {"diagnostics",
           {{"rejected_candidates", d.rejected},
            {"skipped_windows", d.skipped},
            {"stale_completions", d.stale},
            {"counter_semantics", "saturating-uint64"},
            {"latest_reason", d.reason},
            {"evidence_validity",
             {{"state", name(d.evidence)}, {"reasons", d.evidence_reasons}}},
            {"domain_eligibility",
             {{"state", name(d.domain)}, {"reasons", d.domain_reasons}}}}},
          {"last_known", history}};
}
AtomicWriter::AtomicWriter(const std::string &path,
                           const std::string &runtime) {
  check_absolute(path);
  auto p = std::filesystem::path(path);
  filename_ = p.filename().string();
  temporary_ = "." + filename_ + "." + runtime + ".tmp";
  directory_ = open(p.parent_path().c_str(),
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (directory_ < 0)
    throw std::runtime_error("output_directory_unavailable");
  try {
    struct stat s {};
    if (fstat(directory_, &s) != 0 || s.st_uid != geteuid() ||
        (s.st_mode & 0022) != 0)
      throw std::runtime_error("output_directory_not_private");
    regular_at(directory_, filename_, true);
    struct stat existing {};
    if (fstatat(directory_, filename_.c_str(), &existing,
                AT_SYMLINK_NOFOLLOW) == 0) {
      auto old = parse_json(read_regular(path, 262144));
      if (!old.is_object() || old.value("schema_version", std::string{}) !=
                                  "wifi-presence-result/1")
        throw std::runtime_error("output_collision");
    }
    auto lockname = filename_ + ".lock";
    lock_ = openat(directory_, lockname.c_str(),
                   O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_ < 0)
      throw std::runtime_error("writer_lock_unavailable");
    regular_at(directory_, lockname, false);
    if (flock(lock_, LOCK_EX | LOCK_NB) != 0)
      throw std::runtime_error("writer_already_running");
  } catch (...) {
    if (lock_ >= 0)
      close(lock_);
    close(directory_);
    lock_ = directory_ = -1;
    throw;
  }
}
AtomicWriter::~AtomicWriter() {
  if (directory_ >= 0) {
    unlinkat(directory_, temporary_.c_str(), 0);
    close(directory_);
  }
  if (lock_ >= 0)
    close(lock_);
}
void AtomicWriter::write(const Json &value) {
  auto bytes = value.dump();
  if (bytes.size() > 262144)
    throw std::runtime_error("result_limit_exceeded");
  bytes.push_back('\n');
  int fd = openat(directory_, temporary_.c_str(),
                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0)
    throw std::runtime_error("temporary_output_failed");
  try {
    std::size_t done = 0;
    while (done < bytes.size()) {
      ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error("output_write_failed");
      done += static_cast<std::size_t>(n);
    }
    if (fsync(fd) != 0)
      throw std::runtime_error("output_sync_failed");
    close(fd);
    fd = -1;
    regular_at(directory_, filename_, true);
    if (renameat(directory_, temporary_.c_str(), directory_,
                 filename_.c_str()) != 0 ||
        fsync(directory_) != 0)
      throw std::runtime_error("output_replace_failed");
  } catch (...) {
    if (fd >= 0)
      close(fd);
    unlinkat(directory_, temporary_.c_str(), 0);
    throw;
  }
}
} // namespace presence

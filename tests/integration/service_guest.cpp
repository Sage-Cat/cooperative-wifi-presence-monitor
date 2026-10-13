// Real Linux process/FIFO/filesystem acceptance, with synthetic fixtures only.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
std::string executable;
bool actual_namespace_check = false;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::int64_t now() {
  timespec t{};
  if (clock_gettime(CLOCK_BOOTTIME, &t))
    throw std::runtime_error("CLOCK_BOOTTIME unavailable");
  return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
std::string read(const fs::path &path) {
  std::ifstream in(path);
  if (!in)
    throw std::runtime_error("read failed");
  return {std::istreambuf_iterator<char>(in), {}};
}
void write(const fs::path &path, const Json &value) {
  std::ofstream out(path);
  out << value.dump() << '\n';
  check(bool(out), "fixture write failed");
}
std::string digest(const std::string &bytes) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(),
         digest);
  std::ostringstream out;
  for (auto byte : digest)
    out << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
  return out.str();
}
std::string hash(const fs::path &path) { return digest(read(path)); }
std::string clock_domain() {
  auto boot = read("/proc/sys/kernel/random/boot_id");
  while (!boot.empty() && (boot.back() == '\n' || boot.back() == '\r'))
    boot.pop_back();
  char identity[256];
  const auto length = readlink("/proc/self/ns/time", identity, sizeof identity);
  std::string time_namespace;
  if (length < 0) {
    if (errno != ENOENT)
      throw std::runtime_error("time namespace identity unavailable");
    time_namespace = "global";
  } else {
    check(static_cast<std::size_t>(length) < sizeof identity,
          "time namespace identity too long");
    time_namespace.assign(identity, static_cast<std::size_t>(length));
  }
  return digest("wifi-presence-clock/1:" + boot + ":" + time_namespace);
}
class Service {
public:
  fs::path root;
  Json manifest, configuration;
  pid_t pid = -1;
  int pipe = -1;
  std::string boot;
  Service(int age = 650, bool synthetic = true, bool corrupt = false) {
    char location[] = "/tmp/presence-guest-XXXXXX";
    auto directory = mkdtemp(location);
    check(directory, "runtime directory failed");
    root = directory;
    boot = read("/proc/sys/kernel/random/boot_id");
    while (!boot.empty() && (boot.back() == '\n' || boot.back() == '\r'))
      boot.pop_back();
    write(root / "linear.json", {{"schema_version", "wifi-presence-linear/1"},
                                 {"weights", {1.0, 1.0}},
                                 {"bias", 0.0},
                                 {"absent_threshold", -0.5},
                                 {"present_threshold", 0.5}});
    write(root / "pipeline.json",
          {{"schema_version", "wifi-presence-pipeline/1"},
           {"aggregation", "per-source-mean"},
           {"feature_names", {"synthetic_level"}},
           {"feature_units", {"unitless"}}});
    manifest = {{"schema_version", "wifi-presence-model/1"},
                {"model_id", "synthetic-linear-test"},
                {"pipeline_id", "synthetic-mean-v1"},
                {"profile_id", "synthetic-profile"},
                {"provenance_kind", "synthetic"},
                {"task", "zone-presence"},
                {"stationary_presence", true},
                {"negative_class", "absence"},
                {"source_ids", {"rx-a", "rx-b"}},
                {"feature_names", {"synthetic_level"}},
                {"feature_units", {"unitless"}},
                {"required_metadata", {{"scenario", "synthetic"}}},
                {"samples_per_source", 2},
                {"max_window_span_ms", 200},
                {"max_alignment_ms", 50},
                {"max_age_ms", age},
                {"max_uncertainty_ms", 20},
                {"artifact_path", "linear.json"},
                {"artifact_sha256", hash(root / "linear.json")},
                {"pipeline_path", "pipeline.json"},
                {"pipeline_sha256", hash(root / "pipeline.json")}};
    write(root / "manifest.json", manifest);
    configuration = {{"schema_version", "wifi-presence-config/1"},
                     {"zone_id", "synthetic-zone"},
                     {"input_path", (root / "observations.fifo").string()},
                     {"output_path", (root / "current.json").string()},
                     {"manifest_path", (root / "manifest.json").string()},
                     {"manifest_sha256", hash(root / "manifest.json")},
                     {"max_frame_bytes", 4096}};
    write(root / "config.json", configuration);
    if (corrupt) {
      std::ofstream out(root / "linear.json", std::ios::app);
      out << "tampered";
    }
    check(mkfifo((root / "observations.fifo").c_str(), 0600) == 0,
          "FIFO creation failed");
    pipe = open((root / "observations.fifo").c_str(),
                O_RDWR | O_NONBLOCK | O_CLOEXEC);
    check(pipe >= 0, "FIFO open failed");
    start(synthetic);
    wait([](const Json &value) { return value.at("estimate") == "unknown"; });
  }
  ~Service() {
    try {
      stop();
    } catch (...) {
    }
    if (pipe >= 0)
      close(pipe);
    fs::remove_all(root);
  }
  void start(bool synthetic = true) {
    pid = fork();
    check(pid >= 0, "fork failed");
    if (pid == 0) {
      int nullfd = open("/dev/null", O_RDWR);
      dup2(nullfd, 0);
      dup2(nullfd, 1);
      dup2(nullfd, 2);
      if (nullfd > 2)
        close(nullfd);
      if (chdir("/") != 0)
        _exit(127);
      auto config = (root / "config.json").string();
      if (synthetic)
        execl(executable.c_str(), executable.c_str(), "--config",
              config.c_str(), "--allow-synthetic", nullptr);
      else
        execl(executable.c_str(), executable.c_str(), "--config",
              config.c_str(), nullptr);
      _exit(127);
    }
  }
  int stop() {
    if (pid < 0)
      return 0;
    kill(pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 400; ++i) {
      auto done = waitpid(pid, &status, WNOHANG);
      if (done == pid) {
        pid = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
      }
      usleep(5000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    pid = -1;
    throw std::runtime_error("service shutdown timeout");
  }
  int wait_exit() {
    int status = 0;
    for (int i = 0; i < 400; ++i) {
      if (waitpid(pid, &status, WNOHANG) == pid) {
        pid = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
      }
      usleep(5000);
    }
    throw std::runtime_error("startup rejection did not exit");
  }
  Json result() const { return Json::parse(read(root / "current.json")); }
  Json wait(const std::function<bool(const Json &)> &predicate,
            int milliseconds = 2500) const {
    auto until = now() + milliseconds;
    while (now() < until) {
      try {
        auto r = result();
        if (predicate(r))
          return r;
      } catch (const std::exception &) {
      }
      usleep(5000);
    }
    throw std::runtime_error("service result timeout");
  }
  Json observation(std::string source, std::uint64_t sequence, double level = 1,
                   std::int64_t capture = -1) const {
    return {{"schema_version", "wifi-presence-observation/1"},
            {"source_id", source},
            {"boot_epoch", "boot-1"},
            {"config_epoch", "config-1"},
            {"sequence", sequence},
            {"capture_ms", capture < 0 ? now() : capture},
            {"uncertainty_ms", 0},
            {"clock_domain", clock_domain()},
            {"pipeline_sha256", manifest.at("pipeline_sha256")},
            {"feature_names", {"synthetic_level"}},
            {"feature_units", {"unitless"}},
            {"features", {level}},
            {"metadata", {{"scenario", "synthetic"}}},
            {"integrity", true},
            {"parser_status", "ok"}};
  }
  void raw(const std::string &text) const {
    std::size_t position = 0;
    auto until = now() + 2000;
    while (position < text.size()) {
      auto n = ::write(pipe, text.data() + position, text.size() - position);
      if (n > 0)
        position += static_cast<std::size_t>(n);
      else if (errno == EAGAIN && now() < until)
        usleep(1000);
      else
        throw std::runtime_error("FIFO write failed");
    }
  }
  void send(const Json &value) const { raw(value.dump() + "\n"); }
  void window(double level = 1, std::uint64_t sequence = 1) const {
    auto capture = now();
    for (const auto &source : {"rx-a", "rx-b"})
      for (std::uint64_t i = 0; i < 2; ++i)
        send(observation(source, sequence + i, level, capture));
  }
};
void classes() {
  Service service(1500);
  service.window();
  auto r = service.wait(
      [](const Json &value) { return value.at("estimate") == "present"; });
  check(r.at("schema_version") == "wifi-presence-result/1",
        "result schema mismatch");
  check(r.at("provenance").dump().find("synthetic") != std::string::npos,
        "synthetic provenance missing");
  check(r.dump().find(service.boot) == std::string::npos,
        "raw clock identity leaked");
  service.window(-1, 3);
  service.wait(
      [](const Json &value) { return value.at("estimate") == "absent"; });
  service.window(0, 5);
  r = service.wait([](const Json &value) {
    return value.at("decision_reasons").dump().find("model_abstained") !=
           std::string::npos;
  });
  check(r.at("estimate") == "unknown", "abstention fallback");
  check(r.at("evidence_validity").at("state") == "valid",
        "abstention evidence");
}
void candidate_and_eof() {
  Service service;
  service.window();
  auto prior = service.wait(
      [](const Json &value) { return value.at("estimate") == "present"; });
  auto invalid = service.observation("rx-a", 3);
  invalid["integrity"] = false;
  service.send(invalid);
  auto r = service.wait([&](const Json &value) {
    return value.at("diagnostics") != prior.at("diagnostics");
  });
  check(r.at("estimate") == "present", "candidate erased current");
  check(r.at("valid_until") == prior.at("valid_until"), "deadline renewed");
  close(service.pipe);
  service.pipe = open((service.root / "observations.fifo").c_str(),
                      O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  r = service.wait([](const Json &value) {
    return value.at("decision_reasons").dump().find("evidence_expired") !=
           std::string::npos;
  });
  check(r.at("estimate") == "unknown", "EOF failed expiry");
  check(!r.at("last_known").is_null(), "history not labelled");
  check(r.at("last_known").at("evidence_validity") ==
            prior.at("evidence_validity"),
        "history evidence assessment changed");
  check(r.at("last_known").at("domain_eligibility") ==
            prior.at("domain_eligibility"),
        "history domain assessment changed");
  const auto lag = now() - prior.at("valid_until").get<std::int64_t>();
  std::cout << "TIMING fixed deadline retained; EOF expiry observed +" << lag
            << " ms" << std::endl;
}
void malformed_and_subset() {
  Service service(1500);
  service.raw("{\n[]\nnull\n" + std::string(5000, 'x') + "\n");
  for (const auto &key : {"sequence", "capture_ms", "uncertainty_ms"}) {
    auto o = service.observation("rx-a", 1);
    o[key] = -1;
    service.send(o);
    o[key] = 1.5;
    service.send(o);
    o[key] = true;
    service.send(o);
  }
  auto bad = service.observation("rx-a", 1);
  bad["schema_version"] = "wifi-presence-observation/99";
  service.send(bad);
  bad = service.observation("rx-a", 1);
  bad["features"] = {true};
  service.send(bad);
  service.raw("{\"features\":[NaN]}\n");
  service.send(service.observation("rx-a", 10));
  service.send(service.observation("rx-a", 11));
  usleep(40000);
  check(service.result().at("estimate") == "unknown",
        "unsupported partial source subset inferred");
  service.window(1, 20);
  service.wait(
      [](const Json &value) { return value.at("estimate") == "present"; });
}
void invalidation() {
  Service service(1500);
  service.window();
  service.wait(
      [](const Json &value) { return value.at("estimate") == "present"; });
  auto change = service.observation("rx-a", 3, 1, now());
  change["metadata"]["scenario"] = "changed";
  service.send(change);
  service.wait(
      [](const Json &value) { return value.at("estimate") == "unknown"; });
  service.send(service.observation("rx-b", 3));
  service.window(1, 4);
  service.wait(
      [](const Json &value) { return value.at("estimate") == "present"; });
  change = service.observation("rx-a", 1, 1, now());
  change["boot_epoch"] = "boot-2";
  change["feature_names"] = {"changed"};
  service.send(change);
  service.wait(
      [](const Json &value) { return value.at("estimate") == "unknown"; });
}
void flood_atomicity() {
  Service service;
  service.window();
  service.wait(
      [](const Json &value) { return value.at("estimate") == "present"; });
  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};
  std::thread reader([&] {
    while (!stop) {
      try {
        if (service.result().at("schema_version") != "wifi-presence-result/1")
          ++errors;
      } catch (...) {
        ++errors;
      }
    }
  });
  try {
    auto until = now() + 900;
    while (now() < until)
      service.raw(std::string(100, 'x') + "\n");
    service.wait(
        [](const Json &value) { return value.at("estimate") == "unknown"; });
  } catch (...) {
    stop = true;
    reader.join();
    throw;
  }
  stop = true;
  reader.join();
  check(errors == 0, "atomic reader saw incomplete record");
}
void restart_and_writer() {
  Service service(1500);
  service.window();
  auto original = service.wait(
      [](const Json &value) { return value.at("estimate") == "present"; });
  auto original_pid = service.pid;
  service.start();
  auto overlap = service.pid;
  service.pid = original_pid;
  int status = 0;
  bool finished = false;
  for (int i = 0; i < 400; ++i) {
    if (waitpid(overlap, &status, WNOHANG) == overlap) {
      finished = true;
      break;
    }
    usleep(5000);
  }
  if (!finished) {
    kill(overlap, SIGKILL);
    waitpid(overlap, &status, 0);
  }
  check(finished && WIFEXITED(status) && WEXITSTATUS(status) != 0,
        "overlapping writer not rejected");
  check(service.result().at("estimate") == "present",
        "overlap overwrote current");
  service.stop();
  service.start();
  auto r = service.wait([&](const Json &value) {
    return value.at("estimate") == "unknown" &&
           value.at("provenance").at("runtime_session") !=
               original.at("provenance").at("runtime_session");
  });
  check(r.at("last_known").is_null(), "restart restored history");
}
void startup_guards() {
  {
    Service service(650, false);
    check(service.wait_exit() == 2, "synthetic without opt-in accepted");
    check(service.result().at("estimate") == "unknown", "startup guard class");
  }
  {
    Service service(650, true, true);
    check(service.wait_exit() == 2, "corrupted artifact accepted");
    check(service.result().at("estimate") == "unknown",
          "integrity startup class");
  }
}
void clock_namespaces() {
  Service service(1500);
  const auto expected = clock_domain();
  const auto wrong = digest("wifi-presence-clock/1:" + service.boot +
                            ":different-time-namespace");
  for (const auto &source : {"rx-a", "rx-b"})
    for (std::uint64_t sequence = 1; sequence <= 2; ++sequence) {
      auto frame = service.observation(source, sequence);
      frame["clock_domain"] = wrong;
      service.send(frame);
    }
  service.wait([](const Json &value) {
    return value.at("diagnostics").at("rejected_candidates").get<int>() >= 4;
  });
  check(service.result().at("estimate") == "unknown",
        "wrong namespace inferred");
  auto legacy = service.observation("rx-a", 1);
  legacy.erase("clock_domain");
  legacy["clock_boot_id"] = service.boot;
  service.send(legacy);
  service.wait([](const Json &value) {
    return value.at("diagnostics").at("rejected_candidates").get<int>() >= 5;
  });
  if (actual_namespace_check) {
    int descriptors[2];
    check(pipe2(descriptors, O_CLOEXEC) == 0, "namespace result pipe failed");
    const auto child = fork();
    check(child >= 0, "namespace child failed");
    if (child == 0) {
      close(descriptors[0]);
      if (unshare(CLONE_NEWTIME) != 0) {
        const int code = errno;
        const std::string status = (code == EPERM || code == EACCES ||
                                    code == EINVAL || code == ENOSYS)
                                       ? "SKIP"
                                       : "ERROR";
        const auto ignored =
            ::write(descriptors[1], status.data(), status.size());
        (void)ignored;
        _exit(0);
      }
      const auto producer = fork();
      if (producer == 0) {
        try {
          const auto token = clock_domain();
          const auto n = ::write(descriptors[1], token.data(), token.size());
          _exit(n == static_cast<ssize_t>(token.size()) ? 0 : 1);
        } catch (...) {
          _exit(1);
        }
      }
      if (producer < 0)
        _exit(1);
      close(descriptors[1]);
      int status = 0;
      waitpid(producer, &status, 0);
      _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
    }
    close(descriptors[1]);
    char buffer[128];
    std::string token;
    for (;;) {
      const auto n = ::read(descriptors[0], buffer, sizeof buffer);
      if (n < 0 && errno == EINTR)
        continue;
      check(n >= 0, "namespace token read failed");
      if (n == 0)
        break;
      token.append(buffer, static_cast<std::size_t>(n));
    }
    close(descriptors[0]);
    int status = 0;
    waitpid(child, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "namespace child failed");
    if (token == "SKIP")
      std::cout << "SKIP actual time namespace: unavailable or not permitted"
                << std::endl;
    else {
      check(token.size() == 64 && token != expected,
            "actual namespace identity was not distinguished");
      auto frame = service.observation("rx-a", 1);
      frame["clock_domain"] = token;
      service.send(frame);
      service.wait([](const Json &value) {
        return value.at("diagnostics").at("rejected_candidates").get<int>() >=
               6;
      });
      check(service.result().at("estimate") == "unknown",
            "actual namespace inferred");
      std::cout << "PASS actual child time namespace rejected on same boot"
                << std::endl;
    }
  } else
    std::cout << "SKIP actual time namespace: guest-only check disabled"
              << std::endl;
  service.window();
  const auto result = service.wait(
      [](const Json &value) { return value.at("estimate") == "present"; });
  check(result.at("provenance").at("clock_domain") == expected,
        "published clock domain mismatch");
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 2 &&
      !(argc == 3 && std::string(argv[2]) == "--check-time-namespace")) {
    std::cerr << "usage: presence-service-tests /absolute/monitor "
                 "[--check-time-namespace]\n";
    return 2;
  }
  actual_namespace_check = argc == 3;
  executable = fs::absolute(argv[1]);
  const std::pair<const char *, void (*)()> cases[] = {
      {"classes, abstention, synthetic provenance", classes},
      {"candidate retention and EOF expiry", candidate_and_eof},
      {"schema, integers and exact source subset", malformed_and_subset},
      {"domain and epoch/feature invalidation", invalidation},
      {"input flood timers and atomic readers", flood_atomicity},
      {"overlapping writer and clean restart", restart_and_writer},
      {"opt-in and artifact integrity guards", startup_guards},
      {"time namespace token and legacy field rejection", clock_namespaces}};
  int failures = 0;
  for (const auto &test : cases) {
    try {
      test.second();
      std::cout << "PASS " << test.first << std::endl;
    } catch (const std::exception &error) {
      ++failures;
      std::cout << "FAIL " << test.first << ": " << error.what() << std::endl;
    }
  }
  std::cout << "SERVICE TESTS: " << (8 - failures) << " passed, " << failures
            << " failed" << std::endl;
  return failures ? 1 : 0;
}

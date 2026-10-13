#include "presence/adapters.hpp"
#include <array>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <sys/random.h>
#include <sys/stat.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stopping = 0;
void signal_stop(int) { stopping = 1; }
presence::Time now() {
  timespec ts{};
  if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0)
    throw std::runtime_error("clock_unavailable");
  return static_cast<presence::Time>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}
std::string session() {
  std::array<unsigned char, 16> bytes{};
  std::size_t filled = 0;
  while (filled < bytes.size()) {
    auto n = getrandom(bytes.data() + filled, bytes.size() - filled, 0);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      throw std::runtime_error("random_unavailable");
    filled += static_cast<std::size_t>(n);
  }
  const char *hex = "0123456789abcdef";
  std::string s;
  for (auto b : bytes) {
    s += hex[b >> 4];
    s += hex[b & 15];
  }
  return s;
}
class Worker {
  const presence::Bundle &bundle_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::optional<presence::Job> job_;
  std::optional<presence::Outcome> completion_;
  bool stop_ = false;
  int write_fd_;
  std::thread thread_;

public:
  Worker(const presence::Bundle &b, int write_fd)
      : bundle_(b), write_fd_(write_fd), thread_([this] {
          for (;;) {
            std::optional<presence::Job> job;
            {
              std::unique_lock<std::mutex> lock(mutex_);
              wake_.wait(lock, [&] { return stop_ || job_.has_value(); });
              if (stop_)
                return;
              job = std::move(job_);
              job_.reset();
            }
            presence::Outcome outcome;
            try {
              outcome = presence::infer(*job, bundle_);
            } catch (...) {
              outcome = {job->runtime,
                         job->generation,
                         job->id,
                         job->window.id,
                         presence::Estimate::unknown,
                         false};
            }
            {
              std::lock_guard<std::mutex> lock(mutex_);
              completion_ = std::move(outcome);
            }
            const char c = 'x';
            auto ignored = write(write_fd_, &c, 1);
            (void)ignored;
          }
        }) {}
  ~Worker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable())
      thread_.join();
  }
  void submit(presence::Job job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (job_ || completion_)
      throw std::runtime_error("worker_slot_collision");
    job_ = std::move(job);
    wake_.notify_one();
  }
  std::optional<presence::Outcome> take() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto c = std::move(completion_);
    completion_.reset();
    return c;
  }
};
struct Descriptor {
  int fd = -1;
  bool owned = true;
  ~Descriptor() {
    if (owned && fd >= 0)
      close(fd);
  }
};
} // namespace
int main(int argc, char **argv) {
  std::string config_path;
  bool synthetic = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--version" && argc == 2) {
      std::cout << "wifi-presence-monitor " PRESENCE_BUILD_ID "\n";
      return 0;
    }
    if (arg == "--config" && i + 1 < argc && config_path.empty())
      config_path = argv[++i];
    else if (arg == "--allow-synthetic" && !synthetic)
      synthetic = true;
    else {
      std::cerr << "usage: wifi-presence-monitor --config "
                   "/absolute/config.json [--allow-synthetic]\n";
      return 2;
    }
  }
  if (config_path.empty()) {
    std::cerr << "configuration_required\n";
    return 2;
  }
  try {
    umask(0077);
    auto config = presence::load_config(config_path);
    auto runtime = session();
    presence::AtomicWriter writer(config.output, runtime);
    presence::Contract startup_contract;
    startup_contract.sources = {"startup"};
    startup_contract.features = {"startup"};
    startup_contract.units = {"unit"};
    presence::PresenceMonitor startup(startup_contract, runtime);
    writer.write(presence::encode(startup, config, nullptr, now(), 1));
    std::optional<presence::Bundle> loaded;
    try {
      loaded = presence::load_bundle(config, synthetic);
    } catch (...) {
      startup.fail_startup(now());
      writer.write(presence::encode(startup, config, nullptr, now(), 2));
      std::cerr << "configuration_invalid\n";
      return 2;
    }
    auto boot = presence::read_regular("/proc/sys/kernel/random/boot_id", 128);
    while (!boot.empty() && (boot.back() == '\n' || boot.back() == '\r'))
      boot.pop_back();
    if (boot.empty())
      throw std::runtime_error("local_clock_identity_unavailable");
    config.clock_domain = presence::digest("wifi-presence-clock/1:" + boot);
    const auto &bundle = *loaded;
    presence::PresenceMonitor monitor(bundle.contract, runtime);
    std::uint64_t serial = 2;
    writer.write(presence::encode(monitor, config, &bundle, now(), serial));
    Descriptor input;
    if (config.input == "-") {
      input.fd = STDIN_FILENO;
      input.owned = false;
    } else {
      input.fd = open(config.input.c_str(),
                      O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
      if (input.fd < 0)
        throw std::runtime_error("input_open_failed");
      struct stat s {};
      if (fstat(input.fd, &s) != 0 || !S_ISFIFO(s.st_mode))
        throw std::runtime_error("input_fifo_required");
    }
    auto input_flags = fcntl(input.fd, F_GETFL);
    if (input_flags < 0 ||
        fcntl(input.fd, F_SETFL, input_flags | O_NONBLOCK) < 0)
      throw std::runtime_error("input_nonblocking_failed");
    struct Restore {
      int fd, flags;
      ~Restore() { fcntl(fd, F_SETFL, flags); }
    } restore{input.fd, input_flags};
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) != 0)
      throw std::runtime_error("completion_pipe_failed");
    Descriptor read_pipe{pipefd[0]}, write_pipe{pipefd[1]};
    struct sigaction action {};
    action.sa_handler = signal_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    signal(SIGPIPE, SIG_IGN);
    Worker worker(bundle, write_pipe.fd);
    auto apply = [&](presence::Step step) {
      const auto issuance = now();
      auto latest = monitor.tick(issuance);
      if (latest.job) {
        if (step.job)
          throw std::runtime_error("coordinator_slot_collision");
        step.job = std::move(latest.job);
      }
      if (step.publish || latest.publish) {
        if (serial == std::numeric_limits<std::uint64_t>::max())
          throw std::runtime_error("result_counter_exhausted");
        writer.write(
            presence::encode(monitor, config, &bundle, issuance, ++serial));
      }
      if (step.job)
        worker.submit(std::move(*step.job));
    };
    std::string line;
    line.reserve(config.max_frame);
    bool discarding = false, eof = false;
    presence::Time fifo_retry = 0;
    std::array<char, 4096> chunk{};
    std::size_t offset = 0, available = 0;
    try {
      while (!stopping) {
        apply(monitor.tick(now()));
        if (auto outcome = worker.take())
          apply(monitor.complete(*outcome, now()));
        std::size_t frames = 0, bytes = 0;
        while (frames < 32 && bytes < 65536 && !stopping) {
          if (offset == available) {
            offset = available = 0;
            if (eof && (config.input == "-" || now() < fifo_retry))
              break;
            ssize_t n = read(input.fd, chunk.data(), chunk.size());
            if (n < 0) {
              if (errno == EINTR)
                continue;
              if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
              throw std::runtime_error("input_read_failed");
            }
            if (n == 0) {
              if (!eof && (!line.empty() || discarding)) {
                apply(monitor.reject("incomplete_frame", now()));
                line.clear();
                discarding = false;
              }
              eof = true;
              fifo_retry = now() + 100;
              break;
            }
            eof = false;
            available = static_cast<std::size_t>(n);
          }
          char c = chunk[offset++];
          ++bytes;
          if (c == '\n') {
            ++frames;
            if (!discarding) {
              std::optional<presence::Observation> observation;
              try {
                observation = presence::decode(line, boot);
              } catch (const std::exception &) {
              }
              if (observation)
                apply(monitor.observe(std::move(*observation), now()));
              else
                apply(monitor.reject("input_contract_mismatch", now()));
            }
            line.clear();
            discarding = false;
          } else if (!discarding) {
            if (line.size() == config.max_frame) {
              line.clear();
              discarding = true;
              apply(monitor.reject("capacity_exceeded", now()));
            } else
              line.push_back(c);
          }
        }
        apply(monitor.tick(now()));
        pollfd fds[2]{
            {read_pipe.fd, POLLIN, 0},
            {input.fd,
             static_cast<short>(
                 (!eof || (config.input != "-" && now() >= fifo_retry)) ? POLLIN
                                                                        : 0),
             0}};
        if (eof)
          fds[1].fd = -1;
        int ready = poll(fds, 2, offset < available ? 0 : 10);
        if (ready < 0 && errno != EINTR)
          throw std::runtime_error("poll_failed");
        if (fds[0].revents & POLLIN) {
          char drain[64];
          while (read(read_pipe.fd, drain, sizeof drain) > 0) {
          }
        }
      }
      apply(monitor.stop(now()));
    } catch (...) {
      try {
        apply(monitor.stop(now()));
      } catch (...) {
      }
      throw;
    }
    return 0;
  } catch (const std::exception &) {
    std::cerr << "service_failed\n";
    return 2;
  }
}

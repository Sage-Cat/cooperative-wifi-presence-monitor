# Build and install

The first supported route is Linux with a C++17 compiler, CMake 3.20 or later,
OpenSSL development libraries and POSIX threads. Tests also use Python 3.
The checked native compiler is GCC 13.3; the sanitizer route uses Clang 18.1.3.
The build downloads the pinned nlohmann/json 3.12.0 header into its build tree,
verifies its SHA-256 and retains its required license notice. No runtime download
is performed. For offline builds, prepopulate the exact expected build-local
header with the documented checksum; verification is still required.

## Native build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

A separate sanitizer build exercises native memory and arithmetic behavior:

```sh
cmake -S . -B build/sanitizers -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ -DPRESENCE_SANITIZERS=ON
cmake --build build/sanitizers --parallel
ctest --test-dir build/sanitizers --output-on-failure
```

The portable domain/application core can be configured without the Linux
service and its JSON/OpenSSL/thread dependencies:

```sh
cmake -S . -B build/core -DPRESENCE_BUILD_SERVICE=OFF
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
```

## Static guest build

```sh
cmake -S . -B build/static -DCMAKE_BUILD_TYPE=Release -DPRESENCE_STATIC=ON
cmake --build build/static --parallel
```

Static mode requires the selected toolchain's static C/C++ and OpenSSL libraries.
It is separate from sanitizer mode. The resulting installed service can run in
a minimal Linux initramfs without a development checkout. Static linking does
not by itself establish support for other architectures, OpenWrt or prplOS.

## Installation

```sh
cmake --install build --prefix /usr/local
```

Configure `CMAKE_INSTALL_PREFIX` before building when choosing another deployed
prefix: the optional systemd unit embeds that configured executable path. An
install-time `--prefix` override is useful for staging, but does not rewrite the
unit. For a system package, prefer a configured `/usr` prefix plus `DESTDIR`.

This command may need operator privileges for a system prefix. For an unprivileged
clean-install check, choose a writable prefix or set `DESTDIR` to a temporary
staging directory. The allowlist in `packaging/INSTALL_ALLOWLIST.txt` describes
installed members. No model, site configuration, test fixture, recording, build
cache, kernel or QEMU image is installed with the service.

Create a dedicated unprivileged `wifi-presence-monitor` user/group before using
the optional systemd unit. Provide a reviewed model/profile and explicit config
under operator management; the producer must grant that account access to its
FIFO. Configure output in the unit's volatile runtime directory. Consumers need
explicit read access appropriate to that service account; no broad permissions
or world-readable output are assumed. The unit is not enabled automatically.

For a foreground process, use an operator-selected unprivileged account:

```sh
wifi-presence-monitor --config /etc/wifi-presence-monitor/config.json
```

The selected config and output directory must satisfy the runtime checks.
Synthetic demonstration bundles require the explicit `--allow-synthetic` option;
they never establish physical sensing accuracy. Model/configuration changes use
controlled stop/start, not a live management API. No persistent log/cache or
telemetry is enabled by default.

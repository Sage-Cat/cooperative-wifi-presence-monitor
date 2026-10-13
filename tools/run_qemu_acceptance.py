#!/usr/bin/env python3
"""Boot a real Linux guest and retain its console, exit status and VGA image.

All kernel, executable, install and runtime paths are explicit. No networking,
downloads, desktop windows or source-tree runtime files are needed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def copy_binary(binary, root, target):
    destination = root / target.lstrip("/")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, destination)
    linked = subprocess.run(["ldd", str(binary)], capture_output=True, text=True)
    # Static executables deliberately have no dynamic dependencies.
    for line in linked.stdout.splitlines():
        for part in line.split():
            if part.startswith("/") and Path(part).is_file():
                dep = root / part.lstrip("/")
                dep.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(part, dep)


def qmp_command(stream, command, arguments=None):
    stream.write((json.dumps({"execute": command, "arguments": arguments or {}}) + "\n").encode())
    stream.flush()
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError("QMP closed unexpectedly")
        response = json.loads(line)
        if "error" in response:
            raise RuntimeError(str(response["error"]))
        if "return" in response:
            return response["return"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--bios-directory", type=Path)
    parser.add_argument("--monitor", type=Path, required=True)
    parser.add_argument("--guest-tests", type=Path, required=True)
    parser.add_argument("--core-tests", type=Path)
    parser.add_argument("--busybox", type=Path, default=Path("/usr/bin/busybox"))
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    scratch = args.scratch.resolve()
    source_root = Path(__file__).resolve().parent.parent
    executable_directory = args.monitor.resolve().parent
    installation = executable_directory.parent if executable_directory.name == "bin" else executable_directory
    if scratch.is_relative_to(source_root) or scratch.is_relative_to(installation):
        raise RuntimeError("Runtime scratch must be outside the source and installed package")
    if len(str(scratch / "qmp.sock").encode()) >= 104:
        raise RuntimeError("Scratch path is too long for a QMP Unix-domain socket")
    scratch.mkdir(parents=True, exist_ok=False)
    root = scratch / "root"
    for directory in ("bin", "dev", "proc", "sys", "run", "tmp", "opt/bin"):
        (root / directory).mkdir(parents=True, exist_ok=True)
    copy_binary(args.busybox, root, "/bin/busybox")
    copy_binary(args.monitor, root, "/opt/bin/wifi-presence-monitor")
    copy_binary(args.guest_tests, root, "/opt/bin/presence-service-tests")
    if args.core_tests:
        copy_binary(args.core_tests, root, "/opt/bin/presence-core-tests")
    for name in ("sh", "mount", "mkdir", "cat", "sleep", "poweroff", "hostname", "uname"):
        (root / "bin" / name).symlink_to("busybox")
    init = root / "init"
    init.write_text('''#!/bin/sh
export PATH=/bin:/opt/bin
mount -t devtmpfs devtmpfs /dev
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t tmpfs tmpfs /run
mount -t tmpfs tmpfs /tmp
hostname presence-guest
echo "Presence monitor - real Linux system guest"
echo "Synthetic software acceptance; no physical accuracy claim"
echo "Networking disabled; volatile runtime only"
uname -srm
echo
status=0
if [ -x /opt/bin/presence-core-tests ]; then
  /opt/bin/presence-core-tests > /run/acceptance.log 2>&1 || status=1
  cat /run/acceptance.log
  cat /run/acceptance.log > /dev/ttyS0
  echo "CORE_CHECKPOINT" > /dev/ttyS0
  sleep 2
fi
printf '\033[2J\033[H'
echo "Presence monitor - installed service acceptance"
echo "Synthetic software checks; networking disabled"
uname -srm
echo
/opt/bin/presence-service-tests /opt/bin/wifi-presence-monitor --check-time-namespace > /run/acceptance.log 2>&1 || status=1
cat /run/acceptance.log
cat /run/acceptance.log > /dev/ttyS0
echo "GUEST_TEST_EXIT_CODE=$status"
echo "GUEST_TEST_EXIT_CODE=$status" > /dev/ttyS0
echo "QEMU_ACCEPTANCE_COMPLETE"
echo "QEMU_ACCEPTANCE_COMPLETE" > /dev/ttyS0
sleep 5
poweroff -f
''')
    init.chmod(0o755)
    archive = scratch / "initramfs.cpio"
    # CPIO names are generated without invoking a shell.
    names = ["."] + sorted(str(path.relative_to(root)) for path in root.rglob("*"))
    with archive.open("wb") as output:
        packed = subprocess.run(["cpio", "-o", "-H", "newc", "--quiet"], cwd=root,
                                input=("\n".join(names) + "\n").encode(), stdout=output,
                                stderr=subprocess.PIPE)
    if packed.returncode:
        raise RuntimeError(packed.stderr.decode())
    serial = scratch / "serial.log"
    qmp = scratch / "qmp.sock"
    argv = [str(args.qemu.resolve()), "-machine", "pc", "-accel", "tcg", "-m", "192M",
            "-smp", "1", "-kernel", str(args.kernel.resolve()), "-initrd", str(archive),
            "-append", "console=ttyS0 console=tty0 panic=-1 quiet", "-display", "none",
            "-vga", "std", "-serial", "file:" + str(serial), "-monitor", "none",
            "-qmp", "unix:" + str(qmp) + ",server=on,wait=off", "-nic", "none", "-no-reboot"]
    if args.bios_directory:
        argv.extend(["-L", str(args.bios_directory.resolve())])
    screenshot = scratch / "guest-console.ppm"
    core_screenshot = scratch / "core-console.ppm"

    def capture(destination):
        with socket.socket(socket.AF_UNIX) as channel:
            channel.settimeout(3)
            channel.connect(str(qmp))
            stream = channel.makefile("rwb")
            json.loads(stream.readline())
            qmp_command(stream, "qmp_capabilities")
            qmp_command(stream, "screendump", {"filename": str(destination)})

    with (scratch / "qemu.log").open("wb") as log:
        process = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                contents = serial.read_text(errors="replace") if serial.exists() else ""
                if "CORE_CHECKPOINT" in contents and not core_screenshot.exists():
                    capture(core_screenshot)
                if "QEMU_ACCEPTANCE_COMPLETE" in contents:
                    break
                if process.poll() is not None:
                    raise RuntimeError("Guest terminated before acceptance completed")
                time.sleep(0.1)
            else:
                raise RuntimeError("Guest acceptance timed out")
            capture(screenshot)
            exit_code = process.wait(timeout=15)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    receipt = {"schema_version": "presence-qemu-acceptance/1", "qemu_exit_code": exit_code,
               "guest_test_exit_code": 0 if "GUEST_TEST_EXIT_CODE=0\n" in contents else 1,
               "network": "disabled", "acceleration": "tcg", "architecture": "x86_64",
               "sha256": {name: digest(path) for name, path in {
                   "kernel": args.kernel, "qemu": args.qemu, "monitor": args.monitor,
                   "guest_tests": args.guest_tests, "initramfs": archive,
                   "serial_log": serial, "screenshot_ppm": screenshot}.items()}}
    if args.core_tests:
        receipt["sha256"]["core_tests"] = digest(args.core_tests)
        receipt["sha256"]["core_screenshot_ppm"] = digest(core_screenshot)
    if args.bios_directory:
        receipt["sha256"]["bios"] = digest(args.bios_directory / "bios-256k.bin")
        receipt["sha256"]["vga_bios"] = digest(args.bios_directory / "vgabios-stdvga.bin")
    (scratch / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))
    return 0 if exit_code == 0 and receipt["guest_test_exit_code"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

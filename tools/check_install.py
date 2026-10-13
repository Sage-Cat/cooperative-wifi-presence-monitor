#!/usr/bin/env python3
"""Check exact package membership and installed execution without a checkout.

This optional acceptance helper requires strace to conclusively observe network
syscalls. Reports and trace artifacts belong in the explicitly supplied scratch.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess


def hashes(root):
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(root.rglob("*")) if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--scratch", required=True, type=Path)
    parser.add_argument("--strace", default="strace")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    scratch = args.scratch.resolve()
    if scratch.is_relative_to(prefix) or scratch.is_relative_to(Path(__file__).resolve().parent.parent):
        raise RuntimeError("Runtime scratch must be outside the source and installed package")
    scratch.mkdir(mode=0o700, parents=True, exist_ok=False)
    notice = prefix / "share/cooperative-wifi-presence-monitor/INSTALL_ALLOWLIST.txt"
    allowed = set(notice.read_text().splitlines())
    initial = hashes(prefix)
    if set(initial) != allowed:
        raise RuntimeError("Installed files differ from explicit allowlist")
    if any(path.is_symlink() for path in prefix.rglob("*")):
        raise RuntimeError("Unexpected installed symlink")
    trace = shutil.which(args.strace)
    if trace is None:
        raise RuntimeError("strace is required for the network acceptance check")
    wrapper = scratch / "monitored-service"
    invocation = [trace, "-D", "-ff", "-qq", "-e", "trace=network", "-o", str(scratch / "network.trace"),
                  str(prefix / "bin/wifi-presence-monitor")]
    wrapper.write_text("#!/bin/sh\nexec " + shlex.join(invocation) + ' "$@"\n')
    wrapper.chmod(0o700)
    tests = Path(__file__).resolve().parent.parent / "tests/integration/test_service.py"
    environment = dict(os.environ)
    environment["XDG_CACHE_HOME"] = str(scratch / "empty-cache")
    environment["XDG_STATE_HOME"] = str(scratch / "empty-state")
    result = subprocess.run(["python3", str(tests), str(wrapper)], cwd="/", env=environment,
                            capture_output=True, text=True, timeout=90)
    (scratch / "service-tests.log").write_text(result.stdout + result.stderr)
    network_calls = []
    for path in scratch.glob("network.trace.*"):
        network_calls.extend(line for line in path.read_text().splitlines()
                             if line and not line.startswith(("--- ", "+++ ")))
    defaults_created = [name for name in ("empty-cache", "empty-state")
                        if (scratch / name).exists()]
    receipt = {"schema_version": "presence-install-acceptance/1", "test_exit_code": result.returncode,
               "package_members_match": True, "package_unchanged": initial == hashes(prefix),
               "network_syscalls": len(network_calls), "default_persistent_directories": defaults_created,
               "installed_files_sha256": initial}
    (scratch / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))
    return 0 if result.returncode == 0 and receipt["package_unchanged"] and not network_calls and not defaults_created else 1


if __name__ == "__main__":
    raise SystemExit(main())

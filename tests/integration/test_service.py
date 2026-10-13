#!/usr/bin/env python3
"""Independent installed-process checks using only synthetic observations."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from support import boottime_ms, bundle, clock_domain, config, observation, sha256, write_json
from consumer_contract import current_estimate

MONITOR = Path(sys.argv.pop(1)).resolve()


class RunningService:
    def __init__(self, root, sources=("rx-a", "rx-b"), age=700, synthetic=True):
        self.root = Path(root)
        self.manifest = bundle(self.root, sources, age)
        self.config = config(self.root, self.manifest)
        self.pipe = os.open(self.config["input_path"], os.O_RDWR | os.O_NONBLOCK)
        self.log = (self.root / "captured-service.log").open("wb")
        self.process = subprocess.Popen([str(MONITOR), "--config", str(self.root / "config.json")]
                                        + (["--allow-synthetic"] if synthetic else []),
                                        cwd="/", stdin=subprocess.DEVNULL,
                                        stdout=self.log, stderr=subprocess.STDOUT)

    def read(self):
        return json.loads(Path(self.config["output_path"]).read_text())

    def wait(self, predicate, timeout=2):
        deadline = time.monotonic() + timeout
        latest = None
        while time.monotonic() < deadline:
            try:
                latest = self.read()
                if predicate(latest):
                    return latest
            except FileNotFoundError:
                pass
            time.sleep(0.005)
        raise AssertionError("Service state not reached: " + repr(latest))

    def send(self, record):
        data = record if isinstance(record, bytes) else (json.dumps(record) + "\n").encode()
        os.write(self.pipe, data)

    def window(self, value=1, sequence=1, **updates):
        capture = boottime_ms()
        for source in self.manifest["source_ids"]:
            for index in range(self.manifest["samples_per_source"]):
                self.send(observation(self.manifest, source, sequence + index, value,
                                      capture, **updates))

    def close(self):
        os.close(self.pipe)
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise AssertionError("Service did not stop within three seconds")
        self.log.close()


class ServiceAcceptance(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="presence-acceptance-")
        self.services = []

    def tearDown(self):
        for service in reversed(self.services):
            service.close()
        self.temporary.cleanup()

    def service(self, name="runtime", **kwargs):
        service = RunningService(Path(self.temporary.name) / name, **kwargs)
        self.services.append(service)
        service.wait(lambda result: result["estimate"] == "unknown")
        return service

    def test_classes_abstention_and_provenance(self):
        service = self.service(age=1500)
        service.window(1)
        present = service.wait(lambda result: result["estimate"] == "present")
        self.assertEqual(present["schema_version"], "wifi-presence-result/1")
        self.assertIn("synthetic", json.dumps(present["provenance"]))
        self.assertNotIn("synthetic-boot-1", json.dumps(present["provenance"]))
        self.assertNotIn(Path("/proc/sys/kernel/random/boot_id").read_text().strip(), json.dumps(present))
        service.window(-1, 3)
        service.wait(lambda result: result["estimate"] == "absent")
        service.window(0, 5)
        result = service.wait(lambda result: "model_abstained" in result["decision_reasons"])
        self.assertEqual(result["estimate"], "unknown")
        self.assertEqual(result["evidence_validity"]["state"], "valid")
        self.assertEqual(result["domain_eligibility"]["state"], "eligible")

    def test_rejected_candidate_keeps_original_deadline_then_expires(self):
        service = self.service()
        service.window()
        prior = service.wait(lambda result: result["estimate"] == "present")
        invalid = observation(service.manifest, "rx-a", 10, integrity=False)
        service.send(invalid)
        result = service.wait(lambda result: result["diagnostics"] != prior["diagnostics"])
        self.assertEqual(result["estimate"], "present")
        self.assertEqual(result["valid_until"], prior["valid_until"])
        self.assertEqual(result["evidence_window"], prior["evidence_window"])
        expired = service.wait(lambda result: "evidence_expired" in result["decision_reasons"])
        self.assertEqual(expired["estimate"], "unknown")
        self.assertIsNotNone(expired["last_known"])
        self.assertEqual(expired["last_known"]["evidence_validity"], prior["evidence_validity"])
        self.assertEqual(expired["last_known"]["domain_eligibility"], prior["domain_eligibility"])

    def test_exact_subset_and_feature_contract(self):
        service = self.service()
        for sequence in (1, 2):
            service.send(observation(service.manifest, "rx-a", sequence))
        time.sleep(0.06)
        self.assertEqual(service.read()["estimate"], "unknown")
        service.send(observation(service.manifest, "rx-b", 1, feature_names=["different"]))
        service.send(observation(service.manifest, "rx-b", 2, feature_units=["different"]))
        time.sleep(0.06)
        self.assertEqual(service.read()["estimate"], "unknown")
        single = self.service("single", sources=("rx-a",))
        single.window()
        single.wait(lambda result: result["estimate"] == "present")

    def test_malformed_schema_integer_and_nonfinite_frames(self):
        service = self.service(age=1500)
        malformed = [b"{\n", b"[]\n", b"null\n", b"x" * 5000 + b"\n"]
        for data in malformed:
            service.send(data)
        for updates in ({"schema_version": "wifi-presence-observation/99"},
                        {"sequence": -1}, {"sequence": 1.5}, {"sequence": True},
                        {"capture_ms": -1}, {"uncertainty_ms": 0.5},
                        {"features": [float("nan")]}, {"features": [float("inf")]},
                        {"features": [True]}, {"integrity": "true"},
                        {"clock_domain": "0" * 64},
                        {"pipeline_sha256": "0" * 64}):
            service.send(observation(service.manifest, **updates))
        time.sleep(0.1)
        self.assertEqual(service.read()["estimate"], "unknown")
        self.assertIsNone(service.process.poll())
        service.window(sequence=20)
        service.wait(lambda result: result["estimate"] == "present")

    def test_future_mapping_and_excess_uncertainty_reject(self):
        service = self.service()
        future = boottime_ms() + 10000
        for source in service.manifest["source_ids"]:
            for sequence in (1, 2):
                service.send(observation(service.manifest, source, sequence,
                                         capture_ms=future + sequence))
        time.sleep(0.05)
        self.assertEqual(service.read()["estimate"], "unknown")
        service.window(sequence=3, uncertainty_ms=21)
        time.sleep(0.05)
        self.assertEqual(service.read()["estimate"], "unknown")

    def test_domain_and_epoch_changes_withdraw_current(self):
        service = self.service(age=1500)
        service.window()
        prior = service.wait(lambda result: result["estimate"] == "present")
        service.send(observation(service.manifest, "rx-a", 3,
                                 metadata={"scenario": "changed"}))
        invalidated = service.wait(lambda result: result["estimate"] == "unknown")
        self.assertEqual(invalidated["last_known"]["evidence_validity"], prior["evidence_validity"])
        self.assertEqual(invalidated["last_known"]["domain_eligibility"], prior["domain_eligibility"])
        service.send(observation(service.manifest, "rx-b", 3))
        service.window(sequence=4)
        service.wait(lambda result: result["estimate"] == "present")
        service.send(observation(service.manifest, "rx-a", 1, boot_epoch="synthetic-boot-2"))
        service.wait(lambda result: result["estimate"] == "unknown")

    def test_atomic_readers_eof_timer_and_no_default_files(self):
        service = self.service()
        service.window()
        service.wait(lambda result: result["estimate"] == "present")
        failures = []
        stop = threading.Event()

        def reader():
            while not stop.is_set():
                try:
                    result = service.read()
                    if result["schema_version"] != "wifi-presence-result/1":
                        failures.append("schema")
                except Exception as error:
                    failures.append(type(error).__name__)

        thread = threading.Thread(target=reader)
        thread.start()
        try:
            for _ in range(40):
                service.send(b"{\n")
            os.close(service.pipe)
            service.pipe = os.open(service.config["input_path"], os.O_RDONLY | os.O_NONBLOCK)
            service.wait(lambda result: "evidence_expired" in result["decision_reasons"])
        finally:
            stop.set()
            thread.join()
        self.assertEqual(failures, [])
        allowed = {"linear.json", "pipeline.json", "manifest.json", "config.json",
                   "observations.fifo", "current.json", "captured-service.log", "current.json.lock"}
        self.assertEqual(set(path.name for path in service.root.iterdir()) - allowed, set())

    def test_restart_new_session_and_overlapping_writer_rejected(self):
        service = self.service(age=1500)
        service.window()
        original = service.wait(lambda result: result["estimate"] == "present")
        overlap = subprocess.run([str(MONITOR), "--config", str(service.root / "config.json"),
                                  "--allow-synthetic"], cwd="/", capture_output=True, timeout=2)
        self.assertNotEqual(overlap.returncode, 0)
        self.assertEqual(service.read()["estimate"], "present")
        service.process.terminate()
        service.process.wait(timeout=3)
        service.process = subprocess.Popen([str(MONITOR), "--config", str(service.root / "config.json"),
                                            "--allow-synthetic"], cwd="/", stdout=service.log,
                                           stderr=subprocess.STDOUT)
        restarted = service.wait(lambda result: result["estimate"] == "unknown"
                                 and result["provenance"]["runtime_session"] != original["provenance"]["runtime_session"])
        self.assertIsNone(restarted["last_known"])

    def test_synthetic_requires_explicit_opt_in(self):
        service = self.service(synthetic=False)
        service.process.wait(timeout=2)
        self.assertNotEqual(service.process.returncode, 0)
        self.assertEqual(service.read()["estimate"], "unknown")

    def test_gap_recovery_preserves_other_source_complete_window(self):
        service = self.service(age=1500)
        service.window()
        prior = service.wait(lambda result: result["estimate"] == "present")
        service.send(observation(service.manifest, "rx-a", 3,
                                 metadata={"scenario": "changed"}))
        service.wait(lambda result: result["estimate"] == "unknown")
        # rx-b sequence 3 was lost. Its gap must not erase rx-a's new 4,5 window.
        service.window(sequence=4)
        recovered = service.wait(lambda result: result["estimate"] == "present")
        self.assertNotEqual(recovered["evidence_window"], prior["evidence_window"])
        for source in recovered["evidence_window"]:
            self.assertEqual((source["first_sequence"], source["last_sequence"]), (4, 5))

    def test_consumer_independently_checks_clock_schema_and_expiry(self):
        service = self.service(age=1500)
        service.window()
        result = service.wait(lambda result: result["estimate"] == "present")
        trusted_clock = clock_domain()
        deadline = result["valid_until"]
        self.assertEqual(current_estimate(result, boottime_ms(), trusted_clock), "present")
        self.assertEqual(current_estimate(result, deadline, trusted_clock), "present")
        self.assertEqual(current_estimate(result, deadline + 1, trusted_clock), "unknown")
        self.assertEqual(current_estimate(result, boottime_ms(), clock_domain("different-time-namespace")), "unknown")
        for field in result:
            incomplete = copy.deepcopy(result)
            del incomplete[field]
            self.assertEqual(current_estimate(incomplete, boottime_ms(), trusted_clock), "unknown", field)
        for parent in ("age", "evidence_validity", "domain_eligibility", "provenance", "diagnostics"):
            for field in result[parent]:
                incomplete = copy.deepcopy(result)
                del incomplete[parent][field]
                self.assertEqual(current_estimate(incomplete, boottime_ms(), trusted_clock), "unknown", parent + "." + field)
        for field in result["evidence_window"][0]:
            incomplete = copy.deepcopy(result)
            del incomplete["evidence_window"][0][field]
            self.assertEqual(current_estimate(incomplete, boottime_ms(), trusted_clock), "unknown", "source." + field)
        unsupported = copy.deepcopy(result)
        unsupported["schema_version"] = "wifi-presence-result/99"
        self.assertEqual(current_estimate(unsupported, boottime_ms(), trusted_clock), "unknown")

    def test_wrong_time_namespace_and_legacy_raw_boot_field_rejected(self):
        service = self.service(age=1500)
        # Same local boot identity, independently supplied different namespace identity.
        wrong_domain = clock_domain("different-time-namespace")
        service.window(clock_domain=wrong_domain)
        rejected = service.wait(lambda result: result["diagnostics"]["rejected_candidates"] >= 4)
        self.assertEqual(rejected["estimate"], "unknown")
        old_record = observation(service.manifest)
        del old_record["clock_domain"]
        old_record["clock_boot_id"] = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        service.send(old_record)
        service.wait(lambda result: result["diagnostics"]["rejected_candidates"] >= 5)
        self.assertEqual(service.read()["estimate"], "unknown")
        service.window()
        accepted = service.wait(lambda result: result["estimate"] == "present")
        self.assertEqual(accepted["provenance"]["clock_domain"], clock_domain())

    def test_pinned_bundle_integrity_semantics_and_integer_contracts(self):
        cases = [
            ("pin", None), ("artifact-bytes", None), ("pipeline-bytes", None),
            ("schema", ("schema_version", "wifi-presence-model/99")),
            ("fractional-samples", ("samples_per_source", 1.5)),
            ("boolean-samples", ("samples_per_source", True)),
            ("negative-samples", ("samples_per_source", -1)),
            ("excess-samples", ("samples_per_source", 257)),
            ("motion-only", ("stationary_presence", False)),
            ("wrong-negative-semantics", ("negative_class", "no-motion")),
            ("missing-review", ("provenance_kind", "reviewed-presence")),
            ("unsupported-feature-order", ("feature_names", ["other", "synthetic_level"])),
        ]
        for name, update in cases:
            with self.subTest(name=name):
                root = Path(self.temporary.name) / name
                manifest = bundle(root)
                configuration = config(root, manifest)
                if update:
                    manifest[update[0]] = update[1]
                    write_json(root / "manifest.json", manifest)
                    configuration["manifest_sha256"] = sha256(root / "manifest.json")
                elif name == "pin":
                    configuration["manifest_sha256"] = "0" * 64
                else:
                    path = root / ("linear.json" if name == "artifact-bytes" else "pipeline.json")
                    path.write_text(path.read_text() + "tampered")
                write_json(root / "config.json", configuration)
                failed = subprocess.run([str(MONITOR), "--config", str(root / "config.json"),
                                         "--allow-synthetic"], cwd="/", capture_output=True, timeout=2)
                self.assertEqual(failed.returncode, 2)
                result = json.loads((root / "current.json").read_text())
                self.assertEqual(result["estimate"], "unknown")
                self.assertIn("configuration_invalid", result["decision_reasons"])


if __name__ == "__main__":
    unittest.main()

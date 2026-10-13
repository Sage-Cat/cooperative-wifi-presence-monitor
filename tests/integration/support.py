"""Synthetic software fixtures; these values are not a sensing model."""
import hashlib
import json
import os
from pathlib import Path
import time


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, separators=(",", ":")) + "\n")


def boottime_ms():
    return int(time.clock_gettime(time.CLOCK_BOOTTIME) * 1000)


def clock_domain(namespace_identity=None):
    boot = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
    if namespace_identity is None:
        try:
            namespace_identity = os.readlink("/proc/self/ns/time")
        except FileNotFoundError:
            namespace_identity = "global"
    return hashlib.sha256(("wifi-presence-clock/1:" + boot + ":" + namespace_identity).encode()).hexdigest()


def bundle(root, sources=("rx-a", "rx-b"), max_age_ms=700, samples=2):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    write_json(root / "linear.json", {
        "schema_version": "wifi-presence-linear/1", "weights": [1.0] * len(sources),
        "bias": 0.0, "absent_threshold": -0.5, "present_threshold": 0.5,
    })
    write_json(root / "pipeline.json", {
        "schema_version": "wifi-presence-pipeline/1", "aggregation": "per-source-mean",
        "feature_names": ["synthetic_level"], "feature_units": ["unitless"],
    })
    manifest = {
        "schema_version": "wifi-presence-model/1", "model_id": "synthetic-linear-test",
        "pipeline_id": "synthetic-mean-v1", "profile_id": "synthetic-profile",
        "provenance_kind": "synthetic", "task": "zone-presence",
        "stationary_presence": True, "negative_class": "absence",
        "source_ids": list(sources), "feature_names": ["synthetic_level"],
        "feature_units": ["unitless"], "required_metadata": {"scenario": "synthetic"},
        "samples_per_source": samples, "max_window_span_ms": 200,
        "max_alignment_ms": 50, "max_age_ms": max_age_ms, "max_uncertainty_ms": 20,
        "artifact_path": "linear.json", "artifact_sha256": sha256(root / "linear.json"),
        "pipeline_path": "pipeline.json", "pipeline_sha256": sha256(root / "pipeline.json"),
    }
    write_json(root / "manifest.json", manifest)
    return manifest


def config(root, manifest):
    root = Path(root)
    fifo = root / "observations.fifo"
    os.mkfifo(fifo, 0o600)
    value = {
        "schema_version": "wifi-presence-config/1", "zone_id": "synthetic-zone",
        "input_path": str(fifo), "output_path": str(root / "current.json"),
        "manifest_path": str(root / "manifest.json"),
        "manifest_sha256": sha256(root / "manifest.json"), "max_frame_bytes": 4096,
    }
    write_json(root / "config.json", value)
    return value


def observation(manifest, source="rx-a", sequence=1, value=1.0, capture_ms=None, **updates):
    record = {
        "schema_version": "wifi-presence-observation/1", "source_id": source,
        "boot_epoch": "synthetic-boot-1", "config_epoch": "synthetic-config-1",
        "sequence": sequence, "capture_ms": boottime_ms() if capture_ms is None else capture_ms,
        "uncertainty_ms": 0,
        "clock_domain": clock_domain(),
        "pipeline_sha256": manifest["pipeline_sha256"],
        "feature_names": ["synthetic_level"], "feature_units": ["unitless"],
        "features": [value], "metadata": {"scenario": "synthetic"},
        "integrity": True, "parser_status": "ok",
    }
    record.update(updates)
    return record

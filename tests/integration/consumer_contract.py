"""Reference acceptance consumer; local OS context is supplied independently."""


def integer(value):
    return type(value) is int and value >= 0


def current_estimate(result, now_ms, expected_clock_domain):
    """Fail closed on schema, clock, age or completeness errors at every use."""
    try:
        if not isinstance(expected_clock_domain, str) or len(expected_clock_domain) != 64:
            return "unknown"
        required = {"schema_version", "result_id", "zone_id", "estimate", "decision_reasons",
                    "evidence_validity", "domain_eligibility", "evidence_window", "age",
                    "valid_until", "provenance", "diagnostics", "last_known"}
        if not isinstance(result, dict) or not required.issubset(result):
            return "unknown"
        if result["schema_version"] != "wifi-presence-result/1":
            return "unknown"
        if result["estimate"] not in ("present", "absent"):
            return "unknown"
        nested = {
            "evidence_validity": {"state", "reasons"},
            "domain_eligibility": {"state", "reasons", "profile_id", "profile_sha256"},
            "age": {"upper_bound_ms", "maximum_ms", "as_of_ms", "time_basis"},
            "provenance": {"source_schema", "model_id", "model_sha256", "manifest_sha256",
                           "pipeline_id", "pipeline_sha256", "config_sha256", "software_build_id",
                           "runtime_session", "generation", "kind", "synthetic", "review_reference",
                           "clock_domain", "time_basis"},
            "diagnostics": {"rejected_candidates", "skipped_windows", "stale_completions",
                            "counter_semantics", "latest_reason", "evidence_validity", "domain_eligibility"},
        }
        for key, fields in nested.items():
            if not isinstance(result[key], dict) or not fields.issubset(result[key]):
                return "unknown"
        provenance = result["provenance"]
        if provenance["clock_domain"] != expected_clock_domain or provenance["time_basis"] != "CLOCK_BOOTTIME":
            return "unknown"
        if result["evidence_validity"]["state"] != "valid" or result["domain_eligibility"]["state"] != "eligible":
            return "unknown"
        deadline = result["valid_until"]
        age = result["age"]
        if not integer(now_ms) or not integer(deadline) or now_ms > deadline:
            return "unknown"
        if age["time_basis"] != "CLOCK_BOOTTIME" or not all(integer(age[key]) for key in
                                                           ("maximum_ms", "upper_bound_ms", "as_of_ms")):
            return "unknown"
        if age["maximum_ms"] == 0 or age["as_of_ms"] > now_ms:
            return "unknown"
        if not isinstance(result["evidence_window"], list) or not result["evidence_window"]:
            return "unknown"
        for source in result["evidence_window"]:
            fields = {"source_id", "boot_epoch", "config_epoch", "first_sequence", "last_sequence",
                      "capture_start_ms", "capture_end_ms", "uncertainty_ms", "mapping_status", "time_basis"}
            if not isinstance(source, dict) or not fields.issubset(source):
                return "unknown"
            end, uncertainty = source["capture_end_ms"], source["uncertainty_ms"]
            if not integer(end) or not integer(uncertainty):
                return "unknown"
            if source["mapping_status"] != "verified-local-clock-domain" or source["time_basis"] != "CLOCK_BOOTTIME":
                return "unknown"
            if end - now_ms > uncertainty or now_ms - end + uncertainty > age["maximum_ms"]:
                return "unknown"
            if deadline > end - uncertainty + age["maximum_ms"]:
                return "unknown"
        return result["estimate"]
    except (KeyError, TypeError, ValueError):
        return "unknown"

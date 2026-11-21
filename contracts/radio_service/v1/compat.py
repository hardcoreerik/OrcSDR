"""Host-only, side-effect-free reference adapters for Radio Service v1.

NOT production firmware/Android code and NOT a secure remote service.
All identity, generation and timing inputs must come from the provider.
No wall-clock timestamps, authorization grants, or hardware ACKs are invented.
"""
from __future__ import annotations

import copy
from typing import Any, Mapping

CONTRACT = "orcsdr.radio-service"
BYTES_PER_COMPLEX = {"cu8": 2, "cs8": 2, "cs16le": 4, "cf32le": 8}
BYTES_PER_AUDIO_SAMPLE = {"s16le": 2, "f32le": 4}


def envelope(kind: str, payload: Mapping[str, Any], *, host_id: str,
             boot_id: str, receiver_id: str | None, attachment_id: str | None,
             session_id: str | None, generation: int, sequence: int,
             time: Mapping[str, Any]) -> dict[str, Any]:
    """Construct metadata; caller owns correct generation and clock provenance."""
    return {
        "contract": CONTRACT, "version": {"major": 1, "minor": 0},
        "kind": kind, "host_id": host_id, "boot_id": boot_id,
        "receiver_id": receiver_id, "attachment_id": attachment_id,
        "session_id": session_id, "generation": generation,
        "sequence": sequence, "time": dict(time), "payload": dict(payload),
    }


def headless_state(slot: Mapping[str, Any]) -> dict[str, Any]:
    """Map a Headless GET /api/v1/receivers slot to receiver_state payload.

    Legacy state 0 absent, 1 present, 2 claimed, 3 streaming, 4 error.
    The caller must mint identity/generation independently.
    """
    states = {0: "absent", 1: "present", 2: "ready",
              3: "streaming", 4: "error"}
    legacy = slot.get("state")
    if isinstance(legacy, bool) or legacy not in states:
        raise ValueError("invalid Headless receiver state")
    state = states[legacy]
    if slot.get("present") is False:
        state = "absent"
    if not isinstance(slot.get("index"), int) or isinstance(slot["index"], bool):
        raise ValueError("receiver slot index is required")
    center = slot.get("center_hz")
    rate = slot.get("sample_rate")
    return {
        "state": state,
        "owner": None,  # Headless slot does not expose an authoritative lease
        "applied": {
            "center_frequency_hz": center if isinstance(center, int) and
            not isinstance(center, bool) and center > 0 and state != "absent" else None,
            "sample_rate_sps": rate if isinstance(rate, int) and
            not isinstance(rate, bool) and rate > 0 and state != "absent" else None,
            "gain_mode": "unknown",  # requires separate /api/v1/tuner readback
            "gain_tenth_db": None,
        },
        "pending_command_id": None,
    }


def headless_health(slot: Mapping[str, Any]) -> dict[str, Any]:
    """Preserve Headless units; legacy mixed-unit 'drops' cannot become samples."""
    names = ("usb_bytes_received", "iq_bytes_delivered", "iq_bytes_consumed",
             "usb_errors", "ring_overrun_bytes")
    out = {}
    for name in names:
        v = slot.get(name)
        out[name] = v if isinstance(v, int) and not isinstance(v, bool) and v >= 0 else None
    out.update(
        dropped_complex_samples=None,  # mixed-unit drops not convertible
        effective_sample_rate_sps=None,  # configured != measured
        buffer_fill_bytes=None,  # high-water != current fill
        uptime_ms=None,
    )
    return out


def mobile_state(snapshot: Mapping[str, Any]) -> dict[str, Any]:
    """Map the Android RadioSnapshot map; do not infer clock or generation."""
    streaming = snapshot.get("streaming") is True
    freq = snapshot.get("frequencyHz")
    rate = snapshot.get("rateHz")
    return {
        "state": "streaming" if streaming else "present",
        "owner": None,  # current Mobile RadioChannel has no lease field
        "applied": {
            "center_frequency_hz": freq if isinstance(freq, int) and
            not isinstance(freq, bool) and freq > 0 else None,
            "sample_rate_sps": rate if isinstance(rate, int) and
            not isinstance(rate, bool) and rate > 0 else None,
            "gain_mode": "unknown", "gain_tenth_db": None,
        },
        "pending_command_id": None,
    }


def check_semantics(msg: Mapping[str, Any]) -> None:
    """Cross-field checks not expressible in draft-2020-12 JSON Schema alone."""
    payload = msg["payload"]
    kind = msg["kind"]
    if kind == "capabilities":
        rx = payload["receiver"]
        for r in rx["frequency_ranges"]:
            if r["min_hz"] > r["max_hz"]:
                raise ValueError("inverted RF range")
        for r in rx["sample_rate_ranges_sps"]:
            if r["min_sps"] > r["max_sps"]:
                raise ValueError("inverted rate range")
        if not rx["tx"]["supported"] and (rx["tx"]["frequency_ranges"] or rx["tx"]["modes"]):
            raise ValueError("TX capabilities published while TX unsupported")
    if kind == "iq_block":
        if payload["complex_sample_count"] * BYTES_PER_COMPLEX[payload["format"]] != payload["payload_bytes"]:
            raise ValueError("IQ byte length does not match complex sample format")
    if kind == "spectrum_frame" and payload["bin_count"] != len(payload["bins"]):
        raise ValueError("spectrum count does not match bins")
    if kind == "audio_frame":
        expected = (payload["frame_count"] * payload["channels"] *
                    BYTES_PER_AUDIO_SAMPLE[payload["format"]])
        if expected != payload["payload_bytes"]:
            raise ValueError("audio byte length does not match format")
    time = msg["time"]
    if time["utc_valid"] and time["clock_source"] == "unavailable":
        raise ValueError("UTC marked valid without clock source")


def _result(command: Mapping[str, Any], status: str, *, generation: int,
            applied: Mapping[str, Any] | None = None,
            error: dict[str, Any] | None = None, backend_ack: bool = False,
            ack_source: str | None = None) -> dict[str, Any]:
    p = command["payload"]
    return {
        "command_id": p["command_id"], "operation": p["operation"],
        "status": status, "requested": copy.deepcopy(p["requested"]),
        "applied": copy.deepcopy(applied) if applied is not None else None,
        "result_generation": generation, "backend_ack": backend_ack,
        "ack_source": ack_source, "error": error,
    }


def _deny(command: Mapping[str, Any], generation: int, code: str,
          message: str, unavailable: bool = False) -> dict[str, Any]:
    return _result(
        command, "unavailable" if unavailable else "failed",
        generation=generation,
        error={"code": code, "message": message, "retryable": code in {
            "STALE_GENERATION", "ATTACHMENT_CHANGED", "RECEIVER_BUSY",
            "NOT_READY", "TRANSPORT_UNAVAILABLE", "TIMEOUT"}},
    )


def preflight_tune(command: Mapping[str, Any], state: Mapping[str, Any],
                   receiver_capabilities: Mapping[str, Any],
                   scopes: set[str], *, principal_id: str | None = None) -> dict[str, Any]:
    """Return pending or rejection. Never calls a driver or says applied.

    The host must deduplicate command IDs, serialize leases, fence old IQ
    immediately on acceptance, and deliver ACK to complete_tune().
    """
    generation = state["generation"]
    p = command["payload"]
    if command["kind"] != "command" or p["operation"] != "tune":
        return _deny(command, generation, "INVALID_ARGUMENT", "tune command required")
    if ("tune" not in scopes or (state["payload"]["owner"] is not None and
                                state["payload"]["owner"] != principal_id)):
        return _deny(command, generation, "UNAUTHORIZED", "tune scope and ownership required")
    if (command["host_id"] != state["host_id"] or
            command["boot_id"] != state["boot_id"] or
            command["receiver_id"] != state["receiver_id"] or
            command["session_id"] != state["session_id"] or
            p["expected_generation"] != generation):
        return _deny(command, generation, "STALE_GENERATION", "stale receiver session")
    if (command["attachment_id"] != state["attachment_id"] or
            p["expected_attachment_id"] != state["attachment_id"]):
        return _deny(command, generation, "ATTACHMENT_CHANGED", "receiver was reattached")
    if state["payload"]["state"] not in {"ready", "streaming"}:
        return _deny(command, generation, "NOT_READY", "receiver not ready")
    hz = p["requested"].get("frequency_hz")
    if isinstance(hz, bool) or not isinstance(hz, int) or hz <= 0:
        return _deny(command, generation, "INVALID_ARGUMENT", "positive integer frequency_hz required")
    if not any(r["min_hz"] <= hz <= r["max_hz"]
               for r in receiver_capabilities["frequency_ranges"]):
        return _deny(command, generation, "OUT_OF_RANGE", "RF path does not cover requested Hz", True)
    rate = p["requested"].get("sample_rate_sps")
    if rate is not None:
        rates = receiver_capabilities["sample_rates_sps"]
        ranges = receiver_capabilities["sample_rate_ranges_sps"]
        if isinstance(rate, bool) or not isinstance(rate, int) or rate <= 0:
            return _deny(command, generation, "INVALID_ARGUMENT", "invalid sample rate")
        if rate not in rates and not any(r["min_sps"] <= rate <= r["max_sps"] for r in ranges):
            return _deny(command, generation, "UNSUPPORTED_CAPABILITY", "unsupported sample rate", True)
    return _result(command, "pending", generation=generation)


def complete_tune(command: Mapping[str, Any], pending: Mapping[str, Any], *,
                  hardware_ack: bool, effective: Mapping[str, Any] | None = None,
                  ack_source: str | None = None) -> dict[str, Any]:
    """Advance generation on verified driver completion. No optimistic setpoint.

    If ACK fails, host must fence and re-read device before resuming old IQ.
    """
    if pending["status"] != "pending" or pending["command_id"] != command["payload"]["command_id"]:
        raise ValueError("no matching pending command")
    gen = pending["result_generation"]
    if not hardware_ack:
        return _deny(command, gen, "HARDWARE_FAILURE", "hardware did not acknowledge tune")
    if effective is None or not ack_source:
        raise ValueError("verified effective settings and ack source are required")
    if effective.get("center_frequency_hz") is None:
        raise ValueError("readback must include effective frequency")
    return _result(command, "applied", generation=gen + 1,
                   applied=effective, backend_ack=True, ack_source=ack_source)

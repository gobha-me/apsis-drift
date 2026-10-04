"""Compact durable dispatch reports without discarding their source certificates.

This module has no filesystem or numerical authority. The writer supplies
already durable method bindings. A reader supplies bounded, same-job reads;
verification must finish before the reconstructed report is trusted.
"""

from fractions import Fraction
import hashlib
import json
import math
import re


SCHEMA = "apsis.durable-dispatch-reference/1"
MAX_BYTES = 512 * 1024**2
MAX_RESULT = 16 * 1024**2
MAX_MARKER = 512 * 1024
MAX_NODES = 1_000_000
MAX_DEPTH = 32
MAX_TOKEN = 131072
MAX_CALLS = 1092
DISPATCH_KEYS = {
    "status", "coverage_complete", "inspection_sha256",
    "batch_approval_sha256", "records", "calls_started", "calls_expected",
    "metadata_integrity_operations", "pruned_faces", "operations",
    "new_embedding_factory_seed_calls",
    "source_or_physical_material_or_runtime_authority",
    "old_material_containment_or_K_or_mutual_bridge_qualification",
}
REF_KEYS = {
    "method_index", "method_key", "start_pin", "args_sha256",
    "operation_limit", "result_pin", "completion_pin", "completion_state",
}


class Refusal(ValueError):
    pass


def _need(condition, message):
    if not condition:
        raise Refusal(message)


def _integer(value, lower, upper):
    _need(type(value) is int and lower <= value <= upper, "Typed integer bound")


def _pin_shape(value, cap):
    _need(type(value) is dict and set(value) == {"bytes", "sha256"}, "Closed pin")
    _integer(value["bytes"], 1, cap)
    _need(type(value["sha256"]) is str and
          re.fullmatch("[0-9a-f]{64}", value["sha256"]) is not None, "SHA256")


def _values(value):
    """Bound decoded trees and reject JSON's otherwise ambiguous key coercion."""
    nodes = 0
    stack = [(value, 0)]
    while stack:
        item, depth = stack.pop()
        nodes += 1
        _need(nodes <= MAX_NODES and depth <= MAX_DEPTH, "JSON tree bound")
        if type(item) is dict:
            _need(all(type(k) is str for k in item), "String JSON keys")
            _need(len(item) <= MAX_NODES - nodes, "JSON object bound")
            stack.extend((x, depth + 1) for x in item.values())
        elif type(item) in (list, tuple):
            _need(len(item) <= MAX_NODES - nodes, "JSON array bound")
            stack.extend((x, depth + 1) for x in item)
        elif type(item) is str:
            _need(len(item.encode()) <= MAX_TOKEN, "JSON string bound")
        elif type(item) is float:
            _need(math.isfinite(item), "Finite JSON scalar")
        elif type(item) is int:
            _need(item.bit_length() <= 4096, "JSON integer bound")
        elif type(item) is Fraction:
            _need(max(item.numerator.bit_length(), item.denominator.bit_length())
                  <= 4096, "Rational scalar bound")
        else:
            _need(type(item) is bool or item is None, "Supported JSON scalar")


def _chunks(value):
    def exact(item):
        if type(item) is Fraction:
            return {"n": str(item.numerator), "d": str(item.denominator)}
        raise Refusal("Unsupported output scalar")
    for part in json.JSONEncoder(sort_keys=True, separators=(",", ":"),
                                 allow_nan=False, default=exact).iterencode(value):
        raw = part.encode()
        _need(len(raw) <= MAX_TOKEN, "Encoded JSON token bound")
        yield raw


def canonical_pin(value, cap=MAX_BYTES):
    _integer(cap, 1, MAX_BYTES)
    _values(value)
    return _stream_pin(value, cap)


def _stream_pin(value, cap):
    # Aggregate dispatch trees are validated one bounded source result at a time.
    # They may legitimately contain more nodes than one ordinary result record.
    digest = hashlib.sha256()
    size = 0
    for raw in _chunks(value):
        size += len(raw)
        _need(size <= cap, "Canonical byte bound")
        digest.update(raw)
    return {"bytes": size, "sha256": digest.hexdigest()}


def _bytes_pin(raw, cap):
    _need(type(raw) is bytes and 0 < len(raw) <= cap, "Bounded record bytes")
    return {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def _decode(raw, cap):
    _bytes_pin(raw, cap)
    depth = 0
    quote = escape = False
    token = atom = 0
    for c in raw:
        if quote:
            token += 1
            _need(token <= MAX_TOKEN, "JSON lexical token bound")
            if escape:
                escape = False
            elif c == 92:
                escape = True
            elif c == 34:
                quote = False
        elif c == 34:
            quote = True
            token = 0
            atom = 0
        elif c in (91, 123):
            atom = 0
            depth += 1
            _need(depth <= MAX_DEPTH, "JSON lexical depth bound")
        elif c in (93, 125):
            atom = 0
            depth -= 1
            _need(depth >= 0, "JSON lexical delimiters")
        elif c in b" \t\r\n,:":
            atom = 0
        else:
            atom += 1
            _need(atom <= MAX_TOKEN, "JSON lexical atom bound")
    _need(depth == 0 and not quote, "JSON lexical closure")

    def pairs(rows):
        result = {}
        for key, value in rows:
            _need(key not in result, "Duplicate JSON key")
            result[key] = value
        return result

    def invalid(_):
        raise Refusal("Nonfinite JSON constant")

    try:
        value = json.loads(raw, object_pairs_hook=pairs, parse_constant=invalid)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise Refusal("Invalid record JSON") from error
    _values(value)
    _need(canonical_pin(value, cap) == _bytes_pin(raw, cap), "Canonical record JSON")
    return value


def _dispatch(value, compact=False):
    keys = DISPATCH_KEYS | ({"schema", "full_dispatch_pin"} if compact else set())
    _need(type(value) is dict and set(value) == keys, "Closed dispatch fields")
    _integer(value["calls_expected"], MAX_CALLS, MAX_CALLS)
    _integer(value["calls_started"], 0, MAX_CALLS)
    _need(type(value["records"]) is list and
          len(value["records"]) == value["calls_started"], "Ordered dispatch count")
    _need(type(value["coverage_complete"]) is bool, "Typed dispatch completeness")
    _need(value["source_or_physical_material_or_runtime_authority"] is False and
          value["old_material_containment_or_K_or_mutual_bridge_qualification"]
          is False, "No admission authority")
    _integer(value["operations"], 0, 64_000_000)
    _integer(value["metadata_integrity_operations"], 0, 64_000_000)
    _integer(value["pruned_faces"], 0, 40_000_000)
    _integer(value["new_embedding_factory_seed_calls"], 0, 0)
    for name in ("inspection_sha256", "batch_approval_sha256"):
        _pin_shape({"bytes": 1, "sha256": value[name]}, MAX_MARKER)
    _need(type(value["status"]) is str and value["status"] in {
        "CERTIFIED_NUMERIC_COMPLETE_RETAINED_SOURCE_SUPPORT",
        "UNRESOLVED_DISPATCH_WORK_LIMIT", "FIRST_SOURCE_SUPPORT_REFUSAL",
        "FIRST_SOURCE_SUPPORT_ERROR"}, "Dispatch status")
    if value["coverage_complete"]:
        _need(value["calls_started"] == MAX_CALLS and value["status"] ==
              "CERTIFIED_NUMERIC_COMPLETE_RETAINED_SOURCE_SUPPORT", "Full dispatch count")
    if compact:
        _need(value["schema"] == SCHEMA, "Storage schema")
        _pin_shape(value["full_dispatch_pin"], MAX_BYTES)


def _row(value):
    _need(type(value) is dict and {
        "bridge_id", "source_object", "source_id", "source_face", "call_started"
    } <= set(value), "Source dispatch row")
    _need(type(value["source_id"]) is str and value["call_started"] is True,
          "Source dispatch identity")
    _integer(value["source_face"], 0, 1_000_000)
    _values({k: v for k, v in value.items() if k not in ("result", "result_ref")})


def _coverage_rows(dispatch, field):
    for i, row in enumerate(dispatch["records"]):
        _row(row)
        if field not in row:
            _need(not dispatch["coverage_complete"] and dispatch["status"] ==
                  "FIRST_SOURCE_SUPPORT_ERROR" and i == len(dispatch["records"]) - 1
                  and row.get("work_consumed_unknown") is True and
                  type(row.get("error_type")) is str and type(row.get("error")) is str,
                  "Only an explicit final error lacks a result")


def _reference(ref, index):
    _need(type(ref) is dict and set(ref) == REF_KEYS, "Closed result reference")
    _integer(ref["method_index"], index + 7, index + 7)
    _integer(ref["operation_limit"], 0, 16_000_000)
    _need(type(ref["completion_state"]) is str and
          ref["completion_state"] in ("COMPLETED", "REFUSED"), "Completion state")
    for name in ("method_key", "args_sha256"):
        _need(type(ref[name]) is str and
              re.fullmatch("[0-9a-f]{64}", ref[name]) is not None, "Literal identity")
    for name, cap in (("start_pin", MAX_MARKER), ("completion_pin", MAX_MARKER),
                      ("result_pin", MAX_RESULT)):
        _pin_shape(ref[name], cap)


def _method(index, row, start_raw, completion_raw, result_pin, method_key):
    start = _decode(start_raw, MAX_MARKER)
    end = _decode(completion_raw, MAX_MARKER)
    _need(type(start) is dict and set(start) ==
          {"index", "stage", "args", "state", "actual_authority"}, "Method intent")
    _integer(start["index"], index + 7, index + 7)
    _need(start["stage"] == "source_T" and start["state"] ==
          "ATTEMPTED_MAY_HAVE_ENTERED" and start["actual_authority"] is False,
          "Source method scope")
    args = start["args"]
    _need(type(args) is dict and set(args) == {
        "call_index", "source_id", "triangle_and_patches_sha256",
        "operation_limit", "incident_edge_policy_sha256"}, "Closed method args")
    _integer(args["call_index"], index, index)
    _integer(args["operation_limit"], 0, 16_000_000)
    _need(type(args["source_id"]) is str and args["source_id"] == row["source_id"],
          "Source identity")
    for name in ("triangle_and_patches_sha256", "incident_edge_policy_sha256"):
        _pin_shape({"bytes": 1, "sha256": args[name]}, MAX_MARKER)
    expected_key = canonical_pin({k: start[k] for k in ("index", "stage", "args")})["sha256"]
    _need(type(method_key) is str and method_key == expected_key, "Same-job method key")
    _need(type(end) is dict and set(end) ==
          {"stage", "state", "result", "actual_authority"}, "Method completion")
    _need(end["stage"] == "source_T" and end["state"] in ("COMPLETED", "REFUSED")
          and end["actual_authority"] is False, "Completed or refused source method")
    _pin_shape(result_pin, MAX_RESULT)
    _pin_shape(end["result"], MAX_RESULT)
    _need(end["result"] == result_pin, "Durable result pin")
    return {
        "method_index": start["index"], "method_key": method_key,
        "start_pin": _bytes_pin(start_raw, MAX_MARKER),
        "args_sha256": canonical_pin(args, MAX_MARKER)["sha256"],
        "operation_limit": args["operation_limit"], "result_pin": dict(result_pin),
        "completion_pin": _bytes_pin(completion_raw, MAX_MARKER),
        "completion_state": end["state"],
    }


def compact_dispatch(dispatch, bindings):
    """Bind full in-memory results to already durable, same-job method records.

    bindings maps call indices to method_key/start bytes/completion bytes/result
    pin. The owner remains responsible for stable FD/name and journal identities.
    No filesystem read, numerical method or actual clearance occurs here.
    """
    _dispatch(dispatch)
    _need(type(bindings) is dict, "Closed method bindings")
    _coverage_rows(dispatch, "result")
    _values({k: v for k, v in dispatch.items() if k != "records"})
    needed = {i for i, row in enumerate(dispatch["records"]) if "result" in row}
    _need(all(type(i) is int for i in bindings) and set(bindings) == needed,
          "No missing or foreign bindings")
    records = []
    for i, row in enumerate(dispatch["records"]):
        _need(type(row) is dict and "result_ref" not in row, "Original dispatch row")
        copied = dict(row)
        if "result" in row:
            binding = bindings[i]
            _need(type(binding) is dict and set(binding) ==
                  {"method_key", "start", "completion", "result_pin"}, "Closed binding")
            result_pin = canonical_pin(row["result"], MAX_RESULT)
            _need(result_pin == binding["result_pin"], "Full result equals durable bytes")
            copied["result_ref"] = _method(i, row, binding["start"],
                                           binding["completion"], result_pin,
                                           binding["method_key"])
            _need(not dispatch["coverage_complete"] or
                  copied["result_ref"]["completion_state"] == "COMPLETED",
                  "Full report requires completed methods")
            del copied["result"]
        records.append(copied)
    result = {**dispatch, "records": records, "schema": SCHEMA,
              "full_dispatch_pin": _stream_pin(dispatch, MAX_BYTES)}
    canonical_pin(result)
    return result


def verify_dispatch(compact, read_record):
    """Verify streamed legacy canonical equivalence using bounded same-job reads.

    read_record(method_key, fixed_filename, byte_cap) must enforce its own
    approved root and stable physical identities. Returned bytes are bounded and
    hashed here. This function returns only after every reference and the full
    canonical dispatch hash match; it grants no numerical or runtime authority.
    """
    _dispatch(compact, True)
    _coverage_rows(compact, "result_ref")
    _need(callable(read_record), "Explicit recorded reader")
    canonical_pin(compact)
    digest = hashlib.sha256()
    size = read_bytes = 0

    def emit(raw):
        nonlocal size
        size += len(raw)
        _need(size <= MAX_BYTES, "Reconstructed dispatch byte bound")
        digest.update(raw)

    def read(ref, name, field, cap):
        nonlocal read_bytes
        _pin_shape(ref[field], cap)
        _need(read_bytes + ref[field]["bytes"] <= MAX_BYTES,
              "Recorded input aggregate bound")
        raw = read_record(ref["method_key"], name, ref[field]["bytes"])
        actual = _bytes_pin(raw, ref[field]["bytes"])
        read_bytes += len(raw)
        _need(actual == ref[field], "Referenced record integrity")
        return raw

    emit(b"{")
    for key_index, key in enumerate(sorted(DISPATCH_KEYS)):
        if key_index:
            emit(b",")
        for raw in _chunks(key):
            emit(raw)
        emit(b":")
        if key != "records":
            for raw in _chunks(compact[key]):
                emit(raw)
            continue
        emit(b"[")
        for i, row in enumerate(compact["records"]):
            _row(row)
            _need(type(row) is dict and "result" not in row, "Stored reference row")
            if i:
                emit(b",")
            restored = dict(row)
            if "result_ref" in row:
                ref = row["result_ref"]
                _reference(ref, i)
                _need(not compact["coverage_complete"] or
                      ref["completion_state"] == "COMPLETED",
                      "Full report requires completed methods")
                start_raw = read(ref, "method-start.json", "start_pin", MAX_MARKER)
                end_raw = read(ref, "completion.json", "completion_pin", MAX_MARKER)
                expected = _method(i, row, start_raw, end_raw, ref["result_pin"],
                                   ref["method_key"])
                _need(expected == ref, "Exact method/reference binding")
                raw = read(ref, "result.bin", "result_pin", MAX_RESULT)
                restored["result"] = _decode(raw, MAX_RESULT)
                del restored["result_ref"]
            for raw in _chunks(restored):
                emit(raw)
        emit(b"]")
    emit(b"}")
    full_pin = {"bytes": size, "sha256": digest.hexdigest()}
    _need(full_pin == compact["full_dispatch_pin"], "Original full dispatch integrity")
    return {"schema": SCHEMA, "full_dispatch_pin": full_pin,
            "recorded_bytes_read": read_bytes, "qualification": False,
            "numerical_methods_replayed": 0}

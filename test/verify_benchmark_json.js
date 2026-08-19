"use strict";

const fs = require("node:fs");

if (process.argv.length !== 4) {
  throw new Error("usage: verify_benchmark_json.js REPORT EXPECTED_CHECKSUM");
}

const reportPath = process.argv[2];
const expectedChecksum = process.argv[3];
const report = JSON.parse(fs.readFileSync(reportPath, "utf8"));

function requireCanonicalDecimalString(field) {
  const value = report[field];
  if (typeof value !== "string" || !/^(0|[1-9][0-9]*)$/.test(value)) {
    throw new Error(`${field} is not a canonical decimal string`);
  }
  if (BigInt(value).toString() !== value) {
    throw new Error(`${field} did not round-trip through BigInt`);
  }
  return value;
}

const checksum = requireCanonicalDecimalString("checksum");
requireCanonicalDecimalString("total_bytes");

if (checksum !== expectedChecksum) {
  throw new Error(`checksum ${checksum} does not equal ${expectedChecksum}`);
}
if (BigInt(checksum) <= BigInt(Number.MAX_SAFE_INTEGER)) {
  throw new Error("checksum does not exercise the unsafe JSON integer range");
}

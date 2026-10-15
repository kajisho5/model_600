#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  tools/generate_keys.py  -  Vendor serial-key generator (FNV-1a + salt).
#
#  Produces self-validating licence keys and the KV seed file the Cloudflare
#  Worker loads. A key carries an embedded FNV-1a checksum so typos are rejected
#  instantly (offline and in the Worker) before any seat lookup.
#
#  Key format:  M600-XXXXX-XXXXX-XXXXX-CCCCC
#    - 15 payload chars (Crockford base32, ambiguous letters removed)
#    - 5  checksum chars = base32( FNV-1a64(payload + SALT) )[:5]
#
#  Usage:
#    python3 generate_keys.py --count 100 --batch 2026Q4 --seats 2 \
#            --out keys_2026Q4.json [--csv keys_2026Q4.csv]
#
#  The JSON is uploaded to the Worker's KV namespace (key -> record). The CSV is
#  for the reseller. Secrets live only here and in the Worker, never in the DLL.
# =============================================================================
import argparse
import json
import os
import secrets
import sys
from datetime import datetime, timezone

# MUST match license-manager.hpp product_salt() exactly.
SALT = b"SEVENTHWELL::MODEL600::v1::6f3a9c2e8b14d7a0"

# Crockford base32 without I, L, O, U to avoid read-aloud ambiguity.
ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = (1 << 64) - 1


def fnv1a64(data: bytes, seed: int = FNV_OFFSET) -> int:
    h = seed
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & MASK64
    return h


def to_base32(n: int, length: int) -> str:
    out = []
    for _ in range(length):
        out.append(ALPHABET[n & 31])
        n >>= 5
    return "".join(reversed(out))


def random_payload() -> str:
    # 15 chars of cryptographically-strong randomness over the alphabet.
    return "".join(ALPHABET[secrets.randbelow(32)] for _ in range(15))


def checksum(payload: str) -> str:
    tag = fnv1a64(payload.encode("ascii") + SALT)
    return to_base32(tag, 5)


def format_key(payload: str) -> str:
    c = checksum(payload)
    body = payload + c  # 20 chars
    groups = [body[0:5], body[5:10], body[10:15], body[15:20]]
    return "M600-" + "-".join(groups)


def validate_key(key: str) -> bool:
    compact = key.replace("-", "").upper()
    if compact.startswith("M600"):
        compact = compact[4:]
    if len(compact) != 20:
        return False
    payload, c = compact[:15], compact[15:]
    return checksum(payload) == c


def main() -> int:
    ap = argparse.ArgumentParser(description="MODEL 600 licence key generator")
    ap.add_argument("--count", type=int, default=10, help="number of keys to generate")
    ap.add_argument("--batch", type=str, default=datetime.now(timezone.utc).strftime("%Y%m%d"),
                    help="batch label stored with each key")
    ap.add_argument("--seats", type=int, default=2, help="activation seat limit per key")
    ap.add_argument("--out", type=str, required=True, help="KV seed JSON output path")
    ap.add_argument("--csv", type=str, default="", help="optional reseller CSV output path")
    args = ap.parse_args()

    if args.count < 1 or args.count > 1_000_000:
        print("count out of range", file=sys.stderr)
        return 2

    records = {}
    keys = []
    seen = set()
    while len(keys) < args.count:
        key = format_key(random_payload())
        if key in seen:
            continue
        seen.add(key)
        keys.append(key)
        records[key] = {
            "product": "MODEL600",
            "batch": args.batch,
            "seats_max": args.seats,
            "seats": {},          # machine_id -> activated_at (filled by the Worker)
            "revoked": False,
            "created": datetime.now(timezone.utc).isoformat(),
        }

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump({"salt_fingerprint": fnv1a64(SALT) & 0xFFFFFFFF,
                   "batch": args.batch, "count": len(keys), "keys": records},
                  f, indent=2, ensure_ascii=False)

    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as f:
            f.write("key,batch,seats_max\n")
            for k in keys:
                f.write(f"{k},{args.batch},{args.seats}\n")

    # Sanity: every generated key must validate.
    bad = [k for k in keys if not validate_key(k)]
    if bad:
        print(f"INTERNAL ERROR: {len(bad)} keys failed self-validation", file=sys.stderr)
        return 1

    print(f"Generated {len(keys)} keys (batch {args.batch}, {args.seats} seats).")
    print(f"KV seed : {args.out}")
    if args.csv:
        print(f"CSV     : {args.csv}")
    print("Sample  :", keys[0], "(valid)" if validate_key(keys[0]) else "(INVALID)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

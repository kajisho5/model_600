// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  tools/cloudflare_worker_auth.js
//
//  Cloudflare Worker: online activation with a 2-device seat limit and a signed
//  offline token. The token it issues is byte-for-byte identical to the one
//  license-manager.hpp recomputes locally, so online and offline validation
//  agree. Secrets (PEPPER) come from Worker env bindings, never the client.
//
//  Routes (POST, JSON):
//    /activate    { key, machine, product, client_time } -> { ok, token, seats_used, seats_max }
//    /deactivate  { key, machine }                        -> { ok }
//    /verify      { key, machine, token }                 -> { ok }
//
//  Bindings (wrangler.toml):
//    [[kv_namespaces]] binding = "M600_KEYS"      # key -> record (see generate_keys.py)
//    [vars] PRODUCT_SALT = "SEVENTHWELL::MODEL600::v1::6f3a9c2e8b14d7a0"
//    [secrets] OFFLINE_PEPPER = "M600-OFFLINE-PEPPER-{a1b2c3d4e5f60718}"
// =============================================================================
'use strict';

const FNV_OFFSET = 0xcbf29ce484222325n;
const FNV_PRIME = 0x100000001b3n;
const MASK64 = (1n << 64n) - 1n;

function fnv1a64(bytes, seed = FNV_OFFSET) {
  let h = seed;
  for (let i = 0; i < bytes.length; i++) {
    h ^= BigInt(bytes[i]);
    h = (h * FNV_PRIME) & MASK64;
  }
  return h;
}

function u64le(n) {
  const out = new Uint8Array(8);
  let v = n & MASK64;
  for (let i = 0; i < 8; i++) { out[i] = Number(v & 0xffn); v >>= 8n; }
  return out;
}

function base64url(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function concatBytes(...arrs) {
  let n = 0;
  for (const a of arrs) n += a.length;
  const out = new Uint8Array(n);
  let o = 0;
  for (const a of arrs) { out.set(a, o); o += a.length; }
  return out;
}

const enc = new TextEncoder();

// Must match license-manager.hpp make_offline_signature() exactly:
//   sha = SHA256(key | "|" | machine | "|" | pepper)
//   seed = fnv1a64(salt)
//   tag  = fnv1a64(sha, seed)  (uint64, little-endian 8 bytes)
//   token = base64url(sha(32) + tag(8))   // no padding
async function makeToken(key, machine, salt, pepper) {
  const msg = concatBytes(enc.encode(key), enc.encode('|'), enc.encode(machine),
                          enc.encode('|'), enc.encode(pepper));
  const shaBuf = await crypto.subtle.digest('SHA-256', msg);
  const sha = new Uint8Array(shaBuf);
  const seed = fnv1a64(enc.encode(salt));
  const tag = fnv1a64(sha, seed);
  return base64url(concatBytes(sha, u64le(tag)));
}

// Self-validating key: payload(15) + checksum(5), FNV-1a over payload+salt.
const ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
function toBase32(n, len) {
  let out = '';
  for (let i = 0; i < len; i++) { out = ALPHABET[Number(n & 31n)] + out; n >>= 5n; }
  return out;
}
function keyChecksumValid(key, salt) {
  let c = key.toUpperCase().replace(/-/g, '');
  if (c.startsWith('M600')) c = c.slice(4);
  if (c.length !== 20) return false;
  const payload = c.slice(0, 15), chk = c.slice(15);
  const tag = fnv1a64(concatBytes(enc.encode(payload), enc.encode(salt)));
  return toBase32(tag, 5) === chk;
}

function normalizeKey(raw) {
  // Mirror the client sanitiser: full-width -> ASCII, drop spaces/hyphens, upper.
  let out = '';
  for (const ch of (raw || '')) {
    let c = ch.codePointAt(0);
    if (c >= 0xff01 && c <= 0xff5e) c = c - 0xff01 + 0x21;
    if (c === 0x3000) continue;
    if (c === 0x2010 || c === 0x2011 || c === 0x2012 || c === 0x2013 ||
        c === 0x2014 || c === 0x2015 || c === 0xff0d || c === 0x2d) continue;
    if (c === 0x20 || c === 0x09 || c === 0x0a || c === 0x0d) continue;
    out += String.fromCodePoint(c).toUpperCase();
  }
  return out;
}

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' },
  });
}

async function readBody(request) {
  try { return await request.json(); } catch { return {}; }
}

export default {
  async fetch(request, env) {
    if (request.method !== 'POST') return json({ ok: false, reason: 'method' }, 405);
    const url = new URL(request.url);
    const salt = env.PRODUCT_SALT || 'SEVENTHWELL::MODEL600::v1::6f3a9c2e8b14d7a0';
    const pepper = env.OFFLINE_PEPPER || '';
    if (!pepper) return json({ ok: false, reason: 'server_misconfigured' }, 500);

    const body = await readBody(request);
    const key = normalizeKey(body.key);
    const machine = (body.machine || '').toString().slice(0, 64);
    if (!key || !machine) return json({ ok: false, reason: 'missing_fields' }, 400);
    if (!keyChecksumValid(key, salt)) return json({ ok: false, reason: 'invalid' });

    if (url.pathname === '/activate') {
      const raw = await env.M600_KEYS.get(key);
      if (!raw) return json({ ok: false, reason: 'unknown_key' });
      const rec = JSON.parse(raw);
      if (rec.revoked) return json({ ok: false, reason: 'revoked' });
      const seatsMax = rec.seats_max || 2;
      rec.seats = rec.seats || {};
      const now = new Date().toISOString();
      if (!(machine in rec.seats)) {
        if (Object.keys(rec.seats).length >= seatsMax) {
          return json({ ok: false, reason: 'seat_limit',
                        seats_used: Object.keys(rec.seats).length, seats_max: seatsMax });
        }
        rec.seats[machine] = now;
        await env.M600_KEYS.put(key, JSON.stringify(rec));
      } else {
        rec.seats[machine] = now; // Refresh timestamp.
        await env.M600_KEYS.put(key, JSON.stringify(rec));
      }
      const token = await makeToken(key, machine, salt, pepper);
      return json({ ok: true, token, seats_used: Object.keys(rec.seats).length, seats_max: seatsMax });
    }

    if (url.pathname === '/deactivate') {
      const raw = await env.M600_KEYS.get(key);
      if (!raw) return json({ ok: true }); // Nothing to free.
      const rec = JSON.parse(raw);
      if (rec.seats && machine in rec.seats) {
        delete rec.seats[machine];
        await env.M600_KEYS.put(key, JSON.stringify(rec));
      }
      return json({ ok: true, seats_used: rec.seats ? Object.keys(rec.seats).length : 0,
                    seats_max: rec.seats_max || 2 });
    }

    if (url.pathname === '/verify') {
      const token = (body.token || '').toString();
      const expected = await makeToken(key, machine, salt, pepper);
      return json({ ok: token === expected });
    }

    return json({ ok: false, reason: 'not_found' }, 404);
  },
};

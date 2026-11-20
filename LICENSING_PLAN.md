# Licensing & Superadmin Plan

Goal: when someone opens the exe, it asks for a secret key. The key is validated by our server. A key can be used once, on one device only, and is valid for a fixed time window (e.g. 3 hours). We manage keys from a secured superadmin page.

Windows port isolation applies: new work goes in `admin/` (new) and `windows/` + `shared/`. Linux files are never edited.

---

## Decisions

| Topic | Choice | Why |
|---|---|---|
| Database | Supabase (Postgres) | Atomic one-time redemption, relational data, audit logs. Not Mongo/Firebase. |
| Hosting | Vercel (single Next.js app) | No sleep like Render free tier. UI and API in one deploy. HTTP heartbeat needs no WebSockets. |
| Backend | Next.js route handlers (no separate server) | One repo, one deploy. |
| Admin stack | Next.js + TypeScript + Tailwind + Framer Motion | As requested. |
| Admin auth | Env username + password hash, signed httpOnly cookie | Simple and safe for a single admin. |
| Exe auth | Ed25519 signed short-lived tokens | Public key can be embedded safely. |
| Rate limiting | Upstash Redis | Blocks brute force on activate/login. |

## Architecture

```
Admin UI (Next.js) ─┐
                    ├─ Next.js route handlers (Vercel) ── Supabase Postgres
Exe ── /api/activate ┤
    ── /api/heartbeat┘
```

Rules:
- The exe never talks to Supabase directly. Only the server, with the service-role key.
- Keys are stored as SHA-256 hashes. Plain key is shown once at creation.
- Private signing key lives only in Vercel env vars. The exe embeds the public key only.
- Admin routes (`/api/admin/*`) are separate from exe routes (`/api/activate`, `/api/heartbeat`).

---

## Phase 1: Foundation and database

- Create `admin/` Next.js app (TS, Tailwind, Framer Motion).
- Create Supabase project; enable RLS with no public policies.
- Schema:
  - `keys`: id, key_hash (unique), duration_minutes, max_devices, device_id, activated_at, expires_at, revoked, note, created_at
  - `activations`: id, key_id, device_id, ip, user_agent, created_at, result
- Pooled connection string; server-only DB client.
- `.env.example` with all required variables.
- Generate the Ed25519 keypair.

Done when: app runs locally and connects to the database.

## Phase 2: Admin authentication

- Login page with username + password (bcrypt/argon2 hash in env).
- Signed httpOnly, Secure, SameSite=Strict session cookie (`jose`).
- Middleware protecting all admin pages and `/api/admin/*`.
- Rate limiting on login (Upstash).
- Logout and session expiry.

Done when: unauthenticated requests to any admin page or API are rejected.

## Phase 3: Key management API and UI

- Create keys: duration, device limit, note, bulk count. Plain key shown once.
- List and filter keys: unused, active, expired, revoked.
- Revoke, reset device binding, extend time.
- Activation log view.
- UI polish with Framer Motion (transitions, list animations, toasts).

Done when: the admin can fully manage keys from the browser.

## Phase 4: Client API (activate and heartbeat)

- `POST /api/activate` `{key, deviceId}`:
  - Atomic bind: `UPDATE ... WHERE key_hash=$ AND device_id IS NULL AND NOT revoked RETURNING *`.
  - Same device re-activating an active key is allowed; a different device is rejected.
  - Sets `expires_at = now() + duration`. Uses server time only.
  - Returns a signed token (about 10 min lifetime) with deviceId and expiry.
- `POST /api/heartbeat` `{token}`: re-checks revoked/expired and issues a fresh token.
- Rate limiting per IP and per key attempt.
- Logs every attempt to `activations`.

Done when: concurrent activation attempts yield exactly one winner; expiry and revoke are enforced.

## Phase 5: Windows exe integration

- Startup key prompt (GUI or console, matching the existing UI).
- Device ID: hash of machine GUID plus other hardware identifiers.
- Embed the public key; verify tokens locally.
- Heartbeat loop; on failure or expiry, close the app with a clear message.
- Store the token securely (DPAPI); handle offline gracefully within token lifetime.
- Clock tampering check: rely on server `expires_at` and heartbeat, not the local clock alone.
- Only `windows/` and `shared/` are changed.

Done when: the exe will not run without a valid key, and stops when the window ends or the key is revoked.

## Phase 6: Hardening

- Optional TOTP 2FA for admin.
- Security headers, CORS locked down, input validation (zod).
- Server-side value: deliver something the exe needs (e.g. a config decryption key) inside the token, so patching out the check is not enough.
- Obfuscation and code signing for the exe.
- Audit log review, key-leak response (revoke and rotate signing key).

## Phase 7: Deploy and operate

- Deploy to Vercel; set env vars (admin creds hash, session secret, Supabase, Ed25519 private key, Upstash).
- Custom domain and HTTPS.
- Test the full flow against production.
- Backups (Supabase) and a short runbook: create key, revoke key, rotate signing key.

---

## Known limits

A determined attacker can still patch the exe. The mitigation is Phase 6 server-side value, plus short tokens and heartbeat revocation, which make a leaked exe useless without a valid key.

## Open questions

- Default key duration and activation deadline (e.g. must activate within 24h)?
- Should the same device be allowed to re-enter the key after a restart within the window? (Plan assumes yes.)
- GUI or console key prompt in the exe?

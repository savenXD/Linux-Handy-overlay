# Method, provenance, and artifact inventory

## Tooling and boundaries

The user explicitly requested REA from <https://github.com/morluto/rea.git>. The installed skill was `/home/saven/.codex/skills/reverse-engineer-anything/SKILL.md`, with JavaScript/native references consulted. REA supplied packaging inspection, V8 header/string inspection, native imports/helper analysis, Ghidra analysis, and Electron fuse evidence. Supplemental offline V8 recovery was necessary because the initial REA pass did not decompile the cached bytecode.

No CodeTantra code was executed on the Linux analysis host. Installer extraction and ASAR byte-range extraction were inert. No login, credentials, account changes, application patches, or deployment were involved. REA evidence was exported; binary resources were closed and investigation memory reset. The supplied Windows log is the user's runtime evidence, collected separately.

## Artifact identity

| Artifact | SHA-256 |
| --- | --- |
| Supplied installer | `a92b68ebbdb2cd163dd6e32fb22c7e0f11f15e516155f1627f1ae8d09e8e7ec1` |
| Main CodeTantra-SEA.exe | `26e13fc1f9258c33912c387358366b2999e89949f7ffd0ced603de8cb5ac4ae4` |
| resources/app.asar | `08f7d1323158ec6c30d2ff8763c18cf64c42b026ba225e8d2e55bd1ab4a54335` |
| out/main/index.jsc | `0cd680a356735566551cc2ab2baa27f8354b7532045a49e67b79e3a3ab42634e` |
| v8_context_snapshot.bin | `1fa1cdd08423809344fe15fac2d9d1ec604cc3ea5af4b944d89a51b1e98c8665` |

Installer: `/home/saven/Downloads/CodeTantra-SEA-x64-5.2.0.exe`. It is a 32-bit NSIS installer containing `$PLUGINSDIR/app-64.7z`; its installed payload is x64. Extracted main executable is 211,252,328 bytes. Manifest: `codetantra-sea`, version `5.2.0`, entry `./out/main/index.js`. The loader uses `vm.Script` cachedData from `.jsc`, so reading the loader alone misses main logic.

Electron version is 39.8.10, Chromium 142.0.7444.265, V8 14.2.231.22. [Official Electron release metadata](https://releases.electronjs.org/release/v39.8.10).

## Offline V8 recovery

Tool: [aynakeya/v8asm](https://github.com/aynakeya/v8asm), commit `0d1e8a9d05618f459c96b1e1eddb2bf6b95bc8f5`. A matching V8 14.2.231.22 profile was built from official V8 source headers because the shipped profiles ended earlier. Python standard library and system C preprocessor were used; target code was not evaluated.

Relevant adaptations are preserved in `evidence/v8asm-version-support.patch`:

- Runtime macro generation supports the additional argument in `OFFLINE_F`/`OFFLINE_I`.
- AccessorPair fields are read from their newer source location/layout.
- Nested `CALL_PROPERTY_BYTECODES(V)` expands in the correct opcode order.
- Static root definitions include the referenced intl/wasm table.
- The pseudocode layer lowers the newer `NoCell` context load/store instructions to its corresponding existing context representations, including Wide forms. Original decoded instructions remain in bytecode evidence.

The packaged snapshot parsed with version `14.2.231.22-electron.0`. Its magic/version/checksum matched the code cache; version hash is `0x8f50e237`, snapshot checksum 1377191331. Recovery covered 16,032 serialized objects, 1,175 bytecode arrays, and 80,514 instructions. All instruction raw-byte spans matched the input and all decoded jump targets landed on instruction boundaries: zero structural validation issues. This does not certify every pseudocode semantic or establish runtime reachability of every function. Some operations remain unsupported.

Synthetic `0xf000...` values identify serializer objects, not live process addresses. Selected function identifiers: `Lt` `0xf000000c3f00`, `Ff` `0xf000002f5b00`, `Zf` `0xf00000311900`, recovery callback `0xf00000312a00`.

Twelve unpacked native companions differed from ASAR-declared hashes. REA used record-and-continue with mismatches retained. The cause was not established; signing/stripping is only a possibility. Do not turn this into an unsupported malware finding or assume it invalidates every inspected byte.

## Preserved evidence in this folder

- `evidence/main-recovered-pseudocode.js`: approximately 3.6 MiB, final snapshot-assisted recovery, with line numbers referenced by these documents.
- `evidence/secure-mode-bytecode.json`: selected functions, instructions, constant pools, and resolved referenced names.
- `evidence/recovery-validation.json`: parser metadata, counts, hashes, and validation outcome.
- `evidence/v8-14.2.231.22-profile.json`, `v8-profile-source-hashes.json`, `v8asm-version-support.patch`: profile and recovery provenance. The patch alone is not a complete one-command reconstruction environment.
- `evidence/secure-mode-rea-evidence.json`: second-pass REA export, seven records.
- `evidence/capture-state-20261113-092520.jsonl`: unchanged user log; interpretation in the runtime document.
- `evidence/CaptureStateProbe.ps1`, `Run-CaptureStateProbe.cmd`: original read-only diagnostic.
- `evidence/SHA256SUMS`: hashes of preserved local evidence files.

## Larger external evidence and temporary work

Original analysis directory: `/home/saven/.codex/visualizations/2026/11/13/01a1d5ce-ca40-7752-9991-7ac90733d2b9/codetantra-analysis`.

It retains the approximately 91 MiB first-pass `rea-evidence.json` and the historical `findings.md`, which predates decoded control flow. Its claims of unresolved capture call sites are superseded by the recovered implementation and runtime log. The historical report is not the current authority. Large raw bundles were not duplicated into the source repository.

Temporary work paths (not durable):

- `/tmp/codetantra-520-installer/$PLUGINSDIR/app-64.7z`
- `/tmp/codetantra-520-app/` and `/tmp/codetantra-520-source/`
- `/tmp/codetantra-v8asm/`, including `prepare_codetantra_profile.py`
- `/tmp/codetantra-v8-source-14.2.231.22/`
- `/tmp/codetantra-main-disasm-snapshot.json` (approximately 105 MiB full structured output)
- `/tmp/codetantra-main-recovered.js` (the preserved evidence copy is preferable)

Earlier intermediate `...disasm.json`/`...decompiled.js` outputs are less complete; use the final snapshot-assisted recovery.

## Selected REA evidence IDs

| Observation | Evidence ID |
| --- | --- |
| Main executable imports, first pass | `ev_0c5655d5f2530f50acef3fefba985c0a2b6b3a168daa1e7df5b8987262a95188` |
| Main V8 header inspection, first pass | `ev_904867cb4a5a760ae0d9b218f303bc7a352fd72f0e34c636aee3bf40116fa1f8` |
| WinKeyServer imports, first pass | `ev_58847814bfcd5d3555a1a69eb02eaf6f332d6def2d28f57ed93625fb473eda84` |
| fileManagerHost imports | `ev_a48ed17a2c577f2a90e6a2892291442552c0de52fa0adab987f6d532ff82d92f` |
| fileManagerHost string search | `ev_fe2e5f79d6cbda1da2763f0c47e72c97e4bbbfe3755d08dddf3cfb8ab322e813` |
| fileManagerHost xrefs | `ev_a76c1ff82b6cba8f7b57634ae323b5764380df945fc6a726670a98cb33f2761c` |
| fileManagerHost analyzed function | `ev_e55134eaa43df7df4c1db6aad5efe760c0c96278f988c73feb8df593c8f4eed5` |
| Electron fuses | `ev_1e8282d518b9db6bbbcc15ed54ea80961433a8b131ad06000717f77772b70795` |

REA exports do not contain the supplemental Python recovery as REA-native observations; keep that provenance distinction explicit.

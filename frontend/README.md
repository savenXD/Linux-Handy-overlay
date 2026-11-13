# Cronq download frontend

A static 404-style page with a hidden Windows installer download. No build tools or runtime dependencies.

From the repository root, run:

```powershell
python -m http.server 5174 --bind 127.0.0.1 --directory frontend
```

Open http://127.0.0.1:5174 in your browser. Double-click **HTTP ERROR 404**, enter the existing secret code,
and select Continue. The code and encrypted URL remain unchanged.

Use `frontend` as the static site directory. The gate unlocks
`https://github.com/savenXD/Linux-Handy-overlay/releases/latest/download/cronq-setup.exe`.
Publishing a new non-prerelease GitHub release marked **Latest**, with the installer asset named
`cronq-setup.exe`, updates the download without redeploying the page or changing its encrypted blob.
The version in `windows/VERSION`, tag, release notes and installer must agree. The Windows workflow
builds/tests the source, creates the installer and checksum, then publishes those release assets.

Run `node frontend/test-download.mjs` to test the actual gate script. Set `CRONQ_DOWNLOAD_CODE` locally
to include the real-code and latest-URL checks. The test does not print or persist the code and does
not navigate or download anything. Verify the published installer's SHA-256 against `SHA256SUMS.txt`
after release; a successful source push alone does not update the downloadable installer.

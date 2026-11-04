# Cronq download frontend

A static page with the Cronq name and a Windows download button. No build tools or dependencies.

From the repository root, run:

```powershell
python -m http.server 5174 --bind 127.0.0.1 --directory frontend
```

Open http://127.0.0.1:5174 in your browser.

For hosting later, use `frontend` as the static site directory. The download button points to
the latest published GitHub release asset named `cronq-setup.exe`. Publish a new release with that
same asset name to update the download. The Windows v1.0.0 release packages the current Windows features.

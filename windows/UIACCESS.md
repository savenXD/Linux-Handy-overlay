# Cronq UIAccess Guide: Overlaying Elevated (Administrator) Windows

By default, Windows User Interface Privilege Isolation (UIPI) prevents standard-privilege processes from sending input to, intercepting keys from, or staying on top of windows running with Administrator rights.

For power users who need Cronq to overlay elevated applications (e.g., elevated command prompts, Task Manager, admin utilities), Windows provides the **UIAccess** privilege.

UIAccess is not a general override for display-affinity restrictions, protected
video or secure-desktop capture. No UIAccess or administrator-mode fix has been
demonstrated for Deepak's focus app. Its screenshot issue remains open; follow
the [screenshot investigation](SCREENSHOT_INVESTIGATION.md) rather than treating
this signing/install procedure as a capture-protection solution.

---

## Prerequisites for UIAccess

To run with `uiAccess="true"`, Windows requires:
1. The executable must be embedded with an application manifest containing `uiAccess="true"`.
2. The executable must be **digitally signed** by a certificate trusted on the local machine.
3. The executable must run from a **secure location** (such as `%ProgramFiles%` or `%SystemRoot%\system32`).

---

## Step 1: Build the UIAccess Binary

Enable the `CRONQ_UIACCESS` option when configuring CMake:

```powershell
cmake -S windows -B windows/build -A x64 -DCRONQ_UIACCESS=ON
cmake --build windows/build --config Release --target cronqw_uia
```

This embeds `windows/installer/cronqw-uia.manifest` into `cronqw_uia.exe`.

---

## Step 2: Create a Self-Signed Certificate and Trust It

In an **Administrator PowerShell** window:

```powershell
# 1. Create a self-signed code-signing certificate
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject "CN=Cronq Local Development" `
    -CertStoreLocation "Cert:\CurrentUser\My"

# 2. Export the public key to a temporary file
$certPath = "$env:TEMP\CronqLocalDev.cer"
Export-Certificate -Cert $cert -FilePath $certPath

# 3. Import certificate into the Trusted Root Certification Authorities store
Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root"

# 4. Cleanup temporary file
Remove-Item $certPath
```

---

## Step 3: Sign the Executable

Sign `cronqw_uia.exe` using `signtool.exe` (included in the Windows SDK):

```powershell
signtool.exe sign /n "Cronq Local Development" /fd SHA256 /t http://timestamp.digicert.com windows\build\Release\cronqw_uia.exe
```

---

## Step 4: Install to a Secure Directory

UIAccess executables will fail with `ERROR_ACCESS_DENIED` if run from user directories (like `%USERPROFILE%` or Downloads). Copy the binary to `Program Files`:

```powershell
$targetDir = "$env:ProgramFiles\Cronq"
New-Item -ItemType Directory -Path $targetDir -Force
Copy-Item "windows\build\Release\cronqw_uia.exe" "$targetDir\cronqw.exe" -Force
Copy-Item "windows\build\Release\cronq.exe" "$targetDir\cronq.exe" -Force
Copy-Item "windows\build\Release\WebView2Loader.dll" "$targetDir\" -Force
Copy-Item -Path "windows\ui" -Destination "$targetDir\ui" -Recurse -Force
```

---

## Verification

Launch `$env:ProgramFiles\Cronq\cronqw.exe`.

1. Open an elevated Command Prompt (`cmd.exe` as Administrator).
2. Bring the elevated window to the foreground.
3. Notice that the Cronq overlay remains visible on top.
4. Alt+X, Alt+G, and global hotkeys work even while interacting with the elevated window.

# XAVATAR

<p align="center">
  <img src="assets/logo.png" alt="XAVATAR logo" width="240" />
</p>

XAVATAR is an avatar studio payload for jailbroken PS4 consoles. Create, crop, save, and install custom avatars through a browser on port **4002**.

Developed by **Haider A.H (c)**.

## Download

Download the latest version from the [Releases Page](https://github.com/h5d7r/XAVATAR/releases/latest).

| File | Purpose |
| --- | --- |
| `XAVATAR.elf` | Run the avatar studio directly. |
| `XAVATAR-installer.elf` | Install the payload to `/data/payloads/` and start the studio. |

Only one ELF needs to be loaded. No HTML, CSS, or JavaScript files need to be transferred to the console.

## How to Run

### Method 1: Network Injection

Enable your console's ELF receiver and choose a network sender:

- [`payload_sender.py`](payload_sender.py), supplied with XAVATAR.
- [Al-Azif/hermes-link](https://github.com/Al-Azif/hermes-link/releases/latest).

For example, send the installer with:

```sh
python payload_sender.py --host YOUR-PS4-IP --port 9021 --file XAVATAR-installer.elf
```

To run without installing, select `XAVATAR.elf` instead. Use the port configured by your loader; `9021` is the script's default.

### Method 2: On-Console Execution

Copy the ELF by FTP into `/data/payloads/`, or use `usb/payloads/` on your USB drive. Select it from an ELF-compatible release or mode of your launcher:

- [GoldHEN v2.4b18.10+](https://ko-fi.com/s/d64a916507)
- [Vuemony/vue-after-free](https://github.com/Vuemony/vue-after-free/releases/latest), full version
- [Al-Azif/ps4-payload-guest](https://github.com/Al-Azif/ps4-payload-guest/releases/latest)

The chosen receiver or launcher must support ELF payloads. Console and launcher combinations still require on-device verification.

### Installer

Run `XAVATAR-installer.elf` once to:

1. Install `XAVATAR.elf` to `/data/payloads/XAVATAR.elf`.
2. Back up a previous version before replacing it.
3. Start the bundled XAVATAR service on port **4002**.

The installed ELF can be selected from your payload launcher for later sessions. Installation does not configure automatic startup after a reboot. Avoid loading a second instance while XAVATAR is already running.

### Notifications

Notifications include the developer/version line:

```text
Haider A.H (c) 1.1.0V
```

When you create or install an avatar, the notification shows the avatar image itself. Other messages (startup, service status, and errors) use the bundled XAVATAR logo. Images are exported automatically to `/user/data/XAVATAR/`, a location intended for ShellUI image access, so no manual image transfer is needed. If icon preparation or notification submission fails, the payload falls back to the system icon while keeping the credit line.

## How to Use

Open **`http://YOUR-PS4-IP:4002`** on a phone or computer connected to the same network.

| Option | Result |
| --- | --- |
| **Install without saving** | Create an avatar and apply it to a selected user without saving a package. |
| **Create XAVATAR file** | Save a reusable `.xavatar` package to `/data/AVATARS`. |
| **Create & install** | Apply the avatar and save a copy to the library. |

Choose an image from your device, or paste a link to one, adjust its square crop, choose a PS4 user when installing, and complete the operation. The **Library** displays saved avatars and lets you install them for any available user.

### Image links

Paste a direct `http://` or `https://` link to a PNG, JPG or WebP image and select **Load**. The console downloads the image itself, so the link works even when the image host does not allow cross-origin browser access. Links up to 8 MiB are supported, and animated GIFs are not supported through links. If a link still cannot be loaded, download the image and choose the file instead.

### Where to find avatars

Looking for ideas? The [PSPrices PS4 avatars collection](https://psprices.com/region-us/collection/avatars) lists avatars by game. Save an image you like to your phone or computer, or copy its address, then use **Choose image** or the image-link field above.

### Library files

- **Upload XAVATAR** adds a `.xavatar` file from your phone or computer to `/data/AVATARS`. Choose its library name before uploading. Files up to 4 MiB are checked for valid avatar images and textures; existing filenames are never overwritten.
- **Download** saves an exact copy of a library file to your phone or computer.
- **Delete** asks for confirmation before removing a library file. This does not change an avatar already installed on a PS4 profile. Download a copy first if you want to keep it.

### Interface language

The language menu defaults to **Automatic (PS4)**. The payload reads the console's system language using `sceSystemServiceParamGetInt`; the browser's language is not used. Regional English, French, Spanish and Portuguese settings share their corresponding translation; both Chinese settings use the bundled Chinese translation. Unsupported console languages or a failed language read fall back to English.

The 11 interface languages are **English, Arabic, French, Spanish, German, Italian, Portuguese, Russian, Turkish, Japanese and Chinese**. Arabic uses a right-to-left layout. Select a language manually at any time, or return to **Automatic (PS4)**. The preference is saved in that browser for the console's address and automatic mode checks the console language again on each status refresh. System notifications remain in English.

These features are included in **v1.1.0**, available from the [Releases Page](https://github.com/h5d7r/XAVATAR/releases/latest).

Sign out and back in after installation to refresh the console's avatar. Existing profile metadata is preserved, and replaced images are backed up automatically.

### Update check

The interface compares its version with the latest GitHub release and shows a banner with a download link when a newer version exists. The check runs once per page load from your browser and contacts only the GitHub API.

### Stop the Service

Use the power button **Stop service** in the top bar to close port **4002** and end the payload. The interface stops reconnecting automatically and displays a stopped status. Load the normal payload or installer again when you want to reopen the studio, then select **Check again** in the browser.

## How to Build

### Windows

Requires WSL with an Ubuntu distribution and `make`, `python3`, `curl`, `unzip`, `clang-18`, and `lld-18` installed inside Ubuntu.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

On first run, the script downloads and verifies the pinned SDK, then builds the normal payload, installer, and release bundle. Use `-Clean` to rebuild generated files, or `-Distribution Ubuntu-24.04` to choose another installed WSL distribution.

### Native Linux

```sh
sudo apt install make python3 curl unzip clang-18 lld-18
git clone https://github.com/h5d7r/XAVATAR.git
cd XAVATAR
bash tools/build.sh
```

Payloads are written to `build/`; release archives are written to `dist/`.

Native builds pass and the service was checked locally. Physical PS4 testing is still required for firmware, loader, permissions, and shell refresh behavior.

## License

GPL-3.0-or-later. All dependency notices are consolidated in [THIRD_PARTY.md](THIRD_PARTY.md).

XAVATAR v1.1.0 adds image-link loading and avatar notifications to the self-contained PS4 avatar studio on port **4002**.

### New in v1.1.0

- **Image links:** paste a direct `http://` or `https://` link to a PNG, JPG or WebP image and select **Load**. The console downloads the image itself, so the link works even when the image host does not allow cross-origin browser access. Links up to 8 MiB are supported, and animated GIFs are not supported through links. If a link still cannot be loaded, download the image and choose the file instead.
- **Avatar notifications:** creating or installing an avatar now shows the avatar image itself in the system notification. Startup, service-status, and error notifications keep the XAVATAR logo. Each avatar is exported under its own content-hashed file in `/user/data/XAVATAR/`, so the notification always reflects the newest avatar instead of a cached earlier one.

### Downloads

- **XAVATAR.elf** — run the studio directly.
- **XAVATAR-installer.elf** — install `/data/payloads/XAVATAR.elf`, back up an existing version, and start the studio.
- **XAVATAR-1.1.0.zip** — both payloads, sender, logo, README, and notices.
- **XAVATAR-source.zip** — editable source and build helpers.
- **SHA256SUMS.txt** — download checksums.

Send one ELF to a compatible PS4 payload loader, then open `http://YOUR-PS4-IP:4002` from a device on the same network. No frontend-file transfer is required. Sign out and back in after installing an avatar to refresh it. Use **Stop service** to close port 4002.

Notifications include **Haider A.H (c) 1.1.0V**. System notifications remain in English.

### Validation

Native payload and installer builds pass. Local checks cover the new image-link flow (including the console-side fetch endpoint), avatar notification export with per-image filenames, the existing library upload/download/deletion, package validation, filename collisions, installed-profile preservation, PS4 language mapping and fallbacks, and all 11 languages on desktop and mobile. Physical PS4 verification is still required for firmware/loader behavior, outbound link fetching over HTTPS on the console, actual notification icon rendering (including avatar images), and visible avatar refresh.

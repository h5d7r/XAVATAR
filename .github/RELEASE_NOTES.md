XAVATAR is a self-contained PS4 avatar studio on port **4002**.

### Downloads

- **XAVATAR.elf** — run the studio directly.
- **XAVATAR-installer.elf** — install `/data/payloads/XAVATAR.elf`, back up an existing version, and start the studio immediately.
- **XAVATAR-1.0.2.zip** — both payloads, sender, logo, README, and notices.
- **XAVATAR-source.zip** — editable source, frontend, bundled dependencies, and build helpers.
- **SHA256SUMS.txt** — download checksums.

### Use

Send one ELF to a compatible PS4 payload loader. Wait for the ready notification, then open `http://YOUR-PS4-IP:4002` on a phone or computer on the same network. No frontend-file transfer is required.

Create and install without saving, save a `.xavatar` file, or combine both operations. The avatar library supports previews and per-user installation. Sign out and back in to refresh the console avatar.

Use **Stop service** in the top bar to close port 4002 and exit the payload. Load the payload again to reopen it. The repository and archives no longer contain the `docs`, `tests`, or `licenses` directories; dependency notices are preserved in the root `THIRD_PARTY.md` file.

### Branding and notifications

The original blue/cyan XAVATAR logo is included in the README, interface, favicon, and both ELFs. The payload exports its notification image automatically to `/user/data/XAVATAR/notification.png` and uses it in notification requests. Every notification includes **Haider A.H (c) 1.0.2V**. The system icon is used if image preparation or notification submission fails.

### Validation

Native PS4 ELF builds pass. Creation, installation, rollback, the installer, service shutdown, notification request fields, credit formatting, and automatic logo export were checked locally. The desktop/mobile interface was checked as well. Physical PS4 testing is still required for firmware/loader behavior, actual notification icon rendering, and the final visible avatar refresh.

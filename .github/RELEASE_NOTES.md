XAVATAR v1.0.3 adds library file management and 11 interface languages to the self-contained PS4 avatar studio on port **4002**.

### New in v1.0.3

- **Upload XAVATAR** imports a valid `.xavatar` package up to 4 MiB into the console's library. Choose a name before uploading; existing files are never overwritten.
- **Download** saves an exact copy of a library package to your phone or computer.
- **Delete** asks for confirmation before removing a library package. Already installed profile avatars stay unchanged.
- **Automatic (PS4)** selects the interface language from the console's system settings. You can choose a language manually and save your preference, or return to automatic mode.
- **11 languages:** English, Arabic, French, Spanish, German, Italian, Portuguese, Russian, Turkish, Japanese and Chinese. Arabic uses a right-to-left layout. Unsupported or unavailable console languages fall back to English.

### Downloads

- **XAVATAR.elf** — run the studio directly.
- **XAVATAR-installer.elf** — install `/data/payloads/XAVATAR.elf`, back up an existing version, and start the studio.
- **XAVATAR-1.0.3.zip** — both payloads, sender, logo, README, and notices.
- **XAVATAR-source.zip** — editable source and build helpers.
- **SHA256SUMS.txt** — download checksums.

Send one ELF to a compatible PS4 payload loader, then open `http://YOUR-PS4-IP:4002` from a device on the same network. No frontend-file transfer is required. Sign out and back in after installing an avatar to refresh it. Use **Stop service** to close port 4002.

Notifications include **Haider A.H (c) 1.0.3V** and request the bundled XAVATAR logo. System notifications remain in English.

### Validation

Native payload and installer builds pass. Local checks cover library upload/download/deletion, package validation, filename collisions, installed-profile preservation, PS4 language mapping and fallbacks, manual language preferences, and all 11 languages on desktop and mobile. The existing creation, installation, rollback, installer, and sender checks also pass. Physical PS4 verification is still required for firmware/loader behavior, actual system-language access, notification icon rendering, and visible avatar refresh.

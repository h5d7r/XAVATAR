#include "payload.h"
#include "xavatar.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The installer embeds the normal ELF and uses the same linked service code
   to start it immediately, avoiding a second ELF mapper or process loader. */
static int install_payload(char *error, size_t cap) {
  char base[1100], target[1200], temporary[1200], staged[1400];
  char backupbase[1100], backup[1200], backupfile[1400];
  struct stat st;
  if (xa_payload_size < 64 || memcmp(xa_payload, "\177ELF", 4)) {
    snprintf(error, cap, "XAVATAR installer contains an invalid payload.");
    return -1;
  }
  xa_path(base, sizeof(base), "/data/payloads");
  if (xa_mkdirs(base)) {
    snprintf(error, cap, "Cannot access /data/payloads: %s", strerror(errno));
    return -1;
  }
  snprintf(target, sizeof(target), "%s/XAVATAR.elf", base);
  if (!lstat(target, &st)) {
    Blob previous = {0};
    if (!S_ISREG(st.st_mode) || xa_read(target, &previous, 16 * 1024 * 1024)) {
      snprintf(error, cap,
               "Existing XAVATAR.elf is not a readable regular file.");
      return -1;
    }
    xa_path(backupbase, sizeof(backupbase), "/data/XAVATAR/backups");
    if (xa_mkdirs(backupbase)) {
      free(previous.data);
      snprintf(error, cap, "Cannot create a backup of the installed payload.");
      return -1;
    }
    snprintf(backup, sizeof(backup), "%s/payload-XXXXXX", backupbase);
    if (!mkdtemp(backup)) {
      free(previous.data);
      snprintf(error, cap, "Cannot allocate a payload backup directory.");
      return -1;
    }
    snprintf(backupfile, sizeof(backupfile), "%s/XAVATAR.elf", backup);
    int result = xa_write_new(backupfile, previous.data, previous.size);
    free(previous.data);
    if (result) {
      rmdir(backup);
      snprintf(error, cap,
               "Cannot save the previous payload; installation cancelled.");
      return -1;
    }
    xa_log(backupfile);
  } else if (errno != ENOENT) {
    snprintf(error, cap, "Cannot inspect the installed payload: %s",
             strerror(errno));
    return -1;
  }

  snprintf(temporary, sizeof(temporary), "%s/.xavatar-install-XXXXXX", base);
  if (!mkdtemp(temporary)) {
    snprintf(error, cap, "Cannot stage the payload: %s", strerror(errno));
    return -1;
  }
  snprintf(staged, sizeof(staged), "%s/XAVATAR.elf", temporary);
  int failed = xa_write_new(staged, xa_payload, xa_payload_size) ||
               chmod(staged, 0755) || rename(staged, target);
  int saved_errno = errno;
  unlink(staged);
  rmdir(temporary);
  if (failed) {
    snprintf(error, cap,
             "Payload installation failed: %s. Existing payload preserved.",
             strerror(saved_errno));
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (xa_configure(argc, argv))
    return 1;
  char path[1100], error[1700];
  xa_path(path, sizeof(path), "/data/XAVATAR");
  if (xa_mkdirs(path)) {
    xa_notify("XAVATAR installer could not access /data/XAVATAR");
    return 1;
  }
  xa_notify("XAVATAR installer activated");
  if (install_payload(error, sizeof(error))) {
    xa_notify(error);
    return 1;
  }
  xa_notify(
      "XAVATAR installed to /data/payloads/XAVATAR.elf. Starting port 4002.");
  return xa_run();
}

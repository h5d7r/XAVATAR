#include "assets.h"
#include "xavatar.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

char xa_root[512] = "";

void xa_path(char *out, size_t cap, const char *absolute) {
  snprintf(out, cap, "%s%s", xa_root, absolute);
}

int xa_mkdirs(const char *path) {
  char tmp[1024];
  if (strlen(path) >= sizeof(tmp))
    return -1;
  strcpy(tmp, path);
  for (char *p = tmp + 1;; p++) {
    if (*p == '/' || !*p) {
      char saved = *p;
      *p = 0;
      if (mkdir(tmp, 0755) && errno != EEXIST)
        return -1;
      struct stat st;
      if (lstat(tmp, &st) || !S_ISDIR(st.st_mode))
        return -1;
      *p = saved;
      if (!saved)
        break;
    }
  }
  return 0;
}

int xa_read(const char *path, Blob *out, size_t limit) {
  memset(out, 0, sizeof(*out));
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0)
    return -1;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
      (uint64_t)st.st_size > limit) {
    close(fd);
    return -1;
  }
  out->size = (size_t)st.st_size;
  out->data = malloc(out->size);
  if (!out->data) {
    close(fd);
    return -1;
  }
  size_t offset = 0;
  while (offset < out->size) {
    ssize_t n = read(fd, out->data + offset, out->size - offset);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      free(out->data);
      memset(out, 0, sizeof(*out));
      close(fd);
      return -1;
    }
    offset += (size_t)n;
  }
  close(fd);
  return 0;
}

/* O_EXCL prevents silent overwrites, including symlink targets. */
int xa_write_new(const char *path, const void *data, size_t size) {
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
  if (fd < 0)
    return -1;
  size_t offset = 0;
  while (offset < size) {
    ssize_t n = write(fd, (const char *)data + offset, size - offset);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      close(fd);
      unlink(path);
      return -1;
    }
    offset += (size_t)n;
  }
  int failed = fsync(fd);
  if (close(fd))
    failed = 1;
  if (failed) {
    unlink(path);
    return -1;
  }
  return 0;
}

#ifndef XAVATAR_HOST
int sceUserServiceInitialize(void *);
int sceUserServiceGetUserName(int32_t, char *, size_t);
int sceUserServiceTerminate(void);
int sceKernelSendNotificationRequest(int, void *, size_t, int);
int32_t sceSystemServiceParamGetInt(int32_t, int32_t *);
static int user_service_owned;
#endif

const char *xa_language(int *system_id) {
  int32_t value = -1;
#ifdef XAVATAR_HOST
  /* Deterministic console-language fixture; never use the host/browser locale. */
  const char *setting = getenv("XAVATAR_TEST_LANGUAGE");
  if (setting) {
    char *end;
    long parsed = strtol(setting, &end, 10);
    if (*setting && !*end && parsed >= 0 && parsed <= 29)
      value = (int32_t)parsed;
  }
#else
  /* OpenOrbis SystemService: parameter 1 is the PS4 system language. */
  if (sceSystemServiceParamGetInt(1, &value) < 0)
    value = -1;
#endif
  *system_id = value;
  switch (value) {
  case 0: return "ja";
  case 2: case 22: return "fr";
  case 3: case 20: return "es";
  case 4: return "de";
  case 5: return "it";
  case 7: case 17: return "pt";
  case 8: return "ru";
  case 10: case 11: return "zh";
  case 19: return "tr";
  case 21: return "ar";
  default: return "en";
  }
}

int xa_platform_init(void) {
#ifndef XAVATAR_HOST
  int result = sceUserServiceInitialize(NULL);
  user_service_owned = result == 0;
  /* 0x80960003: already initialized by the payload host. */
  if (result && (uint32_t)result != 0x80960003u)
    return -1;
#endif
  return 0;
}
void xa_platform_end(void) {
#ifndef XAVATAR_HOST
  if (user_service_owned)
    sceUserServiceTerminate();
#endif
}

int xa_users(User *users, char *error, size_t cap) {
  char path[1024];
  xa_path(path, sizeof(path), "/user/home");
  DIR *dir = opendir(path);
  if (!dir) {
    snprintf(error, cap, "Cannot read PS4 users: %s", strerror(errno));
    return -1;
  }
  struct dirent *de;
  int count = 0;
  while (count < XA_MAX_USERS && (de = readdir(dir))) {
    if (strlen(de->d_name) != 8 ||
        strspn(de->d_name, "0123456789abcdefABCDEF") != 8)
      continue;
    uint32_t uid = (uint32_t)strtoul(de->d_name, NULL, 16);
    char userpath[1200];
    struct stat st;
    snprintf(userpath, sizeof(userpath), "%s/%s", path, de->d_name);
    if (!uid || uid == UINT32_MAX || lstat(userpath, &st) ||
        !S_ISDIR(st.st_mode))
      continue;
    User *u = &users[count];
    memset(u, 0, sizeof(*u));
    u->id = uid;
#ifdef XAVATAR_HOST
    snprintf(u->name, sizeof(u->name), "Test user %08x", uid);
#else
    if (sceUserServiceGetUserName((int32_t)uid, u->name, sizeof(u->name)) < 0) {
      snprintf(u->name, sizeof(u->name), "User %08x", uid);
    }
#endif
    u->name[sizeof(u->name) - 1] = 0;
    count++;
  }
  closedir(dir);
  return count;
}

int xa_profile(uint32_t uid, char *path, size_t cap) {
  char base[1024];
  xa_path(base, sizeof(base), "/system_data/priv/cache/profile");
  DIR *d = opendir(base);
  struct dirent *de;
  if (d) {
    while ((de = readdir(d))) {
      const char *s = de->d_name;
      if (strlen(s) != 10 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X') ||
          strspn(s + 2, "0123456789abcdefABCDEF") != 8)
        continue;
      if ((uint32_t)strtoul(s + 2, NULL, 16) == uid) {
        snprintf(path, cap, "%s/%s", base, s);
        closedir(d);
        return 0;
      }
    }
    closedir(d);
  }
  snprintf(path, cap, "%s/0x%08x", base, uid);
  return 0;
}

void xa_log(const char *message) {
  char path[1024];
  xa_path(path, sizeof(path), "/data/XAVATAR/log.txt");
  FILE *f = fopen(path, "a");
  if (f) {
    fprintf(f, "%lld %s\n", (long long)time(NULL), message);
    fclose(f);
  }
#ifdef XAVATAR_HOST
  fprintf(stderr, "%s\n", message);
#endif
}

/* ShellUI can read custom notification images from /user/data. Exported PNGs
   never need a manual file transfer; the original logo is bundled in the ELF. */
static int png_bytes(const void *data, size_t size) {
  return data && size >= 8 && !memcmp(data, "\211PNG\r\n\032\n", 8);
}

/* Writes data to the absolute path atomically when it differs from what is
   already there. Returns 0 on success, -1 when the image cannot be exported. */
static int export_image(const char *absolute, const unsigned char *data,
                        size_t size) {
  char directory[1100], path[1200], temporary[1200], staged[1300];
  xa_path(directory, sizeof(directory), "/user/data/XAVATAR");
  xa_path(path, sizeof(path), absolute);
  if (xa_mkdirs(directory))
    return -1;
  Blob existing = {0};
  int matches = !xa_read(path, &existing, XA_MAX_PACKAGE) &&
                existing.size == size && !memcmp(existing.data, data, size);
  free(existing.data);
  if (matches)
    return 0;
  snprintf(temporary, sizeof(temporary), "%s/.notification-XXXXXX", directory);
  if (!mkdtemp(temporary))
    return -1;
  snprintf(staged, sizeof(staged), "%s/icon.png", temporary);
  int failed = xa_write_new(staged, data, size) || rename(staged, path);
  unlink(staged);
  rmdir(temporary);
  return failed ? -1 : 0;
}

static const char *notification_icon(void) {
  static char uri[1400];
  static int prepared;
  if (prepared)
    return uri;
  prepared = 1;
  snprintf(uri, sizeof(uri),
           "cxml://psnotification/tex_default_icon_notification");
  const XaAsset *logo = xa_asset_get("logo.png");
  if (!png_bytes(logo ? logo->data : NULL, logo ? logo->size : 0))
    return uri;
  if (export_image("/user/data/XAVATAR/notification.png", logo->data,
                   logo->size)) {
    xa_log("Could not export notification logo; using the system icon.");
    return uri;
  }
  /* Direct absolute image paths are supported by the notification API. */
  xa_path(uri, sizeof(uri), "/user/data/XAVATAR/notification.png");
  return uri;
}

/* Exports the avatar the user just applied and uses it as the notification
   icon, falling back to the bundled logo when the image is unusable. The
   filename carries a content hash because ShellUI caches notification images
   by path, which would otherwise keep showing the previous avatar. */
static const char *notification_avatar(const void *data, size_t size) {
  static char uri[1400];
  static char previous[1400];
  if (!png_bytes(data, size))
    return notification_icon();
  uint32_t hash = 2166136261u;
  for (const unsigned char *p = data, *end = p + size; p < end; p++) {
    hash ^= *p;
    hash *= 16777619u;
  }
  char absolute[160], rooted[1400];
  snprintf(absolute, sizeof(absolute),
           "/user/data/XAVATAR/avatar-notification-%08x.png", hash);
  xa_path(rooted, sizeof(rooted), absolute);
  if (previous[0] && strcmp(previous, rooted))
    unlink(previous);
  if (export_image(absolute, data, size)) {
    xa_log("Could not export avatar notification image; using the logo.");
    return notification_icon();
  }
  snprintf(previous, sizeof(previous), "%s", rooted);
  snprintf(uri, sizeof(uri), "%s", rooted);
  return uri;
}

static void notify_with_icon(const char *message, const char *icon) {
  char text[0x400];
  const char *credit = XA_CREDIT;
  size_t available = sizeof(text) - strlen(credit) - 2;
  snprintf(text, sizeof(text), "%.*s\n%s", (int)available, message, credit);
#ifndef XAVATAR_HOST
  /* SDK reference ABI: message at 0x2d, total request length 0xc30. */
  unsigned char request[0xc30] = {0};
  uint32_t invalid = UINT32_MAX;
  memcpy(request + 12, &invalid, 4);
  for (int i = 16; i <= 28; i += 4)
    memcpy(request + i, &invalid, 4);
  request[44] = 1;
  snprintf((char *)request + 45, 0x400, "%s", text);
  snprintf((char *)request + 45 + 0x400, 0x800, "%s", icon);
  if (sceKernelSendNotificationRequest(0, request, sizeof(request), 0) < 0) {
    snprintf((char *)request + 45 + 0x400, 0x800,
             "cxml://psnotification/tex_default_icon_notification");
    sceKernelSendNotificationRequest(0, request, sizeof(request), 0);
  }
#else
  (void)icon;
#endif
  xa_log(text);
}

void xa_notify(const char *message) {
  notify_with_icon(message, notification_icon());
}

void xa_notify_avatar(const char *message, const void *png, size_t len) {
  notify_with_icon(message, notification_avatar(png, len));
}

int xa_filename(const char *name) {
  size_t n = strlen(name);
  if (n < 9 || n > 100 || strcmp(name + n - 8, ".xavatar"))
    return 0;
  if (name[0] == '.' || name[0] == ' ')
    return 0;
  for (size_t i = 0; i < n - 8; i++) {
    unsigned char c = (unsigned char)name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' '))
      return 0;
  }
  return 1;
}

#include "assets.h"
#include "xavatar.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef XAVATAR_HOST
/* Same SceNet interface as the supplied port-8088 reference. */
int sceNetSocket(const char *, int, int, int);
int sceNetSocketClose(int);
int sceNetSetsockopt(int, int, int, const void *, socklen_t);
int sceNetBind(int, const struct sockaddr *, socklen_t);
int sceNetListen(int, int);
int sceNetAccept(int, struct sockaddr *, socklen_t *);
ssize_t sceNetSend(int, const void *, size_t, int);
ssize_t sceNetRecv(int, void *, size_t, int);
#define NET_SOCKET() sceNetSocket("XAVATAR", AF_INET, SOCK_STREAM, 0)
#define net_close sceNetSocketClose
#define net_setsockopt sceNetSetsockopt
#define net_bind sceNetBind
#define net_listen sceNetListen
#define net_accept sceNetAccept
#define net_send sceNetSend
#define net_recv sceNetRecv
#else
#define NET_SOCKET() socket(AF_INET, SOCK_STREAM, 0)
#define net_close close
#define net_setsockopt setsockopt
#define net_bind bind
#define net_listen listen
#define net_accept accept
#define net_send send
#define net_recv recv
#endif

typedef struct {
  char method[8], target[1024], host[256], origin[280], content_type[96];
  unsigned char *body;
  size_t length;
  int action_header;
} Request;

static int service_running = 1;

static int send_all(int fd, const void *data, size_t size) {
  while (size) {
#ifdef XAVATAR_HOST
    int flags = MSG_NOSIGNAL;
#else
    int flags = 0;
#endif
    ssize_t n = net_send(fd, data, size, flags);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return -1;
    data = (const unsigned char *)data + n;
    size -= (size_t)n;
  }
  return 0;
}

static void response_headers(int fd, int code, const char *type, const void *body,
                             size_t len, const char *extra) {
  char header[1200];
  const char *reason = code == 200   ? "OK"
                       : code == 201 ? "Created"
                       : code == 400 ? "Bad Request"
                       : code == 403 ? "Forbidden"
                       : code == 404 ? "Not Found"
                       : code == 409 ? "Conflict"
                       : code == 413 ? "Payload Too Large"
                                     : "Error";
  int n = snprintf(
      header, sizeof(header),
      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: "
      "%zu\r\nConnection: close\r\n"
      "%sCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
      "Content-Security-Policy: default-src 'self'; img-src 'self' blob: "
      "data:; style-src 'self'; script-src 'self'; connect-src 'self'; "
      "object-src 'none'; base-uri 'none'; frame-ancestors 'none'\r\n\r\n",
      code, reason, type, len, extra);
  if (n > 0 && (size_t)n < sizeof(header) && !send_all(fd, header, (size_t)n))
    send_all(fd, body, len);
}

static void response(int fd, int code, const char *type, const void *body,
                     size_t len) {
  response_headers(fd, code, type, body, len, "");
}

static void escape_json(const char *in, char *out, size_t cap) {
  size_t n = 0;
  for (; *in && n + 7 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') {
      out[n++] = '\\';
      out[n++] = (char)c;
    } else if (c < 32) {
      snprintf(out + n, cap - n, "\\u%04x", c);
      n += 6;
    } else
      out[n++] = (char)c;
  }
  out[n] = 0;
}
static void message(int fd, int code, const char *text) {
  char escaped[10000], body[11000];
  escape_json(text, escaped, sizeof(escaped));
  snprintf(body, sizeof(body), "{\"ok\":%s,\"message\":\"%s\"}",
           code < 300 ? "true" : "false", escaped);
  response(fd, code, "application/json; charset=utf-8", body, strlen(body));
}

static int receive_request(int fd, Request *r) {
  char header[8193];
  size_t used = 0, headlen = 0;
  memset(r, 0, sizeof(*r));
  /* Read headers without consuming the binary body. */
  while (used < sizeof(header) - 1) {
    ssize_t n = net_recv(fd, header + used, 1, 0);
    if (n <= 0)
      return 400;
    if (!header[used])
      return 400;
    used++;
    header[used] = 0;
    if (used >= 4 && !memcmp(header + used - 4, "\r\n\r\n", 4)) {
      headlen = used;
      break;
    }
  }
  if (!headlen)
    return 413;
  char *end = strstr(header, "\r\n");
  if (!end)
    return 400;
  *end = 0;
  char version[16], extra;
  if (sscanf(header, "%7s %1023s %15s %c", r->method, r->target, version,
             &extra) != 3 ||
      (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0")) ||
      r->target[0] != '/')
    return 400;
  int length_seen = 0, host_seen = 0, type_seen = 0, origin_seen = 0;
  char *line = end + 2;
  while (*line && strncmp(line, "\r\n", 2)) {
    end = strstr(line, "\r\n");
    if (!end)
      return 400;
    *end = 0;
    char *colon = strchr(line, ':');
    if (!colon || line[0] == ' ' || line[0] == '\t')
      return 400;
    *colon = 0;
    char *value = colon + 1;
    while (*value == ' ' || *value == '\t')
      value++;
    char *tail = value + strlen(value);
    while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t'))
      *--tail = 0;
    if (!strcasecmp(line, "Content-Length")) {
      if (length_seen++ || !*value)
        return 400;
      size_t len = 0;
      for (const char *p = value; *p; p++) {
        if (!isdigit((unsigned char)*p))
          return 400;
        len = len * 10 + (unsigned)(*p - '0');
        size_t limit = !strncmp(r->target, "/api/upload", 11) &&
                               (r->target[11] == '?' || !r->target[11])
                           ? XA_MAX_PACKAGE : XA_MAX_PNG;
        if (len > limit)
          return 413;
      }
      r->length = len;
    } else if (!strcasecmp(line, "Transfer-Encoding") ||
               !strcasecmp(line, "Expect"))
      return 400;
    else if (!strcasecmp(line, "Host")) {
      if (host_seen++ || strlen(value) >= sizeof(r->host))
        return 400;
      strcpy(r->host, value);
    } else if (!strcasecmp(line, "Origin")) {
      if (origin_seen++ || strlen(value) >= sizeof(r->origin))
        return 400;
      strcpy(r->origin, value);
    } else if (!strcasecmp(line, "Content-Type")) {
      if (type_seen++ || strlen(value) >= sizeof(r->content_type))
        return 400;
      strcpy(r->content_type, value);
    } else if (!strcasecmp(line, "X-XAVATAR")) {
      if (r->action_header || strcmp(value, "1"))
        return 400;
      r->action_header = 1;
    }
    line = end + 2;
  }
  if (!r->host[0])
    return 400;
  if (strcmp(r->method, "GET") && strcmp(r->method, "POST"))
    return 400;
  if (!strcmp(r->method, "GET") && r->length)
    return 400;
  int denied = 0;
  if (!strcmp(r->method, "POST")) {
    char expected[300];
    snprintf(expected, sizeof(expected), "http://%s", r->host);
    if (!r->action_header || (r->origin[0] && strcmp(r->origin, expected)))
      denied = 403;
  }
  if (r->length) {
    r->body = malloc(r->length);
    if (!r->body)
      return 500;
    size_t offset = 0;
    while (offset < r->length) {
      ssize_t n = net_recv(fd, r->body + offset, r->length - offset, 0);
      if (n <= 0)
        return 400;
      offset += (size_t)n;
    }
  }
  /* Consume the bounded body before replying. Closing with unread upload bytes
     can reset TCP and discard the useful error response on the client. */
  return denied;
}

static int unhex(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
static int query(const char *target, const char *key, char *out, size_t cap) {
  const char *p = strchr(target, '?');
  out[0] = 0;
  int found = 0;
  if (!p)
    return 0;
  p++;
  while (*p) {
    const char *end = strchr(p, '&');
    if (!end)
      end = p + strlen(p);
    const char *eq = memchr(p, '=', (size_t)(end - p));
    if (eq && (size_t)(eq - p) == strlen(key) &&
        !memcmp(p, key, (size_t)(eq - p))) {
      if (found++)
        return -1;
      size_t n = 0;
      for (const char *v = eq + 1; v < end; v++) {
        int c = (unsigned char)*v;
        if (c == '%') {
          if (end - v < 3 || unhex(v[1]) < 0 || unhex(v[2]) < 0)
            return -1;
          c = unhex(v[1]) * 16 + unhex(v[2]);
          v += 2;
        } else if (c == '+')
          c = ' ';
        if (!c || c < 32 || n + 1 >= cap)
          return -1;
        out[n++] = (char)c;
      }
      out[n] = 0;
    }
    p = *end ? end + 1 : end;
  }
  return found;
}

static void serve_users(int fd) {
  User users[XA_MAX_USERS];
  char error[512], body[10000];
  int n = xa_users(users, error, sizeof(error));
  if (n < 0) {
    message(fd, 500, error);
    return;
  }
  size_t pos = (size_t)snprintf(body, sizeof(body), "{\"users\":[");
  for (int i = 0; i < n; i++) {
    char escaped[512];
    escape_json(users[i].name, escaped, sizeof(escaped));
    pos += (size_t)snprintf(body + pos, sizeof(body) - pos,
                            "%s{\"id\":\"%08x\",\"name\":\"%s\"}", i ? "," : "",
                            users[i].id, escaped);
  }
  snprintf(body + pos, sizeof(body) - pos, "]}");
  response(fd, 200, "application/json; charset=utf-8", body, strlen(body));
}

static void serve_library(int fd) {
  char base[1024];
  xa_path(base, sizeof(base), "/data/AVATARS");
  DIR *d = opendir(base);
  if (!d) {
    message(fd, 500, "Cannot scan /data/AVATARS.");
    return;
  }
  char *body = malloc(256 * 1024);
  if (!body) {
    closedir(d);
    message(fd, 500, "Out of memory.");
    return;
  }
  size_t pos = (size_t)sprintf(body, "{\"avatars\":[");
  int count = 0, truncated = 0;
  struct dirent *de;
  while ((de = readdir(d))) {
    if (!xa_filename(de->d_name))
      continue;
    char file[1400], escaped[700];
    struct stat st;
    snprintf(file, sizeof(file), "%s/%s", base, de->d_name);
    if (lstat(file, &st) || !S_ISREG(st.st_mode))
      continue;
    if (count >= 1000) {
      truncated = 1;
      break;
    }
    escape_json(de->d_name, escaped, sizeof(escaped));
    pos += (size_t)snprintf(body + pos, 256 * 1024 - pos,
                            "%s{\"name\":\"%s\",\"size\":%lld}",
                            count ? "," : "", escaped, (long long)st.st_size);
    count++;
  }
  closedir(d);
  snprintf(body + pos, 256 * 1024 - pos, "],\"truncated\":%s}",
           truncated ? "true" : "false");
  response(fd, 200, "application/json; charset=utf-8", body, strlen(body));
  free(body);
}

static int library_path(const char *target, char *path, size_t cap) {
  char name[128], base[1024];
  if (query(target, "name", name, sizeof(name)) != 1 || !xa_filename(name)) {
    return -1;
  }
  xa_path(base, sizeof(base), "/data/AVATARS");
  snprintf(path, cap, "%s/%s", base, name);
  return 0;
}

static int library_avatar(const char *target, Avatar *a, char *error,
                          size_t cap) {
  char path[1200];
  Blob zip = {0};
  if (library_path(target, path, sizeof(path))) {
    snprintf(error, cap, "Invalid avatar filename.");
    return 400;
  }
  if (xa_read(path, &zip, XA_MAX_PACKAGE)) {
    snprintf(error, cap, "Avatar file is missing, unreadable, or too large.");
    return 404;
  }
  int result = avatar_open(zip.data, zip.size, a, error, cap);
  free(zip.data);
  return result ? 400 : 0;
}

static int user_id(const char *target, uint32_t *uid) {
  char value[16];
  if (query(target, "user", value, sizeof(value)) != 1 || strlen(value) != 8 ||
      strspn(value, "0123456789abcdefABCDEF") != 8)
    return -1;
  *uid = (uint32_t)strtoul(value, NULL, 16);
  return 0;
}

static int save_package(const char *name, Blob *zip, char *path,
                        size_t pathcap) {
  char base[1024], temp[1200], file[1400];
  xa_path(base, sizeof(base), "/data/AVATARS");
  snprintf(path, pathcap, "%s/%s", base, name);
  snprintf(temp, sizeof(temp), "%s/.pending-XXXXXX", base);
  if (!mkdtemp(temp))
    return -1;
  snprintf(file, sizeof(file), "%s/avatar", temp);
  int result = xa_write_new(file, zip->data, zip->size);
  if (!result)
    result = link(
        file, path); /* atomic publish; existing names are never overwritten */
  int saved_errno = errno;
  unlink(file);
  rmdir(temp);
  errno = saved_errno;
  return result;
}

static void upload_package(int fd, Request *r) {
  char name[128], saved[1200], error[2000];
  if (query(r->target, "name", name, sizeof(name)) != 1 || !xa_filename(name)) {
    message(fd, 400, "Invalid avatar filename.");
    return;
  }
  if (strcmp(r->content_type, "application/octet-stream")) {
    message(fd, 400, "Expected application/octet-stream.");
    return;
  }
  Avatar avatar = {0};
  if (avatar_open(r->body, r->length, &avatar, error, sizeof(error))) {
    message(fd, 400, error);
    return;
  }
  avatar_free(&avatar);
  Blob zip = {r->body, r->length};
  if (save_package(name, &zip, saved, sizeof(saved))) {
    message(fd, errno == EEXIST ? 409 : 500,
            errno == EEXIST ? "That filename already exists. Choose another name."
                           : "Could not save the avatar. Check free space and permissions.");
    return;
  }
  message(fd, 201, "Avatar added to your library.");
}

static void download_package(int fd, Request *r) {
  char path[1200], name[128], header[256];
  Blob zip = {0};
  if (library_path(r->target, path, sizeof(path))) {
    message(fd, 400, "Invalid avatar filename.");
    return;
  }
  if (xa_read(path, &zip, XA_MAX_PACKAGE)) {
    message(fd, 404, "Avatar file is missing, unreadable, or too large.");
    return;
  }
  query(r->target, "name", name, sizeof(name));
  snprintf(header, sizeof(header), "Content-Disposition: attachment; filename=\"%s\"\r\n", name);
  response_headers(fd, 200, "application/octet-stream", zip.data, zip.size, header);
  free(zip.data);
}

static void delete_package(int fd, Request *r) {
  char path[1200];
  struct stat st;
  if (library_path(r->target, path, sizeof(path))) {
    message(fd, 400, "Invalid avatar filename.");
    return;
  }
  if (lstat(path, &st)) {
    message(fd, errno == ENOENT ? 404 : 500, "Could not find the avatar file.");
    return;
  }
  if (!S_ISREG(st.st_mode)) {
    message(fd, 400, "Only regular avatar files can be deleted.");
    return;
  }
  if (unlink(path)) {
    message(fd, 500, "Could not delete the avatar. Check permissions.");
    return;
  }
  message(fd, 200, "Avatar deleted from your library.");
}

static void create_avatar(int fd, Request *r) {
  char mode[16], name[128], error[2000], saved[1200] = "";
  uint32_t uid = 0;
  if (query(r->target, "mode", mode, sizeof(mode)) != 1 ||
      (strcmp(mode, "install") && strcmp(mode, "save") &&
       strcmp(mode, "both"))) {
    message(fd, 400, "Choose install, save, or both.");
    return;
  }
  int install = strcmp(mode, "save") != 0, save = strcmp(mode, "install") != 0;
  if (install && user_id(r->target, &uid)) {
    message(fd, 400, "Select a PS4 user.");
    return;
  }
  if (save && (query(r->target, "name", name, sizeof(name)) != 1 ||
               !xa_filename(name))) {
    message(fd, 400,
            "Use a filename with letters, numbers, spaces, hyphens or "
            "underscores, ending in .xavatar.");
    return;
  }
  if (strcmp(r->content_type, "image/png")) {
    message(fd, 400, "Expected image/png.");
    return;
  }
  Blob zip = {0};
  Avatar avatar = {0};
  if (avatar_create(r->body, r->length, &zip, error, sizeof(error)) ||
      avatar_open(zip.data, zip.size, &avatar, error, sizeof(error))) {
    free(zip.data);
    message(fd, 400, error);
    return;
  }
  if (save && save_package(name, &zip, saved, sizeof(saved))) {
    int conflict = errno == EEXIST;
    message(
        fd, conflict ? 409 : 500,
        conflict
            ? "That filename already exists. Choose another name."
            : "Could not save the avatar. Check free space and permissions.");
    goto done;
  }
  if (install && avatar_install(&avatar, uid, error, sizeof(error))) {
    if (save && unlink(saved)) {
      size_t len = strlen(error);
      snprintf(error + len, sizeof(error) - len,
               " The saved package remains in /data/AVATARS.");
    }
    message(fd, 500, error);
    goto done;
  }
  const char *success =
      install ? (save ? "Avatar installed and saved. Sign out and back in to "
                        "refresh it."
                      : "Avatar installed without saving a package. Sign out "
                        "and back in to refresh it.")
              : "XAVATAR file saved to /data/AVATARS.";
  xa_notify(success);
  message(fd, 201, success);
done:
  avatar_free(&avatar);
  free(zip.data);
}

static void handle(int fd, Request *r) {
  char path[1024];
  snprintf(path, sizeof(path), "%s", r->target);
  char *q = strchr(path, '?');
  if (q)
    *q = 0;
  if (!strcmp(r->method, "GET")) {
    if (!strcmp(path, "/api/status")) {
      int language_id;
      const char *language = xa_language(&language_id);
#ifdef XAVATAR_HOST
      const char *host_test = "true";
#else
      const char *host_test = "false";
#endif
      char body[256];
      snprintf(body, sizeof(body), "{\"name\":\"XAVATAR\",\"port\":4002,"
               "\"version\":\"" XA_VERSION "\",\"hostTest\":%s,"
               "\"language\":\"%s\",\"systemLanguage\":%d}",
               host_test, language, language_id);
      response(fd, 200, "application/json", body, strlen(body));
      return;
    }
    if (!strcmp(path, "/api/users")) {
      serve_users(fd);
      return;
    }
    if (!strcmp(path, "/api/avatars")) {
      serve_library(fd);
      return;
    }
    if (!strcmp(path, "/api/download")) {
      download_package(fd, r);
      return;
    }
    if (!strcmp(path, "/api/preview")) {
      Avatar a = {0};
      char error[2000];
      int result = library_avatar(r->target, &a, error, sizeof(error));
      if (result) {
        message(fd, result, error);
        return;
      }
      for (int i = 0; i < a.count; i++)
        if (!strcmp(a.entries[i].name, "avatar.png")) {
          response(fd, 200, "image/png", a.entries[i].blob.data,
                   a.entries[i].blob.size);
          break;
        }
      avatar_free(&a);
      return;
    }
    const char *asset = NULL, *type = NULL;
    if (!strcmp(path, "/") || !strcmp(path, "/ui.html")) {
      asset = "ui.html";
      type = "text/html; charset=utf-8";
    }
    if (!strcmp(path, "/styles.css")) {
      asset = "styles.css";
      type = "text/css; charset=utf-8";
    }
    if (!strcmp(path, "/app.js")) {
      asset = "app.js";
      type = "application/javascript; charset=utf-8";
    }
    if (!strcmp(path, "/i18n.js")) {
      asset = "i18n.js";
      type = "application/javascript; charset=utf-8";
    }
    if (!strcmp(path, "/logo.png")) {
      asset = "logo.png";
      type = "image/png";
    }
    if (asset) {
      const XaAsset *resource = xa_asset_get(asset);
      if (resource)
        response(fd, 200, type, resource->data, resource->size);
      else
        message(fd, 500, "Frontend resource missing from this build.");
      return;
    }
  } else if (!strcmp(r->method, "POST")) {
    if (!strcmp(path, "/api/upload")) {
      upload_package(fd, r);
      return;
    }
    if (!strcmp(path, "/api/delete")) {
      delete_package(fd, r);
      return;
    }
    if (!strcmp(path, "/api/stop")) {
      service_running = 0;
      message(fd, 200, "XAVATAR is shutting down.");
      return;
    }
    if (!strcmp(path, "/api/create")) {
      create_avatar(fd, r);
      return;
    }
    if (!strcmp(path, "/api/install")) {
      uint32_t uid;
      char error[2000];
      Avatar a = {0};
      if (user_id(r->target, &uid)) {
        message(fd, 400, "Select a PS4 user.");
        return;
      }
      int result = library_avatar(r->target, &a, error, sizeof(error));
      if (result) {
        message(fd, result, error);
        return;
      }
      if (avatar_install(&a, uid, error, sizeof(error)))
        message(fd, 500, error);
      else {
        xa_notify("XAVATAR avatar installed");
        message(fd, 200, error);
      }
      avatar_free(&a);
      return;
    }
  }
  message(fd, 404, "Endpoint not found.");
}

int xa_server(void) {
  service_running = 1;
  int server = NET_SOCKET();
  if (server < 0)
    return -1;
  int reuse = 1;
  net_setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  struct sockaddr_in address = {0};
#ifndef XAVATAR_HOST
  address.sin_len = sizeof(address);
#endif
  address.sin_family = AF_INET;
  address.sin_port = htons(XA_PORT);
  address.sin_addr.s_addr = INADDR_ANY;
  if (net_bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 ||
      net_listen(server, 8) < 0) {
    net_close(server);
    return -1;
  }
  xa_notify("XAVATAR is available on port 4002");
  while (service_running) {
    int client = net_accept(server, NULL, NULL);
    if (client < 0) {
      if (errno == EINTR)
        continue;
      net_close(server);
      return -1;
    }
    struct timeval timeout = {15, 0};
    net_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    net_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifndef XAVATAR_HOST
    int one = 1;
    net_setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    Request r;
    int result = receive_request(client, &r);
    if (result)
      message(client, result, "Invalid, incomplete, or disallowed request.");
    else
      handle(client, &r);
    free(r.body);
    net_close(client);
  }
  net_close(server);
  xa_notify("XAVATAR stopped. Port 4002 is closed.");
  return 0;
}

int xa_configure(int argc, char **argv) {
#ifdef XAVATAR_HOST
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--root") && i + 1 < argc)
      snprintf(xa_root, sizeof(xa_root), "%s", argv[++i]);
    else {
      fprintf(stderr, "Usage: %s --root TEST_DIRECTORY\n", argv[0]);
      return 1;
    }
  }
  if (!xa_root[0]) {
    fprintf(stderr, "Host testing requires --root.\n");
    return 1;
  }
#else
  (void)argc;
  (void)argv;
#endif
  return 0;
}

int xa_run(void) {
  char path[1024];
  xa_path(path, sizeof(path), "/data/XAVATAR");
  if (xa_mkdirs(path))
    return 1;
  xa_notify("XAVATAR payload activated");
  xa_path(path, sizeof(path), "/data/AVATARS");
  if (xa_mkdirs(path) || xa_platform_init()) {
    xa_notify("XAVATAR initialization failed");
    return 1;
  }
  xa_notify("XAVATAR opening port 4002");
  int result = xa_server();
  if (result)
    xa_notify("XAVATAR service failed. Check port 4002 and filesystem access.");
  xa_platform_end();
  return result ? 1 : 0;
}

#ifndef XAVATAR_INSTALLER
int main(int argc, char **argv) {
  if (xa_configure(argc, argv))
    return 1;
  return xa_run();
}
#endif

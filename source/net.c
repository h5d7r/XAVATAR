/* Outbound image fetching for the "image link" feature. The console performs
   the request so that the browser never hits cross-origin restrictions; the
   host target keeps a small plain-HTTP client for local testing. */
#include "xavatar.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XAVATAR_HOST
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int send_fd(int fd, const char *data, size_t size) {
  while (size) {
    ssize_t n = send(fd, data, size, 0);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return -1;
    data += n;
    size -= (size_t)n;
  }
  return 0;
}

/* Host testing supports http:// links only; the console handles https://. */
static int host_fetch(const char *url, Blob *out, char *error, size_t cap) {
  if (strncmp(url, "http://", 7)) {
    snprintf(error, cap, "Host testing fetches http:// links only.");
    return -1;
  }
  const char *authority = url + 7;
  const char *slash = strchr(authority, '/');
  size_t hostlen = slash ? (size_t)(slash - authority) : strlen(authority);
  char hostport[256];
  if (!hostlen || hostlen >= sizeof(hostport)) {
    snprintf(error, cap, "The image link is invalid.");
    return -1;
  }
  memcpy(hostport, authority, hostlen);
  hostport[hostlen] = 0;
  const char *path = slash ? slash : "/";
  char host[256];
  int port = 80;
  char *colon = strrchr(hostport, ':');
  if (colon) {
    *colon = 0;
    port = atoi(colon + 1);
  }
  snprintf(host, sizeof(host), "%s", hostport);
  if (!host[0] || port <= 0 || port > 65535) {
    snprintf(error, cap, "The image link is invalid.");
    return -1;
  }

  char service[8];
  snprintf(service, sizeof(service), "%d", port);
  struct addrinfo hints, *list = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, service, &hints, &list)) {
    snprintf(error, cap, "The image host could not be resolved.");
    return -1;
  }
  int fd = -1;
  for (struct addrinfo *it = list; it; it = it->ai_next) {
    fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0)
      continue;
    struct timeval timeout = {20, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (!connect(fd, it->ai_addr, it->ai_addrlen))
      break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(list);
  if (fd < 0) {
    snprintf(error, cap, "The image host could not be reached.");
    return -1;
  }

  char request[1200];
  int length = snprintf(request, sizeof(request),
                        "GET %s HTTP/1.0\r\nHost: %s\r\n"
                        "User-Agent: XAVATAR/1.1\r\nAccept: image/*\r\n"
                        "Connection: close\r\n\r\n",
                        path, host);
  if (length <= 0 || (size_t)length >= sizeof(request) ||
      send_fd(fd, request, (size_t)length)) {
    close(fd);
    snprintf(error, cap, "The request could not be sent.");
    return -1;
  }

  size_t limit = XA_MAX_FETCH, got = 0;
  unsigned char *buffer = malloc(limit + 1);
  if (!buffer) {
    close(fd);
    snprintf(error, cap, "Out of memory.");
    return -1;
  }
  while (got < limit) {
    ssize_t n = recv(fd, buffer + got, limit - got, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (n == 0)
      break;
    got += (size_t)n;
  }
  close(fd);
  if (got >= limit) {
    free(buffer);
    snprintf(error, cap, "The linked image is larger than 8 MB.");
    return -1;
  }
  buffer[got] = 0;

  size_t body = 0;
  for (size_t i = 0; i + 3 < got; i++)
    if (!memcmp(buffer + i, "\r\n\r\n", 4)) {
      body = i + 4;
      break;
    }
  if (!body || got < 12 || memcmp(buffer, "HTTP/", 5) || buffer[9] != '2') {
    free(buffer);
    snprintf(error, cap, "The link did not return a usable image.");
    return -1;
  }
  size_t size = got - body;
  memmove(buffer, buffer + body, size);
  buffer[size] = 0;
  out->data = buffer;
  out->size = size;
  return 0;
}
#else
/* OpenOrbis libSceNet / libSceSsl / libSceHttp (stubbed by the payload SDK). */
int sceNetInit(void);
int sceNetPoolCreate(const char *, int, int);
int sceSslInit(size_t);
int sceHttpInit(int, int, size_t);
int sceHttpTerm(int);
int sceHttpCreateTemplate(int, const char *, int, int);
int sceHttpsSetSslCallback(int, int (*)(void), void *);
int sceHttpSetAutoRedirect(int, int);
int sceHttpSetConnectTimeOut(int, uint32_t);
int sceHttpSetRecvTimeOut(int, uint32_t);
int sceHttpDeleteTemplate(int);
int sceHttpCreateConnectionWithURL(int, const char *, int);
int sceHttpDeleteConnection(int);
int sceHttpCreateRequestWithURL(int, int, const char *, uint64_t);
int sceHttpSendRequest(int, const void *, size_t);
int sceHttpGetStatusCode(int, int *);
int sceHttpReadData(int, void *, size_t);
int sceHttpDeleteRequest(int);

#define XA_HTTP_USER_AGENT "XAVATAR/1.1"

static int http_pool = -1, http_ssl = -1, http_ctx = -1;

static int ssl_accept(void) { return 0; }

static int http_ready(void) {
  if (http_ctx >= 0)
    return 0;
  /* The payload environment may already own libnet; treat init as best-effort
     and rely on pool creation to report a real failure. */
  if (sceNetInit())
    xa_log("XAVATAR: sceNetInit reported an error; continuing.");
  if ((http_pool = sceNetPoolCreate("XAVATAR-http", 1024 * 1024, 0)) < 0)
    return -1;
  if ((http_ssl = sceSslInit(128 * 1024)) < 0)
    return -1;
  if ((http_ctx = sceHttpInit(http_pool, http_ssl, 1024 * 1024)) < 0)
    return -1;
  return 0;
}

static int remote_fetch(const char *url, Blob *out, char *error, size_t cap) {
  int tpl = -1, conn = -1, req = -1, status = 0, result = -1;
  unsigned char *buffer = NULL;
  size_t limit = XA_MAX_FETCH, got = 0;

  if (http_ready()) {
    snprintf(error, cap, "The console network stack is unavailable.");
    return -1;
  }
  tpl = sceHttpCreateTemplate(http_ctx, XA_HTTP_USER_AGENT, 2, 1);
  if (tpl < 0) {
    snprintf(error, cap, "The console could not prepare the request.");
    return -1;
  }
  sceHttpsSetSslCallback(tpl, ssl_accept, NULL);
  /* Follow redirects (many share links jump to the real image) and cap waiting
     so a stalled host cannot hold up the single-threaded server. */
  sceHttpSetAutoRedirect(tpl, 1);
  sceHttpSetConnectTimeOut(tpl, 15 * 1000 * 1000);
  sceHttpSetRecvTimeOut(tpl, 30 * 1000 * 1000);
  conn = sceHttpCreateConnectionWithURL(tpl, url, 0);
  if (conn < 0) {
    snprintf(error, cap, "The console could not reach the link.");
    goto done;
  }
  req = sceHttpCreateRequestWithURL(conn, 0, url, 0);
  if (req < 0) {
    snprintf(error, cap, "The console could not reach the link.");
    goto done;
  }
  if (sceHttpSendRequest(req, NULL, 0)) {
    snprintf(error, cap, "The console could not reach the link.");
    goto done;
  }
  if (sceHttpGetStatusCode(req, &status) || status != 200) {
    snprintf(error, cap, "The link returned HTTP %d.", status);
    goto done;
  }
  buffer = malloc(limit + 1);
  if (!buffer) {
    snprintf(error, cap, "Out of memory.");
    goto done;
  }
  while (got < limit) {
    int n = sceHttpReadData(req, buffer + got, limit - got);
    if (n < 0) {
      snprintf(error, cap, "The download stopped unexpectedly.");
      goto done;
    }
    if (n == 0)
      break;
    got += (size_t)n;
  }
  if (got == 0) {
    snprintf(error, cap, "The link returned an empty image.");
    goto done;
  }
  if (got >= limit) {
    snprintf(error, cap, "The linked image is larger than 8 MB.");
    goto done;
  }
  buffer[got] = 0;
  out->data = buffer;
  out->size = got;
  buffer = NULL;
  result = 0;
done:
  if (req >= 0)
    sceHttpDeleteRequest(req);
  if (conn >= 0)
    sceHttpDeleteConnection(conn);
  if (tpl >= 0)
    sceHttpDeleteTemplate(tpl);
  free(buffer);
  return result;
}
#endif

int xa_fetch(const char *url, Blob *out, char *error, size_t cap) {
  memset(out, 0, sizeof(*out));
  if (!url || !url[0]) {
    snprintf(error, cap, "Provide an image link.");
    return -1;
  }
  if (strlen(url) > 1900) {
    snprintf(error, cap, "The image link is too long.");
    return -1;
  }
  if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) {
    snprintf(error, cap, "Only http:// and https:// links are supported.");
    return -1;
  }
#ifdef XAVATAR_HOST
  return host_fetch(url, out, error, cap);
#else
  return remote_fetch(url, out, error, cap);
#endif
}

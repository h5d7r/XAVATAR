#ifndef XAVATAR_H
#define XAVATAR_H
#include <stddef.h>
#include <stdint.h>

#define XA_PORT 4002
#define XA_VERSION "1.1.0"
#define XA_CREDIT "Haider A.H (c) " XA_VERSION "V"
#define XA_MAX_PACKAGE (4 * 1024 * 1024)
#define XA_MAX_PNG (2 * 1024 * 1024)
#define XA_MAX_FETCH (8 * 1024 * 1024)
#define XA_MAX_USERS 16
#define XA_MAX_ENTRIES 11

typedef struct {
  unsigned char *data;
  size_t size;
} Blob;
typedef struct {
  uint32_t id;
  char name[65];
} User;
typedef struct {
  char name[32];
  Blob blob;
} AvatarEntry;
typedef struct {
  AvatarEntry entries[XA_MAX_ENTRIES];
  int count;
} Avatar;

extern char xa_root[512];
void xa_path(char *out, size_t cap, const char *absolute);
int xa_mkdirs(const char *path);
int xa_read(const char *path, Blob *out, size_t limit);
int xa_write_new(const char *path, const void *data, size_t size);
int xa_fetch(const char *url, Blob *out, char *error, size_t cap);
int xa_users(User *users, char *error, size_t cap);
int xa_profile(uint32_t uid, char *path, size_t cap);
void xa_notify(const char *message);
void xa_notify_avatar(const char *message, const void *png, size_t len);
void xa_log(const char *message);
int xa_platform_init(void);
void xa_platform_end(void);
const char *xa_language(int *system_id);

void avatar_free(Avatar *avatar);
int avatar_create(const void *png, size_t len, Blob *zip, char *error,
                  size_t cap);
int avatar_open(const void *zip, size_t len, Avatar *avatar, char *error,
                size_t cap);
int avatar_install(Avatar *avatar, uint32_t uid, char *error, size_t cap);
int xa_filename(const char *name);
int xa_server(void);
int xa_configure(int argc, char **argv);
int xa_run(void);
#endif

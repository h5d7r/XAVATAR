#include "miniz.h"
#include "xavatar.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 440
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

static const char *names[] = {
    "avatar.png",     "avatar64.dds",   "avatar128.dds", "avatar260.dds",
    "avatar440.dds",  "picture.png",    "picture64.dds", "picture128.dds",
    "picture260.dds", "picture440.dds", "online.json"};
static const int dims[] = {440, 64, 128, 260, 440, 440, 64, 128, 260, 440, 0};
/* Keep identity and verification claims out of a newly generated package. */
static const char metadata[] = "{\"avatarUrl\":\"\",\"firstName\":\"\","
                               "\"lastName\":\"\",\"pictureUrl\":\"\"}";

static uint32_t le32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static void put32(unsigned char *p, uint32_t v) {
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
  p[3] = v >> 24;
}

static int png_valid(const Blob *b) {
  int w, h, c;
  if (b->size > XA_MAX_PNG || b->size < 8 ||
      memcmp(b->data, "\211PNG\r\n\032\n", 8))
    return 0;
  if (!stbi_info_from_memory(b->data, (int)b->size, &w, &h, &c) || w != 440 ||
      h != 440)
    return 0;
  unsigned char *pixels =
      stbi_load_from_memory(b->data, (int)b->size, &w, &h, &c, 4);
  if (!pixels)
    return 0;
  stbi_image_free(pixels);
  return 1;
}

static int dds_valid(const Blob *b, int dimension) {
  if (b->size < 128 || memcmp(b->data, "DDS ", 4) || le32(b->data + 4) != 124 ||
      le32(b->data + 12) != (unsigned)dimension ||
      le32(b->data + 16) != (unsigned)dimension || le32(b->data + 76) != 32 ||
      !(le32(b->data + 80) & 4))
    return 0;
  unsigned block;
  if (!memcmp(b->data + 84, "DXT1", 4))
    block = 8;
  else if (!memcmp(b->data + 84, "DXT5", 4))
    block = 16;
  else
    return 0;
  unsigned levels = le32(b->data + 28), w = dimension, h = dimension;
  if (!levels)
    levels = 1;
  unsigned maxlevels = 1;
  for (unsigned v = dimension; v > 1; v >>= 1)
    maxlevels++;
  if (levels > maxlevels || le32(b->data + 112))
    return 0; /* no cubemaps or volume textures */
  size_t expected = 128;
  for (unsigned level = 0; level < levels; level++) {
    expected += ((w + 3) / 4) * ((h + 3) / 4) * block;
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
  }
  return b->size == expected;
}

void avatar_free(Avatar *a) {
  for (int i = 0; i < a->count; i++)
    free(a->entries[i].blob.data);
  memset(a, 0, sizeof(*a));
}

int avatar_open(const void *data, size_t len, Avatar *a, char *error,
                size_t cap) {
  mz_zip_archive zip = {0};
  int seen[XA_MAX_ENTRIES] = {0};
  memset(a, 0, sizeof(*a));
  snprintf(error, cap,
           "Invalid XAVATAR package (expected PNG and four DDS textures).");
  if (!len || len > XA_MAX_PACKAGE ||
      !mz_zip_reader_init_mem(&zip, data, len, 0))
    return -1;
  unsigned count = mz_zip_reader_get_num_files(&zip);
  if (count < 5 || count > XA_MAX_ENTRIES)
    goto bad;
  size_t total = 0;
  for (unsigned i = 0; i < count; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory ||
        st.m_is_encrypted || (st.m_method != 0 && st.m_method != 8))
      goto bad;
    int index = -1;
    for (int j = 0; j < XA_MAX_ENTRIES; j++)
      if (!strcmp(st.m_filename, names[j]))
        index = j;
    if (index < 0 || seen[index]++ || st.m_uncomp_size == 0 ||
        st.m_uncomp_size > (index == 10 ? 16384u : XA_MAX_PNG))
      goto bad;
    total += (size_t)st.m_uncomp_size;
    if (total > XA_MAX_PACKAGE)
      goto bad;
    AvatarEntry *e = &a->entries[a->count++];
    strcpy(e->name, names[index]);
    e->blob.data = mz_zip_reader_extract_to_heap(&zip, i, &e->blob.size, 0);
    if (!e->blob.data)
      goto bad; /* includes CRC validation */
    if (index != 10 &&
        !((index == 0 || index == 5) ? png_valid(&e->blob)
                                     : dds_valid(&e->blob, dims[index])))
      goto bad;
  }
  for (int i = 0; i < 5; i++)
    if (!seen[i])
      goto bad;
  int pictures = 0;
  for (int i = 5; i < 10; i++)
    pictures += seen[i];
  if (pictures != 0 && pictures != 5)
    goto bad;
  mz_zip_reader_end(&zip);
  return 0;
bad:
  mz_zip_reader_end(&zip);
  avatar_free(a);
  return -1;
}

/* Bilinear resampling from the original crop, keeping RGBA for DXT5 alpha. */
static unsigned char *resize_rgba(const unsigned char *src, int size) {
  unsigned char *dst = malloc((size_t)size * size * 4);
  if (!dst)
    return NULL;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      double fx = (x + .5) * 440 / size - .5, fy = (y + .5) * 440 / size - .5;
      if (fx < 0)
        fx = 0;
      if (fy < 0)
        fy = 0;
      int x0 = (int)fx, y0 = (int)fy;
      int x1 = x0 < 439 ? x0 + 1 : x0, y1 = y0 < 439 ? y0 + 1 : y0;
      double dx = fx - x0, dy = fy - y0;
      for (int c = 0; c < 4; c++) {
        double top = src[(y0 * 440 + x0) * 4 + c] * (1 - dx) +
                     src[(y0 * 440 + x1) * 4 + c] * dx;
        double bot = src[(y1 * 440 + x0) * 4 + c] * (1 - dx) +
                     src[(y1 * 440 + x1) * 4 + c] * dx;
        dst[(y * size + x) * 4 + c] =
            (unsigned char)(top * (1 - dy) + bot * dy + .5);
      }
    }
  return dst;
}

static Blob make_dds(const unsigned char *source, int size) {
  Blob b = {0};
  unsigned char *pixels = resize_rgba(source, size);
  if (!pixels)
    return b;
  b.size = 128 + (size_t)((size + 3) / 4) * ((size + 3) / 4) * 16;
  b.data = calloc(1, b.size);
  if (!b.data) {
    free(pixels);
    b.size = 0;
    return b;
  }
  memcpy(b.data, "DDS ", 4);
  put32(b.data + 4, 124);
  put32(b.data + 8, 0x81007);
  put32(b.data + 12, size);
  put32(b.data + 16, size);
  put32(b.data + 20, (uint32_t)(b.size - 128));
  put32(b.data + 76, 32);
  put32(b.data + 80, 4);
  memcpy(b.data + 84, "DXT5", 4);
  put32(b.data + 108, 0x1000);
  unsigned char *out = b.data + 128;
  for (int y = 0; y < size; y += 4)
    for (int x = 0; x < size; x += 4) {
      unsigned char block[64];
      for (int by = 0; by < 4; by++)
        for (int bx = 0; bx < 4; bx++) {
          int px = x + bx < size ? x + bx : size - 1,
              py = y + by < size ? y + by : size - 1;
          memcpy(block + (by * 4 + bx) * 4, pixels + (py * size + px) * 4, 4);
        }
      stb_compress_dxt_block(out, block, 1, STB_DXT_HIGHQUAL);
      out += 16;
    }
  free(pixels);
  return b;
}

int avatar_create(const void *png, size_t len, Blob *out, char *error,
                  size_t cap) {
  Blob input = {(unsigned char *)png, len};
  memset(out, 0, sizeof(*out));
  if (!png_valid(&input)) {
    snprintf(error, cap, "Upload a cropped 440 x 440 PNG image.");
    return -1;
  }
  int w, h, c, pnglen;
  unsigned char *pixels = stbi_load_from_memory(png, (int)len, &w, &h, &c, 4);
  if (!pixels)
    goto memory;
  /* Re-encode so uploaded PNG metadata is not carried into the package. */
  unsigned char *clean =
      stbi_write_png_to_mem(pixels, 440 * 4, 440, 440, 4, &pnglen);
  if (!clean) {
    stbi_image_free(pixels);
    goto memory;
  }
  mz_zip_archive zip = {0};
  if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
    free(clean);
    stbi_image_free(pixels);
    goto memory;
  }
  int ok = mz_zip_writer_add_mem(&zip, "avatar.png", clean, (size_t)pnglen,
                                 MZ_DEFAULT_COMPRESSION) &&
           mz_zip_writer_add_mem(&zip, "picture.png", clean, (size_t)pnglen,
                                 MZ_DEFAULT_COMPRESSION);
  for (int i = 1; i < 5 && ok; i++) {
    Blob dds = make_dds(pixels, dims[i]);
    ok = dds.data &&
         mz_zip_writer_add_mem(&zip, names[i], dds.data, dds.size,
                               MZ_DEFAULT_COMPRESSION) &&
         mz_zip_writer_add_mem(&zip, names[i + 5], dds.data, dds.size,
                               MZ_DEFAULT_COMPRESSION);
    free(dds.data);
  }
  if (ok)
    ok = mz_zip_writer_add_mem(&zip, "online.json", metadata,
                               sizeof(metadata) - 1, MZ_DEFAULT_COMPRESSION);
  if (ok)
    ok = mz_zip_writer_finalize_heap_archive(&zip, (void **)&out->data,
                                             &out->size);
  mz_zip_writer_end(&zip);
  free(clean);
  stbi_image_free(pixels);
  if (ok)
    return 0;
memory:
  snprintf(error, cap, "Could not allocate or encode avatar data.");
  return -1;
}

/* Stage images and back up originals before changing profile files. Existing
   metadata is preserved; a missing online.json gets a neutral scaffold. */
int avatar_install(Avatar *a, uint32_t uid, char *error, size_t cap) {
  User users[XA_MAX_USERS];
  int n = xa_users(users, error, cap), found = 0;
  if (n < 0)
    return -1;
  for (int i = 0; i < n; i++)
    if (users[i].id == uid)
      found = 1;
  if (!found) {
    snprintf(error, cap, "The selected PS4 user is no longer available.");
    return -1;
  }
  char profile[1200], stage[1400], backupbase[1200], backup[1400];
  xa_profile(uid, profile, sizeof(profile));
  if (xa_mkdirs(profile)) {
    snprintf(error, cap, "Cannot access the profile cache: %s",
             strerror(errno));
    return -1;
  }
  xa_path(backupbase, sizeof(backupbase), "/data/XAVATAR/backups");
  if (xa_mkdirs(backupbase)) {
    snprintf(error, cap, "Cannot create profile backup directory.");
    return -1;
  }
  snprintf(backup, sizeof(backup), "%s/%08x-XXXXXX", backupbase, uid);
  if (!mkdtemp(backup)) {
    snprintf(error, cap, "Cannot create profile backup.");
    return -1;
  }
  snprintf(stage, sizeof(stage), "%s/.xavatar-XXXXXX", profile);
  if (!mkdtemp(stage)) {
    rmdir(backup);
    snprintf(error, cap, "Cannot stage profile images.");
    return -1;
  }
  int existed[XA_MAX_ENTRIES] = {0}, changed[XA_MAX_ENTRIES] = {0},
      apply[XA_MAX_ENTRIES] = {0};
  char target[1600], staged[1600], saved[1600];
  int rollback_failed = 0;
  for (int i = 0; i < a->count; i++) {
    AvatarEntry *e = &a->entries[i];
    snprintf(target, sizeof(target), "%s/%s", profile, e->name);
    snprintf(staged, sizeof(staged), "%s/%s", stage, e->name);
    snprintf(saved, sizeof(saved), "%s/%s", backup, e->name);
    struct stat st;
    if (!strcmp(e->name, "online.json")) {
      if (!lstat(target, &st))
        continue; /* account identity always wins */
      if (errno != ENOENT)
        goto fail;
      apply[i] = 1;
      if (xa_write_new(staged, metadata, sizeof(metadata) - 1))
        goto fail;
      continue;
    }
    apply[i] = 1;
    if (!lstat(target, &st)) {
      Blob old = {0};
      if (!S_ISREG(st.st_mode) || xa_read(target, &old, XA_MAX_PNG))
        goto fail;
      int result = xa_write_new(saved, old.data, old.size);
      free(old.data);
      if (result)
        goto fail;
      existed[i] = 1;
    } else if (errno != ENOENT)
      goto fail;
    if (xa_write_new(staged, e->blob.data, e->blob.size))
      goto fail;
  }
  for (int i = 0; i < a->count; i++) {
    AvatarEntry *e = &a->entries[i];
    if (!apply[i])
      continue;
    snprintf(target, sizeof(target), "%s/%s", profile, e->name);
    snprintf(staged, sizeof(staged), "%s/%s", stage, e->name);
    if (rename(staged, target))
      goto fail;
    changed[i] = 1;
  }
  rmdir(stage);
  snprintf(error, cap, "Avatar installed. Sign out and back in to refresh it.");
  xa_log(backup);
  return 0;
fail:
  for (int i = 0; i < a->count; i++) {
    const char *name = a->entries[i].name;
    if (!apply[i])
      continue;
    snprintf(target, sizeof(target), "%s/%s", profile, name);
    snprintf(staged, sizeof(staged), "%s/%s", stage, name);
    snprintf(saved, sizeof(saved), "%s/%s", backup, name);
    unlink(staged);
    if (changed[i]) {
      if (existed[i]) {
        Blob old = {0};
        if (xa_read(saved, &old, XA_MAX_PNG) ||
            xa_write_new(staged, old.data, old.size) || rename(staged, target))
          rollback_failed = 1;
        free(old.data);
      } else if (unlink(target))
        rollback_failed = 1;
    }
  }
  rmdir(stage);
  if (rollback_failed)
    snprintf(error, cap,
             "Install and rollback failed. Restore originals from %s", backup);
  else
    snprintf(error, cap,
             "Install failed; profile changes were rolled back. Backup: %s",
             backup);
  return -1;
}

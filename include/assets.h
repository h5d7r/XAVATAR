#ifndef XAVATAR_ASSETS_H
#define XAVATAR_ASSETS_H
#include <stddef.h>

typedef struct {
  const char *name;
  const unsigned char *data;
  size_t size;
} XaAsset;

/* Resources are generated from the separate frontend files at build time. */
const XaAsset *xa_asset_get(const char *name);
#endif

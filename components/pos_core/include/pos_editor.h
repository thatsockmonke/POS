#pragma once
#include "pos_fs.h"
#include "pos_install.h"
#include <stdio.h>
#define POS_EDITOR_DRAFT_ROOT "POS/Users/Owner/EditorDrafts"

typedef struct {
  char root[POS_FS_PATH], source[241], directory[POS_FS_PATH];
  uint32_t length, cursor, generation, original_size, original_crc;
  uint32_t base_size, base_crc, added_size, added_crc;
  bool original_exists, dirty;
  unsigned slot;
} pos_editor_t;
/* All document paths are relative, user-owned paths. Working text and CSV
 * piece indexes are kept on SD; no document-sized allocation is made. */
int pos_editor_open(pos_editor_t *editor, const char *root,
                    const char *relative, pos_io_progress_t progress, void *ctx,
                    pos_error_t *error);
int pos_editor_recover(pos_editor_t *editor, const char *root,
                       const char *folder, pos_error_t *error);
int pos_editor_read(const pos_editor_t *editor, uint32_t offset, void *buffer,
                    size_t capacity, size_t *read);
int pos_editor_replace(pos_editor_t *editor, uint32_t remove, const char *text,
                       size_t length, pos_error_t *error);
int pos_editor_save(pos_editor_t *editor, const char *relative, bool replace,
                    pos_io_progress_t progress, void *ctx, pos_error_t *error);
int pos_editor_find(pos_editor_t *editor, const char *text, uint32_t start,
                    uint32_t *found, pos_io_progress_t progress, void *ctx);
int pos_editor_line(pos_editor_t *editor, uint32_t line, uint32_t *offset,
                    pos_io_progress_t progress, void *ctx);
void pos_editor_discard(pos_editor_t *editor);

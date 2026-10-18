#pragma once
#include "pos_fs.h"
#include "pos_install.h"
/* Validates every parent too; protected containers and symlinks are refused. */
bool pos_files_mutable(const char *root, const char *relative);
int pos_files_copy(const char *root, const char *source,
                   const char *destination, pos_io_progress_t progress,
                   void *ctx, pos_error_t *error);
int pos_files_move(const char *root, const char *source,
                   const char *destination, pos_error_t *error);
int pos_files_folder(const char *root, const char *relative,
                     pos_error_t *error);
/* Delete is reversible: move into a unique user-owned trash entry with
 * metadata. */
int pos_files_trash(const char *root, const char *relative, char entry[241],
                    pos_error_t *error);
int pos_files_restore(const char *root, const char *entry, pos_error_t *error);
#define POS_TRASH "POS/Users/Owner/Trash"
int pos_files_remove(const char *root, const char *relative,
                     pos_io_progress_t progress, void *ctx, pos_error_t *error);

#define POS_FILE_SELECTION_MAX 32
typedef struct {
  char paths[POS_FILE_SELECTION_MAX][241];
  unsigned count;
  bool moving;
} pos_file_clipboard_t;
/* Batch preflight rejects collisions/nesting. On a partial cut, only remaining
 * sources stay in the clipboard; the source of an incomplete copy is retained.
 */
int pos_files_paste(const char *, pos_file_clipboard_t *, const char *,
                    pos_io_progress_t, void *, pos_error_t *);
typedef int (*pos_file_match_t)(void *, const char *relative,
                                bool content_match);
/* Case-insensitive filenames + streamed text content. Callback may stop early.
 * Returns 0 complete, 1 cancelled/result limit, -1 error. Symlinks/system
 * skipped. */
int pos_files_search(const char *, const char *, const char *, bool,
                     pos_file_match_t, pos_io_progress_t, void *,
                     pos_error_t *);

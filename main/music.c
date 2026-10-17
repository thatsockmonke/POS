#include "pos_device.h"
#include "pos_music.h"
#include "pos_peripherals.h"
#include "ui.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static unsigned current_index(const char *root, const char *folder,
                              const char *track) {
  char path[POS_FS_PATH];
  pos_fs_join(path, root, folder);
  DIR *d = opendir(path);
  if (!d)
    return 0;
  struct dirent *entry;
  unsigned at = 0, result = 0;
  while ((entry = readdir(d)))
    if (pos_music_supported(entry->d_name)) {
      char relative[241];
      if (snprintf(relative, sizeof(relative), "%s%s%s", folder,
                   *folder ? "/" : "",
                   entry->d_name) >= (int)sizeof(relative) ||
          !pos_files_mutable(root, relative))
        continue;
      struct stat st;
      pos_fs_join(path, root, relative);
      if (pos_fs_lstat(path, &st) || !S_ISREG(st.st_mode))
        continue;
      if (!strcmp(relative, track)) {
        result = at;
        break;
      }
      ++at;
    }
  closedir(d);
  return result;
}
static void player(const char *root, const char *initial, const char *playlist,
                   bool single) {
  char track[241], folder[241];
  strcpy(track, initial);
  strcpy(folder, track);
  char *slash = strrchr(folder, '/');
  if (slash)
    *slash = 0;
  else
    strcpy(folder, "");
  unsigned index = playlist ? 0 : current_index(root, folder, track),
           volume = 30, total = 0;
  bool shuffle = false, paused = false, leave = false;
  while (!leave && !pos_home_pending()) {
    pos_error_t e = {0};
    pos_music_t *music = pos_music_open(root, track, &e);
    if (!music) {
      pos_ui_notice("Cannot play", e.message);
      break;
    }
    bool change = false;
    uint64_t last_frame = UINT64_MAX;
    while (!leave && !change && !pos_home_pending()) {
      pos_event_t event;
      if (pos_device_event(&event, paused ? 100 : 0) && event.pressed) {
        int key = pos_navigation(&event);
        if (key == POS_KEY_HOME) {
          leave = true;
          break;
        }
        if (key == POS_KEY_ESCAPE) {
          leave = true;
          break;
        }
        if (key == ' ' || key == POS_KEY_ENTER) {
          paused = !paused;
          pos_audio_stop();
        }
        if (key == POS_KEY_UP) {
          if (volume < 100)
            volume += 5;
          last_frame = UINT64_MAX;
        }
        if (key == POS_KEY_DOWN) {
          if (volume >= 5)
            volume -= 5;
          last_frame = UINT64_MAX;
        }
        if (!single && (key == 'r' || key == 'R')) {
          shuffle = !shuffle;
          last_frame = UINT64_MAX;
        }
        if (key == 'p' || key == 'P') {
          pos_audio_stop();
          char name[81] = "Favorites.m3u";
          if (pos_ui_text("Add to playlist", "Playlist filename (.m3u)", name,
                          sizeof(name), false)) {
            const char *ext = strrchr(name, '.');
            char relative[160];
            if (!ext || strcasecmp(ext, ".m3u") || strchr(name, '/'))
              pos_ui_notice("Invalid name", "Use a simple .m3u filename.");
            else {
              snprintf(relative, sizeof(relative), "Music/Playlists/%s", name);
              pos_ui_notice(
                  "Playlist",
                  pos_music_playlist_add(root, relative, track, &e)
                      ? e.message
                      : "Track added. Playlists are ordinary .m3u text files.");
            }
          }
          last_frame = UINT64_MAX;
        }
        if (key == 'h' || key == 'H') {
          pos_audio_stop();
          pos_ui_notice(
              single ? "Audio Player controls" : "Music controls",
              single
                  ? "Space/Enter: pause\nLeft/Right: previous/next\nUp/Down: "
                    "volume\nP: add to playlist\nEOF: "
                    "stop/back\nEsc: stop/back\nFn+Space: close app"
                  : "Space/Enter: pause\nLeft/Right: previous/next\nUp/Down: "
                    "volume\nR: shuffle\nP: add to playlist\nEsc: "
                    "stop/back\nFn+Space: close app");
          last_frame = UINT64_MAX;
        }
        if (key == POS_KEY_LEFT || key == POS_KEY_RIGHT) {
          if (playlist)
            pos_music_playlist_track(root, playlist, 0, folder, &total);
          else
            pos_music_track(root, folder, 0, (char[241]){0}, &total);
          if (total) {
            index = key == POS_KEY_LEFT ? (index + total - 1) % total
                                        : (index + 1) % total;
            change = true;
          }
        }
      }
      if (change || leave)
        break;
      if (!paused) {
        const int16_t *pcm;
        size_t frames;
        int result = pos_music_read(music, &pcm, &frames, volume, &e);
        pos_music_status_t status = pos_music_status(music);
        if (result < 0 ||
            (frames && pos_audio_write(pcm, frames, status.rate, &e))) {
          pos_audio_stop();
          pos_ui_notice("Playback stopped", e.message);
          leave = true;
          break;
        }
        if (status.ended) {
          if (single) {
            leave = true;
            break;
          }
          if (playlist)
            pos_music_playlist_track(root, playlist, 0, (char[241]){0}, &total);
          else
            pos_music_track(root, folder, 0, (char[241]){0}, &total);
          if (!total) {
            leave = true;
            break;
          }
          index = shuffle ? (unsigned)rand() % total : (index + 1) % total;
          change = true;
          break;
        }
      }
      pos_music_status_t status = pos_music_status(music);
      uint64_t seconds = status.rate ? status.frames / status.rate : 0;
      if (seconds != last_frame || paused) {
        pos_view_t view;
        pos_view_clear(&view, single ? "Audio Player" : "POS Music");
        const char *name = strrchr(track, '/');
        name = name ? name + 1 : track;
        pos_view_line(&view, 2, "%.30s", name);
        pos_ui_style(&view);
        view.visual = POS_VISUAL_MUSIC;
        view.visual_motion = (int)(seconds & 7);
        view.visual_level = status.total_bytes ? (uint64_t)status.bytes * 1000 /
                                                     status.total_bytes
                                               : 0;
        pos_view_line(
            &view, 3, "%s  %llu:%02llu", paused ? "Paused" : "Playing",
            (unsigned long long)seconds / 60, (unsigned long long)seconds % 60);
        pos_view_line(&view, 11, "%11sVol %u%% %s", "", volume,
                      !single && shuffle ? "Shuffle" : "");
        pos_view_line(&view, 13, "Space:pause   U/D:volume");
        pos_view_line(&view, 14, "L/R:track   P:playlist H:help");
        pos_view_line(&view, 15, "Esc:back  Fn+':close G0:home");
        pos_device_show(&view);
        last_frame = seconds;
      }
    }
    pos_audio_stop();
    pos_music_close(music);
    if (change) {
      int result =
          playlist
              ? pos_music_playlist_track(root, playlist, index, track, &total)
              : pos_music_track(root, folder, index, track, &total);
      if (result) {
        pos_ui_notice(
            "Library changed",
            "Track unavailable. Rescan your music folder or playlist.");
        break;
      }
    }
  }
  pos_audio_stop();
}
void pos_ui_music_file(const char *root, const char *relative) {
  player(root, relative, NULL, true);
}
void pos_ui_music(const char *root) {
  while (!pos_home_pending()) {
    const char *items[] = {"Browse music", "Open playlist",
                           "Controls / formats", "Back"};
    int choice = pos_ui_menu("POS Music", items, 4);
    if (choice < 0 || choice == 3)
      break;
    if (choice == 2) {
      pos_ui_notice(
          "POS Music",
          "Put MP3 or PCM 16-bit WAV files in Music (folders are supported). "
          "Space pauses; Left/Right changes tracks; Up/Down changes volume; R "
          "toggles "
          "shuffle; P adds to a .m3u playlist. Audio stops on closing.");
      continue;
    }
    char path[241];
    if (!pos_ui_pick_file_from(
            root, choice ? "Choose .m3u playlist" : "Choose MP3 / WAV",
            choice ? "Music/Playlists" : "Music", path))
      continue;
    if (!choice) {
      if (pos_music_supported(path))
        player(root, path, NULL, false);
      else
        pos_ui_notice("Unsupported format", "Choose MP3 or 16-bit PCM WAV.");
    } else {
      char track[241];
      unsigned total;
      if (pos_music_playlist_track(root, path, 0, track, &total))
        pos_ui_notice("Playlist empty",
                      "Use an .m3u file with SD-root-relative user audio "
                      "paths. Add tracks with P during playback.");
      else
        player(root, track, path, false);
    }
  }
}

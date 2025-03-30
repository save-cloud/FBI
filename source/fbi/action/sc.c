#include "../loader.h"
#include "action.h"
#include <3ds.h>

int exit_code = 0;
int is_sc_called = 0;
int is_local_cia = 0;
u64 title_id = 0;
char *sc_3dsx_path = NULL;

void install_from_sc_done(void *data) {
  char *args = is_local_cia ? "local" : "cloud";
  if (envIsHomebrew()) {
    if (sc_3dsx_path != NULL) {
      loader_launch_file(sc_3dsx_path, args);
      exit_code = 1;
    }
  } else if (title_id != 0) {
    u8 param[0x300];
    memset(param, 0, sizeof(param));
    strncpy((char *)param, args, sizeof(param));
    u8 hmac[0x20];

    aptSetChainloader(title_id, MEDIATYPE_SD);
    aptSetChainloaderArgs(param, sizeof(param), hmac);
    exit_code = 1;
  }
}

static bool remoteinstall_get_urls_by_path(const char *path, char *out, size_t size) {
  if (out == NULL || size == 0) {
    return false;
  }

  Handle file = 0;
  if (R_FAILED(FSUSER_OpenFileDirectly(
          &file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
          fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0))) {
    return false;
  }

  u32 bytesRead = 0;
  FSFILE_Read(file, &bytesRead, 0, out, size - 1);
  out[bytesRead] = '\0';

  FSFILE_Close(file);

  return bytesRead != 0;
}

void install_start_if_from_sc(int argc, const char *argv[]) {
  // Install from URL if a URL was passed as an argument.
  if (envIsHomebrew()) {
    if (argc > 2) {
      is_sc_called = 1;
      char *url = (char *)calloc(1, 1024 * INSTALL_URLS_MAX);
      remoteinstall_get_urls_by_path(argv[1], url, 1024 * INSTALL_URLS_MAX);
      sc_3dsx_path = (char *)argv[2];
      // file path
      if (strncmp(url, "/", 1) == 0) {
        is_local_cia = 1;
        action_install_cia_by_path(url);
      } else {
        action_install_url("Install From URL?", url, NULL, NULL, NULL,
                           install_from_sc_done, NULL);
      }
      free(url);
    }
  } else {
    u8 param[0x300];
    u8 hmac[0x20];
    bool received = false;
    APT_ReceiveDeliverArg(param, 0x300, hmac, &title_id, &received);
    if (received) {
      int len = 0;
      for (int i = 0; i < 0x300; i++) {
        if (param[i] == 0) {
          len = i;
          break;
        }
      }
      if (len > 0) {
        char *params_str = (char *)param;
        if (strncmp(params_str, "sc:", 3) == 0) {
          is_sc_called = 1;
          char *path = params_str + 3;
          char *url = (char *)calloc(1, 1024 * INSTALL_URLS_MAX);
          remoteinstall_get_urls_by_path(path, url, 1024 * INSTALL_URLS_MAX);
          // file path
          if (strncmp(url, "/", 1) == 0) {
            is_local_cia = 1;
            action_install_cia_by_path(url);
          } else {
            action_install_url("Install From URL?", url, NULL, NULL, NULL,
                               install_from_sc_done, NULL);
          }
          free(url);
        }
      }
    }
  }
}

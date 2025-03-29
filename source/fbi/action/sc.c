#include "../loader.h"
#include <3ds.h>

extern int exit_code;
extern u64 title_id;
extern int is_local_cia;
extern char *sc_3dsx_path;

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

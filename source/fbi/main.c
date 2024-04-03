#include <sys/iosupport.h>
#include <malloc.h>

#include <3ds.h>

#include "../core/clipboard.h"
#include "../core/error.h"
#include "../core/fs.h"
#include "../core/screen.h"
#include "../core/task/task.h"
#include "../core/ui/ui.h"
#include "action/action.h"
#include "section.h"
#include "task/uitask.h"
#include "loader.h"

#define CURRENT_KPROCESS (*(void**) 0xFFFF9004)

#define KPROCESS_PID_OFFSET_OLD (0xB4)
#define KPROCESS_PID_OFFSET_NEW (0xBC)

static bool backdoor_ran = false;
static bool n3ds = false;
static u32 old_pid = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
static __attribute__((naked)) Result svcGlobalBackdoor(s32 (*callback)()) {
    asm volatile(
            "svc 0x30\n"
            "bx lr"
    );
}
#pragma GCC diagnostic pop

static s32 patch_pid_kernel() {
    u32 *pidPtr = (u32*) (CURRENT_KPROCESS + (n3ds ? KPROCESS_PID_OFFSET_NEW : KPROCESS_PID_OFFSET_OLD));

    old_pid = *pidPtr;
    *pidPtr = 0;

    backdoor_ran = true;
    return 0;
}

static s32 restore_pid_kernel() {
    u32 *pidPtr = (u32*) (CURRENT_KPROCESS + (n3ds ? KPROCESS_PID_OFFSET_NEW : KPROCESS_PID_OFFSET_OLD));

    *pidPtr = old_pid;

    backdoor_ran = true;
    return 0;
}

static bool attempt_patch_pid() {
    backdoor_ran = false;
    APT_CheckNew3DS(&n3ds);

    svcGlobalBackdoor(patch_pid_kernel);
    srvExit();
    srvInit();
    svcGlobalBackdoor(restore_pid_kernel);

    return backdoor_ran;
}

static void (*exit_funcs[16])()= {NULL};
static u32 exit_func_count = 0;

static void* soc_buffer = NULL;

void cleanup_services() {
    for(u32 i = 0; i < exit_func_count; i++) {
        if(exit_funcs[i] != NULL) {
            exit_funcs[i]();
            exit_funcs[i] = NULL;
        }
    }

    exit_func_count = 0;

    if(soc_buffer != NULL) {
        free(soc_buffer);
        soc_buffer = NULL;
    }
}

#define INIT_SERVICE(initStatement, exitFunc) (R_SUCCEEDED(res = (initStatement)) && (exit_funcs[exit_func_count++] = (exitFunc)))

Result init_services() {
    Result res = 0;

    soc_buffer = memalign(0x1000, 0x100000);
    if(soc_buffer != NULL) {
        Handle tempAM = 0;
        if(R_SUCCEEDED(res = srvGetServiceHandle(&tempAM, "am:net"))) {
            svcCloseHandle(tempAM);

            if(INIT_SERVICE(amInit(), amExit)
               && INIT_SERVICE(cfguInit(), cfguExit)
               && INIT_SERVICE(acInit(), acExit)
               && INIT_SERVICE(ptmuInit(), ptmuExit)
               && INIT_SERVICE(pxiDevInit(), pxiDevExit)
               && INIT_SERVICE(httpcInit(0), httpcExit)
               && INIT_SERVICE(socInit(soc_buffer, 0x100000), (void (*)()) socExit));
        }
    } else {
        res = R_APP_OUT_OF_MEMORY;
    }

    if(R_FAILED(res)) {
        cleanup_services();
    }

    return res;
}

static u32 old_time_limit = UINT32_MAX;

void init() {
    gfxInitDefault();

    Result romfsRes = romfsInit();
    if(R_FAILED(romfsRes)) {
        error_panic("Failed to mount RomFS: %08lX", romfsRes);
        return;
    }

    if(R_FAILED(init_services())) {
        if(!attempt_patch_pid()) {
            error_panic("Kernel backdoor not installed.\nPlease run a kernel exploit and try again.");
            return;
        }

        Result initRes = init_services();
        if(R_FAILED(initRes)) {
            error_panic("Failed to initialize services: %08lX", initRes);
            return;
        }
    }

    osSetSpeedupEnable(true);

    APT_GetAppCpuTimeLimit(&old_time_limit);
    Result cpuRes = APT_SetAppCpuTimeLimit(30);
    if(R_FAILED(cpuRes)) {
        error_panic("Failed to set syscore CPU time limit: %08lX", cpuRes);
        return;
    }

    AM_InitializeExternalTitleDatabase(false);

    screen_init();
    ui_init();
    task_init();
}

void cleanup() {
    clipboard_clear();

    task_exit();
    ui_exit();
    screen_exit();

    if(old_time_limit != UINT32_MAX) {
        APT_SetAppCpuTimeLimit(old_time_limit);
    }

    osSetSpeedupEnable(false);

    cleanup_services();

    romfsExit();

    gfxExit();
}

int exit_code = 0;
int use_curl_instead = 0;
int cancel_install = 0;
u64 title_id = 0;

void install_from_remote_done(void* data) {
  if (envIsHomebrew()) {
    char *from_3dsx_path = (char *)data;
    if(from_3dsx_path != NULL) {
      loader_launch_file(from_3dsx_path, NULL);
      exit_code = 1;
    }
  }  else if (title_id != 0 && cancel_install) {
    Result res = 0;

    if(R_SUCCEEDED(res = APT_PrepareToDoApplicationJump(0, title_id, 1))) {
        u8 param[0x300];
        u8 hmac[0x20];

        APT_DoApplicationJump(param, sizeof(param), hmac);
    }
  }
}

static bool remoteinstall_get_urls_by_path(const char* path, char* out, size_t size) {
    if(out == NULL || size == 0) {
        return false;
    }

    Handle file = 0;
    if(R_FAILED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0))) {
        return false;
    }

    u32 bytesRead = 0;
    FSFILE_Read(file, &bytesRead, 0, out, size - 1);
    out[bytesRead] = '\0';

    FSFILE_Close(file);

    return bytesRead != 0;
}

int main(int argc, const char* argv[]) {
    if(argc > 0 && envIsHomebrew()) {
        fs_set_3dsx_path(argv[0]);
    }

    init();

    mainmenu_open();

    // Install from URL if a URL was passed as an argument.
    if (envIsHomebrew()) {
      if(argc > 2) {
        use_curl_instead = 1;
        char* url = (char*) calloc(1, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX);
        remoteinstall_get_urls_by_path(argv[1], url, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX);
        action_install_url("Install From URL?",
            url,
            fs_get_3dsx_path(),
            (void *)argv[2],
            NULL,
            install_from_remote_done,
            NULL
        );
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
            use_curl_instead = 1;
            char *path = params_str + 3;
            char* url = (char*) calloc(1, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX);
            remoteinstall_get_urls_by_path(path, url, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX);
            action_install_url("Install From URL?",
                url,
                fs_get_3dsx_path(),
                NULL,
                NULL,
                install_from_remote_done,
                NULL
            );
            free(url);
          }
        }
      }
    }
    while(aptMainLoop() && ui_update());

    cleanup();

    return 0;
}

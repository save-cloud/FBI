#include <stdio.h>
#include <stdlib.h>

#include <3ds.h>

#include "action.h"
#include "../task/uitask.h"
#include "../../error.h"
#include "../../list.h"
#include "../../ui.h"
#include "../../../core/linkedlist.h"
#include "../../../core/stringutil.h"

typedef struct {
    list_item* selected;
    bool cia;
} install_titledb_data;

static void action_install_titledb_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, u32 index) {
    install_titledb_data* installData = (install_titledb_data*) data;

    if(installData->cia) {
        ui_draw_titledb_info_cia(view, installData->selected->data, x1, y1, x2, y2);
    } else {
        ui_draw_titledb_info_tdsx(view, installData->selected->data, x1, y1, x2, y2);
    }
}

static void action_update_titledb_finished(void* data) {
    task_populate_titledb_update_status(((install_titledb_data*) data)->selected);

    free(data);
}

void action_install_titledb(linked_list* items, list_item* selected, bool cia) {
    install_titledb_data* data = (install_titledb_data*) calloc(1, sizeof(install_titledb_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate install TitleDB data.");

        return;
    }

    data->selected = selected;
    data->cia = cia;

    titledb_info* info = (titledb_info*) selected->data;

    char url[64];
    char path3dsx[FILE_PATH_MAX];
    if(data->cia) {
        snprintf(url, DOWNLOAD_URL_MAX, "https://3ds.titledb.com/v1/cia/%lu/download", info->cia.id);
    } else {
        snprintf(url, DOWNLOAD_URL_MAX, "https://3ds.titledb.com/v1/tdsx/%lu/download", info->tdsx.id);

        char name[FILE_NAME_MAX];
        string_escape_file_name(name, info->meta.shortDescription, sizeof(name));
        snprintf(path3dsx, sizeof(path3dsx), "/3ds/%s/%s.3dsx", name, name);
    }

    action_install_url("Install the selected title from TitleDB?", url, path3dsx, data, action_update_titledb_finished, action_install_titledb_draw_top);
}
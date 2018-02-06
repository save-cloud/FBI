#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../error.h"
#include "../list.h"
#include "../resources.h"
#include "../ui.h"
#include "../../core/linkedlist.h"
#include "../../core/screen.h"

static list_item install = {"Install", COLOR_TEXT, action_install_titledb};

typedef struct {
    populate_titledb_data populateData;

    bool populated;
} titledb_data;

typedef struct {
    linked_list* items;
    list_item* selected;
} titledb_entry_data;

typedef struct {
    linked_list* items;
    list_item* selected;
    bool cia;
} titledb_action_data;

static void titledb_action_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    titledb_action_data* actionData = (titledb_action_data*) data;

    if(actionData->cia) {
        ui_draw_titledb_info_cia(view, actionData->selected->data, x1, y1, x2, y2);
    } else {
        ui_draw_titledb_info_tdsx(view, actionData->selected->data, x1, y1, x2, y2);
    }
}

static void titledb_action_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    titledb_action_data* actionData = (titledb_action_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);

        free(data);

        return;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        void(*action)(linked_list*, list_item*, bool) = (void(*)(linked_list*, list_item*, bool)) selected->data;

        ui_pop();
        list_destroy(view);

        action(actionData->items, actionData->selected, actionData->cia);

        free(data);

        return;
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &install);
    }
}

static void titledb_action_open(linked_list* items, list_item* selected, bool cia) {
    titledb_action_data* data = (titledb_action_data*) calloc(1, sizeof(titledb_action_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate TitleDB action data.");

        return;
    }

    data->items = items;
    data->selected = selected;
    data->cia = cia;

    list_display("TitleDB Action", "A: Select, B: Return", data, titledb_action_update, titledb_action_draw_top);
}

static void titledb_entry_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    titledb_entry_data* entryData = (titledb_entry_data*) data;

    if(selected != NULL) {
        if(strncmp(selected->name, "CIA", sizeof(selected->name)) == 0) {
            ui_draw_titledb_info_cia(view, entryData->selected->data, x1, y1, x2, y2);
        } else if(strncmp(selected->name, "3DSX", sizeof(selected->name)) == 0) {
            ui_draw_titledb_info_tdsx(view, entryData->selected->data, x1, y1, x2, y2);
        }
    }
}

static void titledb_entry_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    titledb_entry_data* entryData = (titledb_entry_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();

        linked_list_iter iter;
        linked_list_iterate(items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            free(linked_list_iter_next(&iter));
            linked_list_iter_remove(&iter);
        }

        list_destroy(view);
        free(data);

        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        titledb_action_open(entryData->items, entryData->selected, (bool) selected->data);
        return;
    }

    if(linked_list_size(items) == 0) {
        titledb_info* info = (titledb_info*) entryData->selected->data;

        if(info->cia.exists) {
            list_item* item = (list_item*) calloc(1, sizeof(list_item));
            if(item != NULL) {
                strncpy(item->name, "CIA", sizeof(item->name));
                item->data = (void*) true;
                item->color = info->cia.installed ? COLOR_TITLEDB_INSTALLED : COLOR_TITLEDB_NOT_INSTALLED;

                linked_list_add(items, item);
            }
        }

        if(info->tdsx.exists) {
            list_item* item = (list_item*) calloc(1, sizeof(list_item));
            if(item != NULL) {
                strncpy(item->name, "3DSX", sizeof(item->name));
                item->data = (void*) false;
                item->color = info->tdsx.installed ? COLOR_TITLEDB_INSTALLED : COLOR_TITLEDB_NOT_INSTALLED;

                linked_list_add(items, item);
            }
        }
    }
}

static void titledb_entry_open(linked_list* items, list_item* selected) {
    titledb_entry_data* data = (titledb_entry_data*) calloc(1, sizeof(titledb_entry_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate TitleDB entry data.");

        return;
    }

    data->items = items;
    data->selected = selected;

    list_display("TitleDB Entry", "A: Select, B: Return", data, titledb_entry_update, titledb_entry_draw_top);
}

static void titledb_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    titledb_data* listData = (titledb_data*) data;

    if(!listData->populateData.itemsListed) {
        static const char* text = "Loading title list, please wait...\nNOTE: Cancelling may take up to 15 seconds.";

        float textWidth;
        float textHeight;
        screen_get_string_size(&textWidth, &textHeight, text, 0.5f, 0.5f);
        screen_draw_string(text, x1 + (x2 - x1 - textWidth) / 2, y1 + (y2 - y1 - textHeight) / 2, 0.5f, 0.5f, COLOR_TEXT, true);
    } else if(selected != NULL && selected->data != NULL) {
        ui_draw_titledb_info(view, selected->data, x1, y1, x2, y2);
    }
}

static void titledb_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    titledb_data* listData = (titledb_data*) data;

    svcSignalEvent(listData->populateData.resumeEvent);

    if(hidKeysDown() & KEY_B) {
        if(!listData->populateData.finished) {
            svcSignalEvent(listData->populateData.cancelEvent);
            while(!listData->populateData.finished) {
                svcSleepThread(1000000);
            }
        }

        ui_pop();

        task_clear_titledb(items);
        list_destroy(view);

        free(listData);
        return;
    }

    if(!listData->populated || (hidKeysDown() & KEY_X)) {
        if(!listData->populateData.finished) {
            svcSignalEvent(listData->populateData.cancelEvent);
            while(!listData->populateData.finished) {
                svcSleepThread(1000000);
            }
        }

        listData->populateData.items = items;
        Result res = task_populate_titledb(&listData->populateData);
        if(R_FAILED(res)) {
            error_display_res(NULL, NULL, res, "Failed to initiate TitleDB list population.");
        }

        listData->populated = true;
    }

    if(listData->populateData.finished && R_FAILED(listData->populateData.result)) {
        error_display_res(NULL, NULL, listData->populateData.result, "Failed to populate TitleDB list.");

        listData->populateData.result = 0;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        svcClearEvent(listData->populateData.resumeEvent);

        titledb_entry_open(items, selected);
        return;
    }
}

void titledb_open() {
    titledb_data* data = (titledb_data*) calloc(1, sizeof(titledb_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate TitleDB data.");

        return;
    }

    data->populateData.finished = true;

    list_display("TitleDB.com", "A: Select, B: Return, X: Refresh", data, titledb_update, titledb_draw_top);
}
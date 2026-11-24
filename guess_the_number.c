#include "gtn.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#define STATS_PATH APP_DATA_PATH("stats.bin")

static void stats_load(GtnStats* st) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, STATS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        GtnStats tmp;
        if(storage_file_read(file, &tmp, sizeof(tmp)) == sizeof(tmp) &&
           tmp.magic == GTN_STATS_MAGIC && tmp.version == GTN_STATS_VERSION) {
            *st = tmp;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void stats_save(const GtnStats* st) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, STATS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, st, sizeof(*st));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

typedef struct {
    GtnState state;
    FuriMutex* mutex;
} GtnApp;

uint32_t gtn_random(void) {
    return furi_hal_random_get();
}

static uint32_t now_ms(void) {
    return furi_get_tick() / (furi_kernel_get_tick_frequency() / 1000); /* no overflow */
}

static void draw_callback(Canvas* canvas, void* ctx) {
    GtnApp* app = ctx;
    if(furi_mutex_acquire(app->mutex, 25) != FuriStatusOk) return;
    gtn_draw(canvas, &app->state, now_ms());
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

int32_t guess_the_number_app(void* p) {
    UNUSED(p);

    GtnApp* app = malloc(sizeof(GtnApp));
    gtn_init(&app->state, now_ms());
    stats_load(&app->state.stats);
    GtnStats to_save;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, app);
    view_port_input_callback_set(view_port, input_callback, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);

    InputEvent event;
    bool running = true;
    while(running) {
        FuriStatus status = furi_message_queue_get(queue, &event, 40);

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        uint32_t now = now_ms();
        if(status == FuriStatusOk) {
            gtn_input(&app->state, event.key, event.type, now);
        }
        gtn_tick(&app->state, now);
        GtnFx fx = app->state.fx;
        app->state.fx = GtnFxNone;
        running = !app->state.exit;
        bool save = app->state.stats_dirty || !running;
        if(save) {
            app->state.stats_dirty = false;
            to_save = app->state.stats;
        }
        furi_mutex_release(app->mutex);
        if(save) stats_save(&to_save);

        switch(fx) {
        case GtnFxBlink:
            notification_message(notifications, &sequence_blink_blue_10);
            break;
        case GtnFxSuccess:
            notification_message(notifications, &sequence_success);
            break;
        case GtnFxError:
            notification_message(notifications, &sequence_error);
            break;
        default:
            break;
        }

        view_port_update(view_port);
    }

    furi_record_close(RECORD_NOTIFICATION);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}

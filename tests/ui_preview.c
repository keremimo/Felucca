/* Render representative device pages through the real UI code for visual QA. */
#define FELUCCA_UI_PREVIEW 1
#include "seq_edit_test.c"

int main(int argc, char **argv)
{
    static const char *const pages[] = {"ENV", "LFO", "FX", "GLOBAL", "EDIT 1", "TRACKS", "PATTERN", "STEP"};
    uint32_t i;
    char path[512];
    if (argc != 2)
        return 2;
    reset(16);
    settings.palette = PAL_STUDIO;
    palette_set(settings.palette);
    for (i = 0; i < NTRK; i++) {
        uint32_t step;
        for (step = i % 4u; step < 16u; step += 4u) {
            trk[i].step[step].time = ST_NOTE;
            trk[i].step[step].n = 1;
            trk[i].step[step].note[0] = (uint8_t)(i == TRK_DRUM ? 36u + i : 60u + i * 5u);
        }
    }
    for (i = 0; i < sizeof pages / sizeof pages[0]; i++) {
        ui.page = (uint8_t)page_named(pages[i]);
        ui.home = 0;
        page_entered();
        ui_draw();
        snprintf(path, sizeof path, "%s/%s.ppm", argv[1], pages[i]);
        write_ppm(path);
    }
    ui.page = (uint8_t)page_named("EDIT 1");
    ui.home = 0;
    for (i = 0; i < NENGINES; i++) {
        set_engine(i);
        if (ENGINES[i] == &ENG_FM6)
            fm6_load(0, 0);
        ui.force = 1;
        ui_draw();
        snprintf(path, sizeof path, "%s/ENGINE-%s.ppm", argv[1], ENGINES[i]->name);
        write_ppm(path);
    }
    track_select(TRK_DRUM);
    ui.page = (uint8_t)page_named("EDIT 1");
    ui.home = 0;
    ui.force = 1;
    ui_draw();
    snprintf(path, sizeof path, "%s/DRUM.ppm", argv[1]);
    write_ppm(path);
    track_select(0);
    go_home();
    live_last[60u >> 5] |= 1u << (60u & 31u);
    live_last[64u >> 5] |= 1u << (64u & 31u);
    live_last[67u >> 5] |= 1u << (67u & 31u);
    memcpy(live_held, live_last, sizeof live_held);
    scope_fixture(47);
    ui_draw();
    snprintf(path, sizeof path, "%s/NOTES.ppm", argv[1]);
    write_ppm(path);
    memset(live_held, 0, sizeof live_held);
    memset(scope_buf, 0, sizeof scope_buf);
    ui.frame += 2;
    ui_draw();
    snprintf(path, sizeof path, "%s/NOTES-RELEASED.ppm", argv[1]);
    write_ppm(path);
    home_tap();
    ui_draw();
    snprintf(path, sizeof path, "%s/MIX-LEVEL.ppm", argv[1]);
    write_ppm(path);
    home_tap();
    ui_draw();
    snprintf(path, sizeof path, "%s/MIX-PAN.ppm", argv[1]);
    write_ppm(path);
    home_tap();
    ui_draw();
    snprintf(path, sizeof path, "%s/MIX-FX.ppm", argv[1]);
    write_ppm(path);
    go_home();
    ui.menu = 1;
    ui.force = 1;
    ui_draw();
    snprintf(path, sizeof path, "%s/SETTINGS.ppm", argv[1]);
    write_ppm(path);
    ui.menu = 0;
    ui.confirm = 2;
    ui.confirm_trk = 0;
    ui.force = 1;
    ui_draw();
    snprintf(path, sizeof path, "%s/CONFIRM.ppm", argv[1]);
    write_ppm(path);
    return 0;
}

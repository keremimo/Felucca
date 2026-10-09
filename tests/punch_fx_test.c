/* SPDX-License-Identifier: GPL-3.0-only */
/* FX must own every knob read, including detents arriving between UI handlers and on release. */
static int late_enc[7], enc_reads[7];
#define UI_ENC_TAKE_HOOK(e) do { host_enc[e] += late_enc[e]; late_enc[e] = 0; enc_reads[e]++; } while (0)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void reset_fx(void)
{
    seq_stop(); ui_power_on();
    settings_latch = perf_latch_on = kb_lock = 0;
    perf_latched = 0;
    memset(host_enc, 0, sizeof host_enc);
    memset(late_enc, 0, sizeof late_enc);
    memset(enc_reads, 0, sizeof enc_reads);
}
static void fast_fx_tap(void) { btn_down(B_FX); frame(); btn_up(B_FX); frame(); }

int main(void)
{
    int bad = 0, ok;
    for (unsigned locked = 0; locked < 2; locked++) {
        reset_fx();
        song.rec = 1; song.playing = 1;
        int16_t before[P_COUNT]; memcpy(before, TSEL->p, sizeof before);
        uint32_t count = motion.count;
        if (locked) { fast_fx_tap(); fast_fx_tap(); }
        else { btn_down(B_FX); frame(); }
        memset(enc_reads, 0, sizeof enc_reads);
        for (unsigned k = 0; k < 4; k++) {
            unsigned e = panel.enc[EN_K1 + k];
            int s = (k == 3 ? -10 : 10) * panel.dir[EN_K1 + k];
            host_enc[e] = s; late_enc[e] = s;
        }
        frame();
        ok = !memcmp(before, TSEL->p, sizeof before) && motion.count == count && ui.layer == LAYER_FX;
        for (unsigned k = 0; k < 4; k++) ok &= enc_reads[panel.enc[EN_K1 + k]] == 1;
        bad += check(locked ? "locked FX: late detents never reach HOME or motion recording"
                            : "held FX: late detents never reach HOME or motion recording", ok);
        frame();
        bad += check("next frame consumes queued detents as FX macros", perf_k[0] == 20 && perf_k[1] == 20 &&
                     perf_k[2] == 20 && perf_k[3] == 20 && !memcmp(before, TSEL->p, sizeof before));
        if (locked) fast_fx_tap();
        else { btn_up(B_FX); frame(); }
        bad += check("closing FX restores HOME with original preset and clears macros", ui.home && !ui.layer &&
                     !ui.lock && !perf_k[0] && !perf_k[1] && !perf_k[2] && !perf_k[3] &&
                     !memcmp(before, TSEL->p, sizeof before) && motion.count == count);
    }

    reset_fx();
    int16_t before[P_COUNT]; memcpy(before, TSEL->p, sizeof before);
    btn_down(B_FX); frame();
    for (unsigned k = 0; k < 4; k++) host_enc[panel.enc[EN_K1 + k]] = 5 * panel.dir[EN_K1 + k];
    btn_up(B_FX); frame();
    bad += check("knobs on FX release are swallowed, without a page tap or preset edits",
                 ui.home && !ui.layer && !memcmp(before, TSEL->p, sizeof before));

    reset_fx(); btn_down(B_FX); frame();
    turn(EN_K4, -25); ok = perf_k[3] == 25;
    turn(EN_K4, 10); ok &= perf_k[3] == 15;
    turn(EN_K4, 200); ok &= perf_k[3] == 0;
    turn(EN_K4, -200); ok &= perf_k[3] == 100;
    bad += check("DEPTH increases clockwise, decreases counterclockwise, clamps at 0 and 100 percent", ok);
    btn_up(B_FX); frame();
    btn_down(B_FX); key_down(white(8)); frame();
    perf_begin(CTL);
    turn(EN_K4, 25); ok = perf_harm_on() && perf_k[3] == 25;
    turn(EN_K4, -10); ok &= perf_k[3] == 15;
    bad += check("OCT UP shimmer still increases clockwise", ok);
    key_up(white(8)); btn_up(B_FX); frame();

    reset_fx(); fast_fx_tap(); fast_fx_tap();
    bad += check("double FX press locks performance mode over HOME", ui.lock == LAYER_FX && ui.layer == LAYER_FX && ui.home);
    key_down(white(4)); frame();
    bad += check("locked FX keys punch effects without playing notes", !gates() && (perf_held & PF_BIT(PF_LPF)));
    key_up(white(4)); frame();
    bad += check("locked FX key release ends its punch", !perf_held && ui.lock == LAYER_FX);
    fast_fx_tap();
    key_down(white(4)); frame();
    bad += check("FX press unlocks and restores musical keys", !ui.lock && !ui.layer && gates());
    key_up(white(4)); frame();
    int16_t *value; const param_desc_t *d = home_param(0, &value);
    int16_t original = *value;
    turn(EN_K1, original < d->max ? 1 : -1);
    bad += check("ordinary HOME knobs still edit after FX closes", *value != original);
    printf("punch FX: %s\n", bad ? "FAILED" : "all passed");
    return bad ? 1 : 0;
}

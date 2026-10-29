#include "controls.h"
#include <switch.h>
enum { JUMP=1, CROUCH=2, PRONE=4, RELOAD=8, MAP=16, PRIMARY=32,
    PISTOL=64, KNIFE=128, ITEM=256, GRENADE=512, AIM=1024, FIRE=2048,
    CURSOR=4096, CONFIRM=8192, CANCEL=16384 };
uint64_t swag_controls_update(SwagControls *s, uint64_t held) {
    uint64_t down = held & ~s->previous, up = s->previous & ~held, actions = 0;
    uint64_t combo = HidNpadButton_L | HidNpadButton_R;
    int chord = (held & combo) == combo;
    if (chord) {
        s->cancel_grenade = s->cancel_item = 1;
        if (!s->chord) { s->cursor = !s->cursor; actions |= CURSOR; }
    }
    if (s->cursor || chord) {
        s->cancel_grenade = s->cancel_item = 1;
        if (down & HidNpadButton_A) actions |= CONFIRM;
        if (down & HidNpadButton_B) actions |= CANCEL;
    } else {
        static const struct { uint64_t button; unsigned action; } mapping[] = {
            {HidNpadButton_A,JUMP},{HidNpadButton_B,CROUCH},{HidNpadButton_Y,PRONE},
            {HidNpadButton_X,RELOAD},{HidNpadButton_Plus,MAP},{HidNpadButton_Left,PRIMARY},
            {HidNpadButton_Right,PISTOL},{HidNpadButton_Up,KNIFE}};
        for (unsigned i=0; i<sizeof mapping/sizeof *mapping; ++i)
            if (down & mapping[i].button) actions |= mapping[i].action;
        if ((up & HidNpadButton_L) && !s->cancel_grenade) actions |= GRENADE;
        if ((up & HidNpadButton_Down) && !s->cancel_item) actions |= ITEM;
        if (held & HidNpadButton_ZL) actions |= AIM;
        if (held & HidNpadButton_ZR) actions |= FIRE;
    }
    if (!(held & HidNpadButton_L)) s->cancel_grenade = 0;
    if (!(held & HidNpadButton_Down)) s->cancel_item = 0;
    s->previous = held; s->chord = chord; return actions;
}
void swag_controls_log(FILE *log, uint64_t actions, int cursor) {
    static const char *names[] = {"jump","crouch","prone","reload","map","primary_smg",
        "pistol","knife","cycle_item","grenade","aim","fire","toggle_cursor","confirm","cancel"};
    for (unsigned i=0; i<sizeof names/sizeof *names; ++i)
        if (actions & (UINT64_C(1)<<i)) fprintf(log,"CONTROL_PREVIEW action=%s cursor=%d game_injected=0\n",names[i],cursor);
}
int swag_controls_selftest(FILE *log) {
    SwagControls s = {0}; int errors = 0;
    errors += swag_controls_update(&s,HidNpadButton_A) != JUMP;
    errors += swag_controls_update(&s,0) != 0;
    /* Exercise every press/release ordering of the three-button chord.
     * None may leak a grenade or item-cycle action on release. */
    static const unsigned orders[2][2] = {{0,1},{1,0}};
    const uint64_t parts[2] = {HidNpadButton_L,HidNpadButton_R};
    unsigned scenarios = 0;
    for (unsigned press=0; press<2; ++press) for (unsigned release=0; release<2; ++release) {
        SwagControls state = {0}; uint64_t held = 0;
        for (unsigned j=0; j<2; ++j) {
            held |= parts[orders[press][j]];
            errors += swag_controls_update(&state,held) != (j==1 ? CURSOR : 0);
        }
        errors += swag_controls_update(&state,held|HidNpadButton_A|HidNpadButton_ZR) != CONFIRM;
        swag_controls_update(&state,held);
        for (unsigned j=0; j<2; ++j) {
            held &= ~parts[orders[release][j]];
            errors += swag_controls_update(&state,held) != 0;
        }
        errors += !state.cursor;
        /* Repeat the same ordering while leaving cursor mode. */
        for (unsigned j=0; j<2; ++j) {
            held |= parts[orders[press][j]];
            errors += swag_controls_update(&state,held) != (j==1 ? CURSOR : 0);
        }
        for (unsigned j=0; j<2; ++j) {
            held &= ~parts[orders[release][j]];
            errors += swag_controls_update(&state,held) != 0;
        }
        errors += state.cursor; ++scenarios;
    }
    static const struct { uint64_t button; unsigned action; } single[] = {
        {HidNpadButton_A,JUMP},{HidNpadButton_B,CROUCH},{HidNpadButton_X,RELOAD},
        {HidNpadButton_Y,PRONE},{HidNpadButton_Plus,MAP},{HidNpadButton_Left,PRIMARY},
        {HidNpadButton_Right,PISTOL},{HidNpadButton_Up,KNIFE}};
    for (unsigned i=0; i<sizeof single/sizeof *single; ++i) {
        SwagControls state = {0};
        errors += swag_controls_update(&state,single[i].button) != single[i].action;
        errors += swag_controls_update(&state,single[i].button) != 0;
        errors += swag_controls_update(&state,0) != 0;
    }
    fprintf(log,"CONTROLS_MATRIX_DONE errors=%d chord_order_scenarios=%u single_buttons=8\n",errors,scenarios);
    errors += swag_controls_update(&s,HidNpadButton_L) != 0;
    errors += swag_controls_update(&s,0) != GRENADE;
    errors += swag_controls_update(&s,HidNpadButton_Down) != 0;
    errors += swag_controls_update(&s,0) != ITEM;
    uint64_t combo = HidNpadButton_L|HidNpadButton_R;
    swag_controls_update(&s,HidNpadButton_L);
    errors += swag_controls_update(&s,combo) != CURSOR || !s.cursor;
    errors += swag_controls_update(&s,combo) != 0;
    errors += swag_controls_update(&s,0) != 0;
    errors += swag_controls_update(&s,HidNpadButton_A|HidNpadButton_ZR) != CONFIRM;
    swag_controls_update(&s,0);
    errors += swag_controls_update(&s,combo) != CURSOR || s.cursor;
    errors += swag_controls_update(&s,0) != 0;
    /* Down first is also safe if both shoulders arrive before its release. */
    swag_controls_update(&s,HidNpadButton_Down);
    errors += swag_controls_update(&s,combo) != CURSOR;
    errors += swag_controls_update(&s,0) != 0;
    fprintf(log,"CONTROLS_TEST_DONE errors=%d chord_suppression=CHECKED cursor_confirm=CHECKED game_injected=0\n",errors);
    return errors ? -1 : 0;
}

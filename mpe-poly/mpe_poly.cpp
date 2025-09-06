#include "m_pd.h"
#include <vector>
#include <string>

#define MAX_VOICES 6

typedef struct {
    int note;
    int channel;
    int active;
    unsigned long age;
} VoiceSlot;

static t_class* mpe_poly_class;

typedef struct _mpe_poly {
    t_object  x_obj;
    t_outlet* voice_outs[MAX_VOICES];  // per-voice outlets
    t_outlet* global_out;              // outlet 7: current note number
    VoiceSlot voices[MAX_VOICES];
    unsigned long counter;             // for age stamping
} t_mpe_poly;

// utility: find active voice by note/channel
static int find_voice(t_mpe_poly* x, int note, int chan) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (x->voices[i].active &&
            x->voices[i].note == note &&
            x->voices[i].channel == chan) {
            return i;
        }
    }
    return -1;
}

// utility: find a free voice, or oldest if none free
static int alloc_voice(t_mpe_poly* x) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!x->voices[i].active) return i;
    }
    // steal oldest
    int oldest = 0;
    for (int i = 1; i < MAX_VOICES; i++) {
        if (x->voices[i].age < x->voices[oldest].age)
            oldest = i;
    }
    return oldest;
}

// update outlet 7: current note number (0 if none)
static void update_global(t_mpe_poly* x) {
    int current = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (x->voices[i].active) {
            current = x->voices[i].note;
            // use last active (highest age)
            for (int j = 0; j < MAX_VOICES; j++) {
                if (x->voices[j].active &&
                    x->voices[j].age > x->voices[i].age) {
                    current = x->voices[j].note;
                }
            }
            break;
        }
    }
    outlet_float(x->global_out, current);
}

// ===== Handlers =====

static void mpe_poly_noteon(t_mpe_poly* x, t_floatarg f_note, t_floatarg f_vel, t_floatarg f_chan) {
    int note = (int)f_note;
    int vel = (int)f_vel;
    int chan = (int)f_chan;

    int v = find_voice(x, note, chan);
    if (v < 0) v = alloc_voice(x);

    x->voices[v].note = note;
    x->voices[v].channel = chan;
    x->voices[v].active = (vel > 0);
    x->voices[v].age = ++x->counter;

    t_atom at[3];
    SETFLOAT(&at[0], note);
    SETFLOAT(&at[1], vel);
    SETFLOAT(&at[2], chan);
    outlet_anything(x->voice_outs[v], gensym("noteon"), 3, at);

    update_global(x);
}

static void mpe_poly_noteoff(t_mpe_poly* x, t_floatarg f_note, t_floatarg f_vel, t_floatarg f_chan) {
    int note = (int)f_note;
    int vel = (int)f_vel;
    int chan = (int)f_chan;

    int v = find_voice(x, note, chan);
    if (v >= 0) {
        x->voices[v].active = 0;

        t_atom at[3];
        SETFLOAT(&at[0], note);
        SETFLOAT(&at[1], vel);
        SETFLOAT(&at[2], chan);
        outlet_anything(x->voice_outs[v], gensym("noteoff"), 3, at);
    }

    update_global(x);
}

static void mpe_poly_pitchbend(t_mpe_poly* x, t_floatarg f_val, t_floatarg f_chan) {
    int chan = (int)f_chan;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (x->voices[v].active && x->voices[v].channel == chan) {
            t_atom at[2];
            SETFLOAT(&at[0], f_val);
            SETFLOAT(&at[1], chan);
            outlet_anything(x->voice_outs[v], gensym("pitchbend"), 2, at);
        }
    }
}

static void mpe_poly_cc(t_mpe_poly* x, t_floatarg f_cc, t_floatarg f_val, t_floatarg f_chan) {
    int chan = (int)f_chan;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (x->voices[v].active && x->voices[v].channel == chan) {
            t_atom at[3];
            SETFLOAT(&at[0], f_cc);
            SETFLOAT(&at[1], f_val);
            SETFLOAT(&at[2], chan);
            outlet_anything(x->voice_outs[v], gensym("cc"), 3, at);
        }
    }
}

static void mpe_poly_pressure(t_mpe_poly* x, t_floatarg f_val, t_floatarg f_chan) {
    int chan = (int)f_chan;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (x->voices[v].active && x->voices[v].channel == chan) {
            t_atom at[2];
            SETFLOAT(&at[0], f_val);
            SETFLOAT(&at[1], chan);
            outlet_anything(x->voice_outs[v], gensym("pressure"), 2, at);
        }
    }
}

// ===== Setup =====

static void* mpe_poly_new(void) {
    t_mpe_poly* x = (t_mpe_poly*)pd_new(mpe_poly_class);

    for (int i = 0; i < MAX_VOICES; i++) {
        x->voice_outs[i] = outlet_new(&x->x_obj, &s_list);
        x->voices[i].note = 0;
        x->voices[i].channel = 0;
        x->voices[i].active = 0;
        x->voices[i].age = 0;
    }
    x->global_out = outlet_new(&x->x_obj, &s_float);
    x->counter = 0;

    return (void*)x;
}

extern "C" void mpe_poly_setup(void) {
    mpe_poly_class = class_new(gensym("mpe_poly"),
        (t_newmethod)mpe_poly_new,
        0,
        sizeof(t_mpe_poly),
        CLASS_DEFAULT,
        A_DEFFLOAT, 0);

    class_addmethod(mpe_poly_class, (t_method)mpe_poly_noteon, gensym("noteon"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_noteoff, gensym("noteoff"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_pitchbend, gensym("pitchbend"), A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_cc, gensym("cc"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_pressure, gensym("pressure"), A_FLOAT, A_FLOAT, 0);
}

#include "m_pd.h"
#include <cmath>

static t_class* mpe_voice_class;

typedef struct _mpe_voice {
    t_object  x_obj;
    int       voice_num;     // Argument: which voice this is (1–6)
    t_outlet* out_note;      // 1. Note active
    t_outlet* out_pitch;     // 2. Pitch (Hz)
    t_outlet* out_glide;     // 3. Glide (-1..+1)
    t_outlet* out_slide;     // 4. Slide (-1..+1)
    t_outlet* out_press;     // 5. Press (0..1)
    t_outlet* out_strike;    // 6. Strike (0..1)

    // State
    int   note;        // MIDI note number
    int   velocity;    // Raw velocity
    float bend;        // Pitch bend (raw value, -8192..8191)
    float bend_range;  // Bend range in semitones
    float timbre;      // CC74 (0–127)
    float pressure;    // Aftertouch (0–127)
    int   active;      // 1 if note active
} t_mpe_voice;

static void mpe_voice_reset(t_mpe_voice* x) {
    x->note = 0;
    x->velocity = 0;
    x->bend = 0;
    x->timbre = 64;  // centre
    x->pressure = 0;
    x->active = 0;

    outlet_float(x->out_note, 0);
    outlet_float(x->out_pitch, 0);
    outlet_float(x->out_glide, 0);
    outlet_float(x->out_slide, 0);
    outlet_float(x->out_press, 0);
    outlet_float(x->out_strike, 0);
}

// Convert MIDI note + bend to Hz
static float mpe_voice_note_to_hz(int note, float bend, float bend_range) {
    float semitone_offset = (bend / 8192.0f) * bend_range;
    float n = (float)note + semitone_offset;
    return 440.0f * powf(2.0f, (n - 69.0f) / 12.0f);
}

// Handle noteon
static void mpe_voice_noteon(t_mpe_voice* x, t_floatarg note, t_floatarg vel, t_floatarg chan) {
    x->note = (int)note;
    x->velocity = (int)vel;
    x->active = (vel > 0);

    if (x->active) {
        outlet_float(x->out_note, 1);
        outlet_float(x->out_pitch, mpe_voice_note_to_hz(x->note, x->bend, x->bend_range));
        outlet_float(x->out_glide, x->bend / 8192.0f);   // -1..+1
        outlet_float(x->out_slide, (x->timbre / 63.5f) - 1.0f); // map 0–127 to -1..+1
        outlet_float(x->out_press, x->pressure / 127.0f);
        outlet_float(x->out_strike, x->velocity / 127.0f);
    }
    else {
        mpe_voice_reset(x);
    }
}

// Handle pitchbend
static void mpe_voice_pitchbend(t_mpe_voice* x, t_floatarg val, t_floatarg chan) {
    x->bend = val;
    if (x->active) {
        outlet_float(x->out_pitch, mpe_voice_note_to_hz(x->note, x->bend, x->bend_range));
        // Scale up the glide sensitivity
        float glide_normalized = ((x->bend - 8192.0f) / 8192.0f);
        float glide_scaled = glide_normalized * 50.0f;  // Scale by 50x
        // Clamp to -1..+1 range
        if (glide_scaled > 1.0f) glide_scaled = 1.0f;
        if (glide_scaled < -1.0f) glide_scaled = -1.0f;
        outlet_float(x->out_glide, glide_scaled);
    }
}

// Handle CC74 (timbre/slide)
static void mpe_voice_cc(t_mpe_voice* x, t_floatarg ccnum, t_floatarg val, t_floatarg chan) {
    if ((int)ccnum == 74) {
        x->timbre = val;
        if (x->active) {
            outlet_float(x->out_slide, (x->timbre / 63.5f) - 1.0f);
        }
    }
}

// Handle pressure
static void mpe_voice_pressure(t_mpe_voice* x, t_floatarg val, t_floatarg chan) {
    x->pressure = val;
    if (x->active) {
        outlet_float(x->out_press, x->pressure / 127.0f);
    }
}

static void* mpe_voice_new(t_symbol* s, int argc, t_atom* argv) {
    t_mpe_voice* x = (t_mpe_voice*)pd_new(mpe_voice_class);

    x->voice_num = (argc > 0 && argv[0].a_type == A_FLOAT) ? atom_getint(argv) : 1;
    x->bend_range = 2.0f; // default bend range (can be set later)

    x->out_note = outlet_new(&x->x_obj, &s_float);
    x->out_pitch = outlet_new(&x->x_obj, &s_float);
    x->out_glide = outlet_new(&x->x_obj, &s_float);
    x->out_slide = outlet_new(&x->x_obj, &s_float);
    x->out_press = outlet_new(&x->x_obj, &s_float);
    x->out_strike = outlet_new(&x->x_obj, &s_float);

    mpe_voice_reset(x);
    return (void*)x;
}

extern "C" void mpe_voice_setup(void) {
    mpe_voice_class = class_new(gensym("mpe_voice"),
        (t_newmethod)mpe_voice_new,
        0,
        sizeof(t_mpe_voice),
        CLASS_DEFAULT,
        A_GIMME, 0);

    class_addmethod(mpe_voice_class, (t_method)mpe_voice_noteon, gensym("noteon"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_voice_class, (t_method)mpe_voice_noteon, gensym("noteoff"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_voice_class, (t_method)mpe_voice_pitchbend, gensym("pitchbend"), A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_voice_class, (t_method)mpe_voice_cc, gensym("cc"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_voice_class, (t_method)mpe_voice_pressure, gensym("pressure"), A_FLOAT, A_FLOAT, 0);
}


// mpe-poly.cpp — Polyphonic MPE-aware Pure Data external
// Author: ChatGPT (with love for Pd & MPE)
// License: MIT
//
// Summary
//   • Polyphonic per-channel MPE handling with a fixed 16-entry voice table
//   • Lower/Upper zone presets (default Lower: master=1, notes=2..16)
//   • Methods: noteon, noteoff, pitchbend, timbre (CC74), pressure (ch. pressure), cc
//   • Controls: zone, bendrange, normalize, pbmode (normalized or semitones), debug, reset, flush
//   • Outlets: left = events (note, off), right = updates (pb, timbre, pressure, state, pb_global)
//   • Efficient: reuses atoms, clamps inputs, epsilon filtering for continuous updates
//
// Messages (all are Pd lists tagged with a symbol):
//   [note <note> <vel> <glide> <ch>(           // on note-on (vel>0)
//   [off <note> <relvel> <ch>(                 // on note-off
//   [pb <glide> <ch>(                          // per-note pitch bend; <glide> in [-1..1] or semitones
//   [pb_global <glide>(                        // master-channel pitch bend (zone-wide)
//   [timbre <timbre01> <ch>(                   // CC74 normalized if @normalize 1
//   [pressure <pressure01> <ch>(               // channel pressure normalized if @normalize 1
//   [state <note> <vel> <glide> <timbre> <pressure> <ch>(   // on bang or explicit request
//
// Build notes:
//   - Compile as a Pd external. The setup function name is mpe_poly_setup().
//   - On Windows, export mpe_poly_setup in your .def file.
//     Example .def snippet (if building a multi-external DLL):
//       LIBRARY   mpesimple
//       EXPORTS
//         mpesimple_setup
//         mpe_in_simple_setup
//         mpe_poly_setup
//
//   - Typical compile (Linux/macOS):
//       c++ -O3 -DPD -fPIC -shared -o mpe_poly.pd_linux mpe-poly.cpp
//     Adjust suffix for platform (.pd_darwin, .dll) and include path to m_pd.h.
//
// Usage in Pd:
//   [mpe_poly]
//   Optional after creation:
//     [zone lower( | [zone upper( | [zone 2 16(
//     [bendrange 48(
//     [normalize 1(
//     [pbmode 0(          // 0: normalized [-1..1], 1: semitones
//     [debug 1(
//     [flush(             // send offs for all active voices
//     [reset(             // clear state, no output
//   Send input messages as per methods below with channel numbers 1..16.

extern "C" {
#include "m_pd.h"
}

#include <math.h>
#include <string.h>

static t_class* mpe_poly_class = nullptr;

// Helpers
static inline float clampf(float v, float lo, float hi){ return (v < lo) ? lo : (v > hi ? hi : v); }
static inline float to01(float v){ return clampf(v * (1.0f/127.0f), 0.f, 1.f); }
static inline float from01(float v){ return clampf(v, 0.f, 1.f) * 127.f; }
static inline float bendNormFrom14(float v){ return clampf((v - 8192.f) * (1.0f/8191.0f), -1.f, 1.f); } // symmetrical
static inline float bendSemitones(float norm, float bendrange){ return norm * bendrange; }

struct Voice {
    bool  active;
    int   note;       // 0..127
    float vel;        // 0..1 or 0..127 depending on normalize
    float glide;      // normalized [-1..1] (internally always normalized)
    float timbre;     // 0..1 or 0..127
    float pressure;   // 0..1 or 0..127 (channel pressure)
    int   ch;         // 1..16
};

struct t_mpe_poly {
    t_object x_obj;
    t_outlet* events_out;
    t_outlet* updates_out;

    // Config
    int zone_start;       // inclusive note channels start
    int zone_end;         // inclusive note channels end
    int master_ch;        // zone master channel
    int normalize;        // 1 => scale 0..127 to 0..1 for vel/timbre/pressure
    float bendrange;      // semitones for pbmode=1
    int pbmode;           // 0 => normalized [-1..1], 1 => semitones
    int debug;            // log to Pd console

    Voice voices[16];     // index ch-1

    // Global master state
    float master_glide;   // normalized [-1..1]

    // Reused atoms for efficient output
    t_atom a[6];
};

// Forward decls
static void mpe_poly_output_note(t_mpe_poly* x, const Voice& v);
static void mpe_poly_output_off(t_mpe_poly* x, int ch, int note, float relvel);
static void mpe_poly_output_pb(t_mpe_poly* x, int ch, float norm);
static void mpe_poly_output_pb_global(t_mpe_poly* x, float norm);
static void mpe_poly_output_timbre(t_mpe_poly* x, int ch, float val);
static void mpe_poly_output_pressure(t_mpe_poly* x, int ch, float val);
static void mpe_poly_output_state(t_mpe_poly* x, const Voice& v);
static inline bool ch_in_zone(const t_mpe_poly* x, int ch){ return ch >= x->zone_start && ch <= x->zone_end; }
static void mpe_poly_reset(t_mpe_poly* x);
static void mpe_poly_flush(t_mpe_poly* x);

// Utils to convert internal normalized glide to outgoing based on pbmode
static inline float glide_out(const t_mpe_poly* x, float norm){ return (x->pbmode == 0) ? norm : bendSemitones(norm, x->bendrange); }

// ============ Output helpers ============
static void mpe_poly_output_note(t_mpe_poly* x, const Voice& v){
    // [note <note> <vel> <glide> <ch>(
    SETSYMBOL(&x->a[0], gensym("note"));
    SETFLOAT (&x->a[1], (t_float)v.note);
    SETFLOAT (&x->a[2], (t_float)v.vel);
    SETFLOAT (&x->a[3], (t_float)glide_out(x, v.glide));
    SETFLOAT (&x->a[4], (t_float)v.ch);
    outlet_list(x->events_out, &s_list, 5, x->a);
}

static void mpe_poly_output_off(t_mpe_poly* x, int ch, int note, float relvel){
    // [off <note> <relvel> <ch>(
    SETSYMBOL(&x->a[0], gensym("off"));
    SETFLOAT (&x->a[1], (t_float)note);
    SETFLOAT (&x->a[2], (t_float)relvel);
    SETFLOAT (&x->a[3], (t_float)ch);
    outlet_list(x->events_out, &s_list, 4, x->a);
}

static void mpe_poly_output_pb(t_mpe_poly* x, int ch, float norm){
    // [pb <glide> <ch>(
    SETSYMBOL(&x->a[0], gensym("pb"));
    SETFLOAT (&x->a[1], (t_float)glide_out(x, norm));
    SETFLOAT (&x->a[2], (t_float)ch);
    outlet_list(x->updates_out, &s_list, 3, x->a);
}

static void mpe_poly_output_pb_global(t_mpe_poly* x, float norm){
    // [pb_global <glide>(
    SETSYMBOL(&x->a[0], gensym("pb_global"));
    SETFLOAT (&x->a[1], (t_float)glide_out(x, norm));
    outlet_list(x->updates_out, &s_list, 2, x->a);
}

static void mpe_poly_output_timbre(t_mpe_poly* x, int ch, float val){
    // [timbre <val> <ch>(
    SETSYMBOL(&x->a[0], gensym("timbre"));
    SETFLOAT (&x->a[1], (t_float)val);
    SETFLOAT (&x->a[2], (t_float)ch);
    outlet_list(x->updates_out, &s_list, 3, x->a);
}

static void mpe_poly_output_pressure(t_mpe_poly* x, int ch, float val){
    // [pressure <val> <ch>(
    SETSYMBOL(&x->a[0], gensym("pressure"));
    SETFLOAT (&x->a[1], (t_float)val);
    SETFLOAT (&x->a[2], (t_float)ch);
    outlet_list(x->updates_out, &s_list, 3, x->a);
}

static void mpe_poly_output_state(t_mpe_poly* x, const Voice& v){
    // [state <note> <vel> <glide> <timbre> <pressure> <ch>(
    SETSYMBOL(&x->a[0], gensym("state"));
    SETFLOAT (&x->a[1], (t_float)v.note);
    SETFLOAT (&x->a[2], (t_float)v.vel);
    SETFLOAT (&x->a[3], (t_float)glide_out(x, v.glide));
    SETFLOAT (&x->a[4], (t_float)v.timbre);
    SETFLOAT (&x->a[5], (t_float)v.pressure);
    outlet_list(x->updates_out, &s_list, 6, x->a);
}

// ============ Methods ============
static void mpe_poly_noteon(t_mpe_poly* x, t_floatarg note_f, t_floatarg vel_f, t_floatarg ch_f){
    int ch = (int)ch_f;
    int note = (int)note_f;
    float vel_in = (float)vel_f;

    if (vel_in <= 0.f){
        // Treat note-on with 0 velocity as note-off
        // Call noteoff directly
        // (Note: avoid recursion with same signature)
        int ch_i = ch;
        int note_i = note;
        float rel = 0.f;
        // Forward to noteoff:
        // (we use the function body inline to avoid needing Pd atom parsing)
        if (!((ch_i >= x->zone_start) && (ch_i <= x->zone_end))) { if (x->debug) post("mpe_poly: noteoff ignored, ch %d outside zone", ch_i); return; }
        Voice& v2 = x->voices[ch_i-1];
        if (!v2.active){ if (x->debug) post("mpe_poly: noteoff on inactive ch %d", ch_i); return; }
        float relvel = x->normalize ? to01(rel) : clampf(rel, 0.f, 127.f);
        mpe_poly_output_off(x, ch_i, note_i, relvel);
        v2.active = false;
        return;
    }
    if (!ch_in_zone(x, ch)) { if (x->debug) post("mpe_poly: noteon ignored, ch %d outside zone", ch); return; }

    Voice& v = x->voices[ch-1];
    v.active = true;
    v.ch = ch;
    v.note = note;
    v.vel = x->normalize ? to01(vel_in) : clampf(vel_in, 0.f, 127.f);
    // keep existing v.glide (may come from earlier pb), leave timbre/pressure

    mpe_poly_output_note(x, v);
    if (x->debug) post("mpe_poly: note ch %d note %d vel %.3f", ch, note, v.vel);
}

static void mpe_poly_noteoff(t_mpe_poly* x, t_floatarg note_f, t_floatarg relvel_f, t_floatarg ch_f){
    int ch = (int)ch_f;
    int note = (int)note_f;
    float relvel_in = (float)relvel_f;
    if (!ch_in_zone(x, ch)) { if (x->debug) post("mpe_poly: noteoff ignored, ch %d outside zone", ch); return; }

    Voice& v = x->voices[ch-1];
    if (!v.active){ if (x->debug) post("mpe_poly: noteoff on inactive ch %d", ch); return; }

    float relvel = x->normalize ? to01(relvel_in) : clampf(relvel_in, 0.f, 127.f);
    mpe_poly_output_off(x, ch, note, relvel);

    // clear voice
    v.active = false;
}

static void mpe_poly_pitchbend(t_mpe_poly* x, t_floatarg value_f, t_floatarg ch_f){
    int ch = (int)ch_f;
    float v14 = clampf((float)value_f, 0.f, 16383.f);
    float norm = bendNormFrom14(v14);

    if (ch == x->master_ch){
        if (fabsf(norm - x->master_glide) < 1e-5f) return;
        x->master_glide = norm;
        mpe_poly_output_pb_global(x, norm);
        if (x->debug) post("mpe_poly: pb_global %.3f", glide_out(x, norm));
        return;
    }

    if (!ch_in_zone(x, ch)) { if (x->debug) post("mpe_poly: pb ignored, ch %d outside zone", ch); return; }

    Voice& v = x->voices[ch-1];
    if (!v.active) { v.ch = ch; } // allow glide pre-note for this channel

    if (fabsf(norm - v.glide) < 1e-5f) return; // epsilon filter
    v.glide = norm;
    mpe_poly_output_pb(x, ch, norm);
    if (x->debug) post("mpe_poly: pb ch %d %.3f", ch, glide_out(x, norm));
}

static void mpe_poly_cc(t_mpe_poly* x, t_floatarg cc_f, t_floatarg val_f, t_floatarg ch_f){
    int ch = (int)ch_f;
    int cc = (int)cc_f;
    float val_in = (float)val_f;
    if (ch == x->master_ch){ /* ignore or handle master CCs if desired */ return; }
    if (!ch_in_zone(x, ch)) return;

    Voice& v = x->voices[ch-1];
    if (cc == 74){
        float val = x->normalize ? to01(val_in) : clampf(val_in, 0.f, 127.f);
        if (fabsf(val - v.timbre) < 1e-5f) return;
        v.timbre = val;
        mpe_poly_output_timbre(x, ch, val);
        if (x->debug) post("mpe_poly: timbre ch %d %.3f", ch, val);
    } else {
        // Optional: forward other CCs
        // SETSYMBOL(&x->a[0], gensym("cc")); SETFLOAT(&x->a[1], (t_float)cc); SETFLOAT(&x->a[2], x->normalize? to01(val_in) : clampf(val_in,0.f,127.f)); SETFLOAT(&x->a[3], (t_float)ch); outlet_list(x->updates_out, &s_list, 4, x->a);
    }
}

static void mpe_poly_pressure(t_mpe_poly* x, t_floatarg val_f, t_floatarg ch_f){
    int ch = (int)ch_f;
    float val_in = (float)val_f;
    if (ch == x->master_ch){ /* could emit pressure_global if desired */ return; }
    if (!ch_in_zone(x, ch)) return;

    Voice& v = x->voices[ch-1];
    float val = x->normalize ? to01(val_in) : clampf(val_in, 0.f, 127.f);
    if (fabsf(val - v.pressure) < 1e-5f) return;
    v.pressure = val;
    mpe_poly_output_pressure(x, ch, val);
    if (x->debug) post("mpe_poly: pressure ch %d %.3f", ch, val);
}

static void mpe_poly_bang(t_mpe_poly* x){
    for (int ch = x->zone_start; ch <= x->zone_end; ++ch){
        const Voice& v = x->voices[ch-1];
        if (v.active) mpe_poly_output_state(x, v);
    }
}

// Control methods
static void mpe_poly_zone(t_mpe_poly* x, t_symbol* s, int argc, t_atom* argv){
    (void)s; // unused
    if (argc == 1 && argv[0].a_type == A_SYMBOL){
        t_symbol* mode = argv[0].a_w.w_symbol;
        if (mode == gensym("lower")){
            x->zone_start = 2; x->zone_end = 16; x->master_ch = 1;
        } else if (mode == gensym("upper")){
            x->zone_start = 1; x->zone_end = 15; x->master_ch = 16;
        }
    } else if (argc >= 2 && argv[0].a_type == A_FLOAT && argv[1].a_type == A_FLOAT){
        int srt = (int)atom_getfloat(argv+0);
        int end = (int)atom_getfloat(argv+1);
        srt = srt < 1 ? 1 : (srt > 16 ? 16 : srt);
        end = end < 1 ? 1 : (end > 16 ? 16 : end);
        if (srt > end){ int tmp = srt; srt = end; end = tmp; }
        x->zone_start = srt; x->zone_end = end;
        // choose a master outside range when possible; default to 1 or 16 otherwise
        if (srt > 1) x->master_ch = 1;
        else if (end < 16) x->master_ch = 16;
        else x->master_ch = 1; // fallback
    }
    if (x->debug) post("mpe_poly: zone %d..%d master %d", x->zone_start, x->zone_end, x->master_ch);
}

static void mpe_poly_bendrange(t_mpe_poly* x, t_floatarg st){ x->bendrange = (float)st; if (x->debug) post("mpe_poly: bendrange %.1f", x->bendrange); }
static void mpe_poly_normalize(t_mpe_poly* x, t_floatarg on){ x->normalize = (on != 0.f); if (x->debug) post("mpe_poly: normalize %d", x->normalize); }
static void mpe_poly_pbmode(t_mpe_poly* x, t_floatarg mode){ x->pbmode = (mode != 0.f) ? 1 : 0; if (x->debug) post("mpe_poly: pbmode %s", x->pbmode?"semitones":"normalized"); }
static void mpe_poly_debug(t_mpe_poly* x, t_floatarg on){ x->debug = (on != 0.f); post("mpe_poly: debug %d", x->debug); }

static void mpe_poly_reset(t_mpe_poly* x){
    for (int i=0;i<16;++i){ x->voices[i] = Voice{false, 0, x->normalize?0.f:0.f, 0.f, x->normalize?0.f:0.f, x->normalize?0.f:0.f, i+1}; }
    x->master_glide = 0.f;
}

static void mpe_poly_flush(t_mpe_poly* x){
    for (int ch=x->zone_start; ch<=x->zone_end; ++ch){
        Voice& v = x->voices[ch-1];
        if (v.active){
            mpe_poly_output_off(x, ch, v.note, x->normalize?0.f:0.f);
            v.active = false;
        }
    }
}

// ============ Constructor / Destructor ============
static void* mpe_poly_new(t_symbol* s, int argc, t_atom* argv){
    (void)s; // unused
    t_mpe_poly* x = (t_mpe_poly*)pd_new(mpe_poly_class);

    x->events_out  = outlet_new(&x->x_obj, &s_list);
    x->updates_out = outlet_new(&x->x_obj, &s_list);

    // defaults
    x->zone_start = 2; x->zone_end = 16; x->master_ch = 1;
    x->normalize = 1;
    x->bendrange = 48.f;
    x->pbmode = 0;
    x->debug = 0;
    x->master_glide = 0.f;

    // optional creation args: [mpe_poly lower], [mpe_poly upper], [mpe_poly <start> <end>]
    if (argc >= 1 && argv[0].a_type == A_SYMBOL){
        t_symbol* mode = argv[0].a_w.w_symbol;
        if (mode == gensym("lower")) { x->zone_start=2; x->zone_end=16; x->master_ch=1; }
        else if (mode == gensym("upper")) { x->zone_start=1; x->zone_end=15; x->master_ch=16; }
    } else if (argc >= 2 && argv[0].a_type == A_FLOAT && argv[1].a_type == A_FLOAT){
        int srt = (int)atom_getfloat(argv+0);
        int end = (int)atom_getfloat(argv+1);
        srt = srt < 1 ? 1 : (srt > 16 ? 16 : srt);
        end = end < 1 ? 1 : (end > 16 ? 16 : end);
        if (srt > end){ int tmp = srt; srt = end; end = tmp; }
        x->zone_start = srt; x->zone_end = end; x->master_ch = (srt>1)?1:16;
    }

    mpe_poly_reset(x);
    return (void*)x;
}

static void mpe_poly_free(t_mpe_poly* x){ (void)x; }

extern "C" void mpe_poly_setup(void){
    mpe_poly_class = class_new(gensym("mpe_poly"),
                               (t_newmethod)mpe_poly_new,
                               (t_method)mpe_poly_free,
                               sizeof(t_mpe_poly),
                               CLASS_DEFAULT,
                               A_GIMME, 0);

    // Input methods
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_noteon,     gensym("noteon"),   A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_noteoff,    gensym("noteoff"),  A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_pitchbend,  gensym("pitchbend"),A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_cc,         gensym("cc"),       A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_pressure,   gensym("pressure"), A_FLOAT, A_FLOAT, 0);

    // Controls
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_zone,       gensym("zone"),     A_GIMME, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_bendrange,  gensym("bendrange"),A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_normalize,  gensym("normalize"),A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_pbmode,     gensym("pbmode"),   A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_debug,      gensym("debug"),    A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_flush,      gensym("flush"),     A_FLOAT, 0);
    class_addmethod(mpe_poly_class, (t_method)mpe_poly_reset,      gensym("reset"),     A_FLOAT, 0);

    // Bang -> dump active state
    class_addbang(mpe_poly_class, (t_method)mpe_poly_bang);
}

#include "m_pd.h"

// Class definition for mpe-voice object
static t_class *mpe_voice_class;

typedef struct _mpe_voice {
    t_object x_obj;
    t_outlet *note_outlet;      // Outlet 1: Note number
    t_outlet *strike_outlet;    // Outlet 2: Strike (0-1)
    t_outlet *press_outlet;     // Outlet 3: Press (0-1)
    t_outlet *glide_outlet;     // Outlet 4: Glide (-1 to +1)
    t_outlet *slide_outlet;     // Outlet 5: Slide (0-1)
    t_outlet *lift_outlet;      // Outlet 6: Lift (0-1)
    int voiceNumber;            // Which voice this object handles
} t_mpe_voice;

// Constructor
void *mpe_voice_new(t_floatarg voiceNum) {
    t_mpe_voice *x = (t_mpe_voice *)pd_new(mpe_voice_class);
    
    // Store voice number
    x->voiceNumber = (int)voiceNum;
    
    // Create outlets (in reverse order for Pure Data)
    x->lift_outlet = outlet_new(&x->x_obj, &s_float);      // Outlet 6
    x->slide_outlet = outlet_new(&x->x_obj, &s_float);     // Outlet 5
    x->glide_outlet = outlet_new(&x->x_obj, &s_float);     // Outlet 4
    x->press_outlet = outlet_new(&x->x_obj, &s_float);     // Outlet 3
    x->strike_outlet = outlet_new(&x->x_obj, &s_float);    // Outlet 2
    x->note_outlet = outlet_new(&x->x_obj, &s_float);      // Outlet 1
    
    if (x->voiceNumber < 1 || x->voiceNumber > 6) {
        post("mpe-voice: Warning - voice number %d outside normal range 1-6", x->voiceNumber);
    } else {
        post("mpe-voice: Voice %d initialized", x->voiceNumber);
    }
    
    return (void *)x;
}

// Handle list input from mpe-in
void mpe_voice_list(t_mpe_voice *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc != 6) {
        post("mpe-voice %d: Expected 6 values, got %d", x->voiceNumber, argc);
        return;
    }
    
    // Extract values from the list
    float note = atom_getfloat(&argv[0]);
    float strike = atom_getfloat(&argv[1]);
    float press = atom_getfloat(&argv[2]);
    float glide = atom_getfloat(&argv[3]);
    float slide = atom_getfloat(&argv[4]);
    float lift = atom_getfloat(&argv[5]);
    
    // Validate ranges (optional - could remove for performance)
    if (strike < 0.0f || strike > 1.0f) {
        post("mpe-voice %d: Strike value %f outside expected range 0-1", x->voiceNumber, strike);
    }
    if (press < 0.0f || press > 1.0f) {
        post("mpe-voice %d: Press value %f outside expected range 0-1", x->voiceNumber, press);
    }
    if (glide < -1.0f || glide > 1.0f) {
        post("mpe-voice %d: Glide value %f outside expected range -1 to +1", x->voiceNumber, glide);
    }
    if (slide < 0.0f || slide > 1.0f) {
        post("mpe-voice %d: Slide value %f outside expected range 0-1", x->voiceNumber, slide);
    }
    if (lift < 0.0f || lift > 1.0f) {
        post("mpe-voice %d: Lift value %f outside expected range 0-1", x->voiceNumber, lift);
    }
    
    // Output values to outlets (right to left in Pure Data)
    outlet_float(x->lift_outlet, lift);
    outlet_float(x->slide_outlet, slide);
    outlet_float(x->glide_outlet, glide);
    outlet_float(x->press_outlet, press);
    outlet_float(x->strike_outlet, strike);
    outlet_float(x->note_outlet, note);
    
    // Debug output (can be removed in final version)
    if (note > 0) {
        post("mpe-voice %d: Note %.0f - Strike:%.3f Press:%.3f Glide:%.3f Slide:%.3f Lift:%.3f", 
             x->voiceNumber, note, strike, press, glide, slide, lift);
    } else {
        post("mpe-voice %d: Voice off - Lift:%.3f", x->voiceNumber, lift);
    }
}

// Handle bang input (outputs current state)
void mpe_voice_bang(t_mpe_voice *x) {
    post("mpe-voice %d: Ready for input", x->voiceNumber);
}

// Setup function
void mpe_voice_setup(void) {
    mpe_voice_class = class_new(
        gensym("mpe-voice"),
        (t_newmethod)mpe_voice_new,
        0,  // No destructor needed
        sizeof(t_mpe_voice),
        CLASS_DEFAULT,
        A_DEFFLOAT,  // Voice number argument
        (t_atomtype)0
    );
    
    // Add methods
    class_addmethod(mpe_voice_class, (t_method)mpe_voice_list, gensym("list"), A_GIMME, (t_atomtype)0);
    class_addmethod(mpe_voice_class, (t_method)mpe_voice_bang, gensym("bang"), (t_atomtype)0);
}

// Entry point for Pure Data
void mpe_voice_setup(void);
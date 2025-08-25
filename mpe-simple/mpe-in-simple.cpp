#include "m_pd.h"

// Simple structure - no STL containers
typedef struct _mpe_in_simple {
    t_object x_obj;
    t_outlet *voice_outlet;  // Just one voice outlet for now
    int currentNote;         // Currently playing note (0 = off)
    float currentVelocity;   // Current velocity
} t_mpe_in_simple;

static t_class *mpe_in_simple_class;

// Constructor
void *mpe_in_simple_new(void) {
    t_mpe_in_simple *x = (t_mpe_in_simple *)pd_new(mpe_in_simple_class);
    
    // Create one outlet
    x->voice_outlet = outlet_new(&x->x_obj, &s_list);
    
    // Initialize state
    x->currentNote = 0;
    x->currentVelocity = 0.0f;
    
    post("mpe-in-simple: Simple MPE processor initialized");
    
    return (void *)x;
}

// Handle noteon message
void mpe_in_simple_noteon(t_mpe_in_simple *x, t_floatarg note, t_floatarg velocity, t_floatarg channel) {
    post("mpe-in-simple: noteon %.0f %.0f %.0f", note, velocity, channel);
    
    if (velocity > 0) {
        // Note on
        x->currentNote = (int)note;
        x->currentVelocity = velocity / 127.0f;  // Scale to 0-1
        
        // Send simple output: note strike
        t_atom output[2];
        SETFLOAT(&output[0], x->currentNote);
        SETFLOAT(&output[1], x->currentVelocity);
        
        outlet_list(x->voice_outlet, &s_list, 2, output);
        post("mpe-in-simple: Note %.0f on, velocity %.3f", note, x->currentVelocity);
    } else {
        // Note off (velocity 0)
        mpe_in_simple_noteoff(x, note, velocity, channel);
    }
}

// Handle noteoff message
void mpe_in_simple_noteoff(t_mpe_in_simple *x, t_floatarg note, t_floatarg velocity, t_floatarg channel) {
    post("mpe-in-simple: noteoff %.0f %.0f %.0f", note, velocity, channel);
    
    // Turn off note
    x->currentNote = 0;
    x->currentVelocity = 0.0f;
    
    // Send note off: 0 0
    t_atom output[2];
    SETFLOAT(&output[0], 0);  // Note 0 means off
    SETFLOAT(&output[1], velocity / 127.0f);  // Release velocity
    
    outlet_list(x->voice_outlet, &s_list, 2, output);
    post("mpe-in-simple: Note off");
}

// Handle bang (for testing)
void mpe_in_simple_bang(t_mpe_in_simple *x) {
    post("mpe-in-simple: bang received - current note %.0f", (float)x->currentNote);
}

// Setup function
extern "C" void mpe_in_simple_setup(void) {
    mpe_in_simple_class = class_new(
        gensym("mpe-in-simple"),
        (t_newmethod)mpe_in_simple_new,
        0,
        sizeof(t_mpe_in_simple),
        CLASS_DEFAULT,
        (t_atomtype)0
    );
    
    // Add methods
    class_addmethod(mpe_in_simple_class, (t_method)mpe_in_simple_noteon, gensym("noteon"), A_FLOAT, A_FLOAT, A_FLOAT, (t_atomtype)0);
    class_addmethod(mpe_in_simple_class, (t_method)mpe_in_simple_noteoff, gensym("noteoff"), A_FLOAT, A_FLOAT, A_FLOAT, (t_atomtype)0);
    class_addmethod(mpe_in_simple_class, (t_method)mpe_in_simple_bang, gensym("bang"), (t_atomtype)0);
    
    post("mpe-in-simple: Object class registered");
}
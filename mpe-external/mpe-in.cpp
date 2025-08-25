#include "m_pd.h"
#include <map>
#include <vector>

// Structure to hold note information
struct MpeNote {
    int note;
    int channel;
    int velocity;
    bool isActive;
    float strike;
    float press;
    float glide;
    float slide;
    float lift;
    unsigned long startTime;
};

// Class definition for mpe-in object
static t_class *mpe_in_class;

typedef struct _mpe_in {
    t_object x_obj;
    t_outlet *voice_outlets[6];  // 6 voice outlets
    std::map<int, MpeNote> activeNotes;  // Map: channel -> note info
    std::vector<int> voiceChannels;      // Which channel each voice is using (0 = free)
    int maxVoices;
    bool legatoMode;
} t_mpe_in;

// Constructor
void *mpe_in_new(void) {
    t_mpe_in *x = (t_mpe_in *)pd_new(mpe_in_class);
    
    // Create 6 voice outlets
    for (int i = 0; i < 6; i++) {
        x->voice_outlets[i] = outlet_new(&x->x_obj, &s_list);
    }
    
    // Initialize voice tracking
    x->voiceChannels.resize(6, 0);  // All voices start free (channel 0)
    x->maxVoices = 6;
    x->legatoMode = true;
    
    post("mpe-in: MPE input processor initialized with %d voices", x->maxVoices);
    
    return (void *)x;
}

// Find free voice slot
int findFreeVoice(t_mpe_in *x) {
    for (int i = 0; i < x->maxVoices; i++) {
        if (x->voiceChannels[i] == 0) {
            return i;
        }
    }
    return -1;  // No free voices
}

// Find voice by channel
int findVoiceByChannel(t_mpe_in *x, int channel) {
    for (int i = 0; i < x->maxVoices; i++) {
        if (x->voiceChannels[i] == channel) {
            return i;
        }
    }
    return -1;  // Channel not found
}

// Forward declaration
void mpe_in_noteoff(t_mpe_in *x, t_floatarg note, t_floatarg velocity, t_floatarg channel);

// Handle note on
void mpe_in_noteon(t_mpe_in *x, t_floatarg note, t_floatarg velocity, t_floatarg channel) {
    if (velocity <= 0) {
        // Treat zero velocity as note off
        mpe_in_noteoff(x, note, 0, channel);
        return;
    }
    
    int ch = (int)channel;
    int voiceIndex = findFreeVoice(x);
    
    if (voiceIndex == -1) {
        post("mpe-in: No free voices available");
        return;
    }
    
    // Assign voice to this channel
    x->voiceChannels[voiceIndex] = ch;
    
    // Create note info
    MpeNote noteInfo;
    noteInfo.note = (int)note;
    noteInfo.channel = ch;
    noteInfo.velocity = (int)velocity;
    noteInfo.isActive = true;
    noteInfo.strike = velocity / 127.0f;  // Scale to 0-1
    noteInfo.press = 0.0f;  // Will be updated by CC messages
    noteInfo.glide = 0.0f;  // Will be updated by pitch bend
    noteInfo.slide = 0.0f;  // Will be updated by CC messages
    noteInfo.lift = 0.0f;   // Will be updated during release
    noteInfo.startTime = clock_getlogicaltime();
    
    // Store in active notes map
    x->activeNotes[ch] = noteInfo;
    
    // Send to appropriate voice outlet
    // Format: note strike press glide slide lift
    t_atom voiceData[6];
    SETFLOAT(&voiceData[0], noteInfo.note);
    SETFLOAT(&voiceData[1], noteInfo.strike);
    SETFLOAT(&voiceData[2], noteInfo.press);
    SETFLOAT(&voiceData[3], noteInfo.glide);
    SETFLOAT(&voiceData[4], noteInfo.slide);
    SETFLOAT(&voiceData[5], noteInfo.lift);
    
    outlet_list(x->voice_outlets[voiceIndex], &s_list, 6, voiceData);
    
    post("mpe-in: Note %d on channel %d assigned to voice %d", (int)note, ch, voiceIndex + 1);
}

// Handle note off
void mpe_in_noteoff(t_mpe_in *x, t_floatarg note, t_floatarg velocity, t_floatarg channel) {
    int ch = (int)channel;
    int voiceIndex = findVoiceByChannel(x, ch);
    
    if (voiceIndex == -1) {
        post("mpe-in: Note off for unknown channel %d", ch);
        return;
    }
    
    // Update lift value
    if (x->activeNotes.find(ch) != x->activeNotes.end()) {
        x->activeNotes[ch].lift = velocity / 127.0f;
        x->activeNotes[ch].isActive = false;
        
        // Send final update with note = 0 to indicate voice is free
        t_atom voiceData[6];
        SETFLOAT(&voiceData[0], 0);  // Note = 0 means voice is off
        SETFLOAT(&voiceData[1], x->activeNotes[ch].strike);
        SETFLOAT(&voiceData[2], x->activeNotes[ch].press);
        SETFLOAT(&voiceData[3], x->activeNotes[ch].glide);
        SETFLOAT(&voiceData[4], x->activeNotes[ch].slide);
        SETFLOAT(&voiceData[5], x->activeNotes[ch].lift);
        
        outlet_list(x->voice_outlets[voiceIndex], &s_list, 6, voiceData);
        
        // Free the voice
        x->voiceChannels[voiceIndex] = 0;
        x->activeNotes.erase(ch);
        
        post("mpe-in: Note %d off, voice %d freed", (int)note, voiceIndex + 1);
    }
}

// Handle pitch bend (glide)
void mpe_in_pitchbend(t_mpe_in *x, t_floatarg value, t_floatarg channel) {
    int ch = (int)channel;
    int voiceIndex = findVoiceByChannel(x, ch);
    
    if (voiceIndex == -1) return;  // No active note on this channel
    
    if (x->activeNotes.find(ch) != x->activeNotes.end()) {
        // Scale pitch bend from 0-16383 to -1.0 to +1.0
        x->activeNotes[ch].glide = (value - 8192.0f) / 8192.0f;
        
        // Send updated voice data
        MpeNote &note = x->activeNotes[ch];
        t_atom voiceData[6];
        SETFLOAT(&voiceData[0], note.note);
        SETFLOAT(&voiceData[1], note.strike);
        SETFLOAT(&voiceData[2], note.press);
        SETFLOAT(&voiceData[3], note.glide);
        SETFLOAT(&voiceData[4], note.slide);
        SETFLOAT(&voiceData[5], note.lift);
        
        outlet_list(x->voice_outlets[voiceIndex], &s_list, 6, voiceData);
    }
}

// Handle control change (press/slide)
void mpe_in_controlchange(t_mpe_in *x, t_floatarg controller, t_floatarg value, t_floatarg channel) {
    int ch = (int)channel;
    int voiceIndex = findVoiceByChannel(x, ch);
    
    if (voiceIndex == -1) return;  // No active note on this channel
    
    if (x->activeNotes.find(ch) != x->activeNotes.end()) {
        MpeNote &note = x->activeNotes[ch];
        
        // Map common MPE CCs
        if (controller == 74) {
            // CC74 is typically slide (Y-axis movement)
            note.slide = value / 127.0f;
        } else if (controller == 1) {
            // CC1 is typically press (aftertouch alternative)
            note.press = value / 127.0f;
        }
        
        // Send updated voice data
        t_atom voiceData[6];
        SETFLOAT(&voiceData[0], note.note);
        SETFLOAT(&voiceData[1], note.strike);
        SETFLOAT(&voiceData[2], note.press);
        SETFLOAT(&voiceData[3], note.glide);
        SETFLOAT(&voiceData[4], note.slide);
        SETFLOAT(&voiceData[5], note.lift);
        
        outlet_list(x->voice_outlets[voiceIndex], &s_list, 6, voiceData);
    }
}

// Setup function
void mpe_in_setup(void) {
    mpe_in_class = class_new(
        gensym("mpe-in"),
        (t_newmethod)mpe_in_new,
        0,  // No destructor needed for now
        sizeof(t_mpe_in),
        CLASS_DEFAULT,
        (t_atomtype)0
    );
    
    // Add MIDI input methods
    class_addmethod(mpe_in_class, (t_method)mpe_in_noteon, gensym("noteon"), A_FLOAT, A_FLOAT, A_FLOAT, (t_atomtype)0);
    class_addmethod(mpe_in_class, (t_method)mpe_in_noteoff, gensym("noteoff"), A_FLOAT, A_FLOAT, A_FLOAT, (t_atomtype)0);
    class_addmethod(mpe_in_class, (t_method)mpe_in_pitchbend, gensym("pitchbend"), A_FLOAT, A_FLOAT, (t_atomtype)0);
    class_addmethod(mpe_in_class, (t_method)mpe_in_controlchange, gensym("controlchange"), A_FLOAT, A_FLOAT, A_FLOAT, (t_atomtype)0);
}

// Entry point for Pure Data
void mpe_in_setup(void);
#include "m_pd.h"

extern "C" {
    // External setup function declarations
    void mpe_in_setup(void);
    void mpe_voice_setup(void);
    
    // Main library setup function
    void mpe_setup(void) {
        post("MPE External Library v1.0");
        post("Objects: [mpe-in] [mpe-voice]");
        post("For ROLI Seaboard and other MPE controllers");
        
        // Initialize both objects
        mpe_in_setup();
        mpe_voice_setup();
        
        post("MPE library loaded successfully");
    }
}
#include "m_pd.h"

extern "C" {
    // External setup function declaration
    void mpe_in_simple_setup(void);
    
    // Main library setup function - must match library name exactly
    void mpesimple_setup(void) {
        post("MPE Simple External v1.0");
        post("Objects: [mpe-in-simple]");
        
        // Initialize the object
        mpe_in_simple_setup();
        
        post("MPE Simple library loaded successfully");
    }
}
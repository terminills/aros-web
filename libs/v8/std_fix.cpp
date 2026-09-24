/*
 * std_fix.cpp - The Linker Hacks (Final Corrected)
 * 1. Provides missing std::basic_streambuf::seekpos symbol (Mangled)
 * 2. Provides missing TLS initialization symbols (Mangled)
 * 3. Provides missing libstdc++ ios_base_library_init ABI hook
 */

#include <iostream>
#include <streambuf>
#include <ios>

extern "C" {

    /* ---------------------------------------------------------------------
       FIX 1: std::basic_streambuf::seekpos
       --------------------------------------------------------------------- */
    
    // Forward Declaration
    std::streampos _ZNSt15basic_streambufIcSt11char_traitsIcEE7seekposESt4fposI11__mbstate_tESt13_Ios_Openmode(
        std::basic_streambuf<char>*, 
        std::streampos, 
        std::ios_base::openmode
    ) __attribute__((weak));

    // Implementation
    std::streampos _ZNSt15basic_streambufIcSt11char_traitsIcEE7seekposESt4fposI11__mbstate_tESt13_Ios_Openmode(
        std::basic_streambuf<char>* /* this_ptr */, 
        std::streampos /* sp */, 
        std::ios_base::openmode /* which */
    ) {
        return std::streampos(std::streamoff(-1));
    }

    /* ---------------------------------------------------------------------
       FIX 2: TLS Initialization Weak Symbols
       --------------------------------------------------------------------- */

    // v8::internal::g_current_isolate_ (Length 18)
    void _ZTHN2v88internal18g_current_isolate_E(void) { }

    // v8::internal::RwxMemoryWriteScope::code_space_write_nesting_level_
    // CORRECTION: RwxMemoryWriteScope is 19 chars (not 21).
    // CORRECTION: code_space_write_nesting_level_ is 31 chars.
    void _ZTHN2v88internal19RwxMemoryWriteScope31code_space_write_nesting_level_E(void) { }

    // absl::cord_internal::cordz_next_sample (Length 17)
    void _ZTHN4absl13cord_internal17cordz_next_sampleE(void) { }

    /*
     * GCC 15-built V8 objects reference std::ios_base_library_init(), but
     * the AROS static libstdc++ in this tree still initializes ios_base
     * through the older constructor objects.  Keep this local to v8.library
     * so the monolith can link without changing the global C++ runtime.
     */
    void _ZSt21ios_base_library_initv(void) __attribute__((weak));
    void _ZSt21ios_base_library_initv(void) { }

}

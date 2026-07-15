// Merged entry point for the 7 CompileTimeResourceSystem ad-hoc debug/scratch
// programs (PDB consolidation - each used to be its own add_executable with
// its own int main(); merging avoids re-collating RenderGraph's /Z7 debug
// info into 7 separate ~165MB PDBs).
int run_test_minimal();
int run_test_compile_time_cache();
int run_test_traits_debug();
int run_test_registration();
int run_test_simple_traits();
int run_test_syntax_check();
int run_test_type_check();

int main() {
    int result = 0;

    result |= run_test_minimal();
    result |= run_test_compile_time_cache();
    result |= run_test_traits_debug();
    result |= run_test_registration();
    result |= run_test_simple_traits();
    result |= run_test_syntax_check();
    result |= run_test_type_check();

    return result;
}

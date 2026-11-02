# The one authoritative editor capture timeline (T-6547). The CTest producer, the Windows
# VIXEN/temp/run_editor_script.bat and VIXEN/tools/multiinstance_cache_gate.py all consume these
# values; test_editor_toggle_undo_capture.cpp asserts the artifacts they produce. Keep each value on
# its own `set(NAME "value")` line: the python gate parses this file textually.
set(VIXEN_EDITOR_CAPTURE_SCRIPT
    "parameter_up:0@20,parameter_up:0@21,parameter_up:0@22,parameter_up:0@23,toggle:2@30,undo@60,redo@90,save@100,reopen@110,settings@120,back@130")
set(VIXEN_EDITOR_CAPTURE_FRAME_LIST "5,25,45,75,105,115")
set(VIXEN_EDITOR_CAPTURE_EXIT_AFTER_FRAMES "140")

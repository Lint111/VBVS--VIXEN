// autosync_fullscreen.vert — auto-sync FrameGraph P4 demo (pass C, vertex stage).
// Classic 3-vertex fullscreen triangle generated from gl_VertexIndex; no vertex
// input bindings (the graphics pipeline is built with vertex input disabled).
// Covers the whole viewport so the fragment shader runs per screen pixel.
#version 460

void main() {
    // Maps vertex indices 0,1,2 -> a triangle that covers the [-1,1] NDC square:
    //   index 0 -> (-1,-1), index 1 -> ( 3,-1), index 2 -> (-1, 3)
    vec2 pos = vec2(
        float((gl_VertexIndex << 1) & 2),
        float( gl_VertexIndex       & 2)
    );
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}

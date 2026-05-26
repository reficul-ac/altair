# render_core Public Headers

`animus::render_core` owns native window/context setup, OpenGL loading,
shader/mesh wrappers, debug callback setup, and render stats. Phase E keeps
the API intentionally narrow: enough to prove a deterministic GLFW/GLEW
OpenGL context and draw one primitive before terrain tiles arrive.

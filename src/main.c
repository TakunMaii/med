#include "med.h"

int main(int argc, char **argv) {
    App app;
    memset(&app, 0, sizeof(app));
    editor_init(&app.editor, argc > 1 ? argv[1] : NULL);
    for (int i = 2; i < argc; i++) {
        editor_open_buffer(&app.editor, argv[i]);
    }
    if (argc > 2) editor_load_buffer(&app.editor, 0);
    vk_init(&app);
    while (!glfwWindowShouldClose(app.vk.window)) {
        glfwPollEvents();
        draw_frame(&app);
    }
    vkDeviceWaitIdle(app.vk.device);
    return 0;
}

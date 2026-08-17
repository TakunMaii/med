#include "med.h"

static Color color_for_highlight(HighlightKind kind) {
    switch (kind) {
    case HL_KEYWORD: return gruvbox.keyword;
    case HL_STRING: return gruvbox.string;
    case HL_COMMENT: return gruvbox.comment;
    case HL_FUNCTION: return gruvbox.function;
    case HL_TYPE: return gruvbox.type;
    case HL_NUMBER: return gruvbox.number;
    case HL_PREPROC: return gruvbox.preproc;
    default: return gruvbox.fg;
    }
}

typedef enum {
    TEXT_STYLE_NORMAL = 0,
    TEXT_STYLE_ITALIC = 1 << 0,
    TEXT_STYLE_BOLD = 1 << 1,
} TextStyle;

static TextStyle style_for_highlight(HighlightKind kind) {
    switch (kind) {
    case HL_FUNCTION: return TEXT_STYLE_BOLD;
    case HL_STRING:
    case HL_KEYWORD:
        return TEXT_STYLE_ITALIC;
    default:
        return TEXT_STYLE_NORMAL;
    }
}

typedef struct {
    float screen_size[2];
    float rect_min[2];
    float rect_max[2];
    float p0[2];
    float p1[2];
    float half_size[2];
    float color[4];
    float softness;
    float intensity;
    float mode;
    float _pad;
} CursorPush;

typedef struct {
    float x, y, w, h;
    size_t view_index;
} PaneRect;

static uint32_t find_memory(VkPhysicalDevice physical, uint32_t type_bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    die("no suitable memory type");
    return 0;
}

static void create_buffer(VkApp *app, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, GpuBuffer *out) {
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (vkCreateBuffer(app->device, &bi, NULL, &out->buffer) != VK_SUCCESS) die("vkCreateBuffer failed");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(app->device, out->buffer, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = find_memory(app->physical, req.memoryTypeBits, props)};
    if (vkAllocateMemory(app->device, &ai, NULL, &out->memory) != VK_SUCCESS) die("vkAllocateMemory buffer failed");
    vkBindBufferMemory(app->device, out->buffer, out->memory, 0);
}

static VkCommandBuffer begin_one_time(VkApp *app) {
    VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = app->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(app->device, &ai, &cmd);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

static void end_one_time(VkApp *app, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(app->graphics_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(app->graphics_queue);
    vkFreeCommandBuffers(app->device, app->cmd_pool, 1, &cmd);
}

static void transition_image(VkApp *app, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkCommandBuffer cmd = begin_one_time(app);
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    VkPipelineStageFlags src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, NULL, 0, NULL, 1, &barrier);
    end_one_time(app, cmd);
}

static void copy_buffer_to_image(VkApp *app, VkBuffer buffer, VkImage image, uint32_t w, uint32_t h) {
    VkCommandBuffer cmd = begin_one_time(app);
    VkBufferImageCopy region = {
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageExtent = {w, h, 1},
    };
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    end_one_time(app, cmd);
}

static VkShaderModule load_shader(VkApp *app, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", MED_SHADER_DIR, name);
    size_t len = 0;
    char *bytes = read_file(path, &len);
    if (!bytes) die("failed to read shader");
    VkShaderModuleCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = len, .pCode = (const uint32_t *)bytes};
    VkShaderModule mod;
    if (vkCreateShaderModule(app->device, &ci, NULL, &mod) != VK_SUCCESS) die("vkCreateShaderModule failed");
    free(bytes);
    return mod;
}

static void font_create(VkApp *app) {
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) die("FreeType init failed");
    FT_Face face;
    if (FT_New_Face(ft, MED_DEFAULT_FONT, 0, &face)) die("failed to load CaskaydiaCove font");
    FT_Set_Pixel_Sizes(face, 0, MED_FONT_SIZE);
    unsigned char *pixels = calloc(ATLAS_W * ATLAS_H, 1);
    if (!pixels) die("out of memory");
    int pen_x = 1, pen_y = 1, row_h = 0;
    app->font.ascent = (float)(face->size->metrics.ascender >> 6);
    app->font.line_h = (float)((face->size->metrics.height >> 6) + 4);
    app->font.cell_w = 8.0f;
    for (int c = 32; c < 127; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = face->glyph;
        if (pen_x + (int)g->bitmap.width + 1 >= ATLAS_W) {
            pen_x = 1;
            pen_y += row_h + 1;
            row_h = 0;
        }
        for (int y = 0; y < (int)g->bitmap.rows; y++) {
            memcpy(pixels + (pen_y + y) * ATLAS_W + pen_x, g->bitmap.buffer + y * g->bitmap.pitch, g->bitmap.width);
        }
        Glyph *gl = &app->font.glyphs[c];
        gl->u0 = (float)pen_x / ATLAS_W;
        gl->v0 = (float)pen_y / ATLAS_H;
        gl->u1 = (float)(pen_x + g->bitmap.width) / ATLAS_W;
        gl->v1 = (float)(pen_y + g->bitmap.rows) / ATLAS_H;
        gl->x0 = 0;
        gl->y0 = 0;
        gl->x1 = (float)g->bitmap.width;
        gl->y1 = (float)g->bitmap.rows;
        gl->advance = (float)(g->advance.x >> 6);
        gl->bearing_x = (float)g->bitmap_left;
        gl->bearing_y = (float)g->bitmap_top;
        if (c == 'M') app->font.cell_w = gl->advance;
        pen_x += (int)g->bitmap.width + 1;
        if ((int)g->bitmap.rows > row_h) row_h = (int)g->bitmap.rows;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = {ATLAS_W, ATLAS_H, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(app->device, &ii, NULL, &app->font.image) != VK_SUCCESS) die("vkCreateImage font failed");
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(app->device, app->font.image, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = find_memory(app->physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    if (vkAllocateMemory(app->device, &ai, NULL, &app->font.memory) != VK_SUCCESS) die("font memory failed");
    vkBindImageMemory(app->device, app->font.image, app->font.memory, 0);

    GpuBuffer staging;
    create_buffer(app, ATLAS_W * ATLAS_H, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging);
    void *mapped;
    vkMapMemory(app->device, staging.memory, 0, ATLAS_W * ATLAS_H, 0, &mapped);
    memcpy(mapped, pixels, ATLAS_W * ATLAS_H);
    vkUnmapMemory(app->device, staging.memory);
    transition_image(app, app->font.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(app, staging.buffer, app->font.image, ATLAS_W, ATLAS_H);
    transition_image(app, app->font.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(app->device, staging.buffer, NULL);
    vkFreeMemory(app->device, staging.memory, NULL);
    free(pixels);

    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = app->font.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCreateImageView(app->device, &vi, NULL, &app->font.view);
    VkSamplerCreateInfo si = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    };
    vkCreateSampler(app->device, &si, NULL, &app->font.sampler);
}

static bool queue_supports_present(VkApp *app, uint32_t family) {
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(app->physical, family, app->surface, &present);
    return present == VK_TRUE;
}

static void pick_physical(VkApp *app) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(app->instance, &count, NULL);
    if (!count) die("no Vulkan physical device");
    VkPhysicalDevice *devices = xmalloc(sizeof(*devices) * count);
    vkEnumeratePhysicalDevices(app->instance, &count, devices);
    for (uint32_t d = 0; d < count; d++) {
        app->physical = devices[d];
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(app->physical, &qcount, NULL);
        VkQueueFamilyProperties *qs = xmalloc(sizeof(*qs) * qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(app->physical, &qcount, qs);
        bool got_g = false, got_p = false;
        for (uint32_t i = 0; i < qcount; i++) {
            if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !got_g) {
                app->graphics_family = i;
                got_g = true;
            }
            if (queue_supports_present(app, i) && !got_p) {
                app->present_family = i;
                got_p = true;
            }
        }
        free(qs);
        if (got_g && got_p) {
            free(devices);
            return;
        }
    }
    free(devices);
    die("no suitable Vulkan device");
}

static void create_device(VkApp *app) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci[2] = {
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = app->graphics_family, .queueCount = 1, .pQueuePriorities = &priority},
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = app->present_family, .queueCount = 1, .pQueuePriorities = &priority},
    };
    uint32_t qn = app->graphics_family == app->present_family ? 1 : 2;
    const char *exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = qn, .pQueueCreateInfos = qci, .enabledExtensionCount = 1, .ppEnabledExtensionNames = exts};
    if (vkCreateDevice(app->physical, &ci, NULL, &app->device) != VK_SUCCESS) die("vkCreateDevice failed");
    vkGetDeviceQueue(app->device, app->graphics_family, 0, &app->graphics_queue);
    vkGetDeviceQueue(app->device, app->present_family, 0, &app->present_queue);
}

static VkSurfaceFormatKHR choose_format(VkSurfaceFormatKHR *formats, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return formats[i];
    }
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return formats[i];
    }
    return formats[0];
}

static void create_swapchain(VkApp *app) {
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(app->window, &fbw, &fbh);
    while (fbw == 0 || fbh == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(app->window, &fbw, &fbh);
    }
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physical, app->surface, &caps);
    uint32_t fcount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical, app->surface, &fcount, NULL);
    VkSurfaceFormatKHR *formats = xmalloc(sizeof(*formats) * fcount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical, app->surface, &fcount, formats);
    VkSurfaceFormatKHR fmt = choose_format(formats, fcount);
    free(formats);
    app->swap_format = fmt.format;
    if (caps.currentExtent.width != UINT32_MAX) {
        app->extent = caps.currentExtent;
    } else {
        app->extent.width = (uint32_t)fbw;
        app->extent.height = (uint32_t)fbh;
        if (app->extent.width < caps.minImageExtent.width) app->extent.width = caps.minImageExtent.width;
        if (app->extent.width > caps.maxImageExtent.width) app->extent.width = caps.maxImageExtent.width;
        if (app->extent.height < caps.minImageExtent.height) app->extent.height = caps.minImageExtent.height;
        if (app->extent.height > caps.maxImageExtent.height) app->extent.height = caps.maxImageExtent.height;
    }
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    uint32_t families[] = {app->graphics_family, app->present_family};
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = app->surface,
        .minImageCount = image_count,
        .imageFormat = app->swap_format,
        .imageColorSpace = fmt.colorSpace,
        .imageExtent = app->extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    if (app->graphics_family != app->present_family) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    if (vkCreateSwapchainKHR(app->device, &ci, NULL, &app->swapchain) != VK_SUCCESS) die("vkCreateSwapchainKHR failed");
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &app->image_count, NULL);
    VkImage *imgs = xmalloc(sizeof(*imgs) * app->image_count);
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &app->image_count, imgs);
    app->images = calloc(app->image_count, sizeof(*app->images));
    for (uint32_t i = 0; i < app->image_count; i++) {
        app->images[i].image = imgs[i];
        VkImageViewCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = imgs[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = app->swap_format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
        };
        vkCreateImageView(app->device, &vi, NULL, &app->images[i].view);
    }
    free(imgs);
}

static void create_render_pass(VkApp *app) {
    VkAttachmentDescription color = {
        .format = app->swap_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &ref};
    VkSubpassDependency dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color, .subpassCount = 1, .pSubpasses = &sub, .dependencyCount = 1, .pDependencies = &dep};
    if (vkCreateRenderPass(app->device, &ci, NULL, &app->render_pass) != VK_SUCCESS) die("vkCreateRenderPass failed");
}

static void create_pipeline(VkApp *app) {
    if (!app->desc_layout) {
        VkDescriptorSetLayoutBinding b = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
        VkDescriptorSetLayoutCreateInfo dl = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &b};
        vkCreateDescriptorSetLayout(app->device, &dl, NULL, &app->desc_layout);
    }
    VkPushConstantRange pc = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(float) * 2};
    VkPipelineLayoutCreateInfo pl = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &app->desc_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc};
    vkCreatePipelineLayout(app->device, &pl, NULL, &app->pipeline_layout);

    VkShaderModule vs = load_shader(app, "text.vert.spv");
    VkShaderModule fs = load_shader(app, "text.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"},
    };
    VkVertexInputBindingDescription binding = {.binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[4] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, x)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, u)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, r)},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(Vertex, use_tex)},
    };
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding, .vertexAttributeDescriptionCount = 4, .pVertexAttributeDescriptions = attrs};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {.x = 0, .y = 0, .width = (float)app->extent.width, .height = (float)app->extent.height, .minDepth = 0, .maxDepth = 1};
    VkRect2D scissor = {.extent = app->extent};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend = {.blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend};
    VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vp, .pRasterizationState = &rs, .pMultisampleState = &ms, .pColorBlendState = &cb, .layout = app->pipeline_layout, .renderPass = app->render_pass};
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &gp, NULL, &app->pipeline) != VK_SUCCESS) die("vkCreateGraphicsPipelines failed");
    vkDestroyShaderModule(app->device, vs, NULL);
    vkDestroyShaderModule(app->device, fs, NULL);
}

static void create_cursor_pipeline(VkApp *app) {
    VkPushConstantRange pc = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(CursorPush)};
    VkPipelineLayoutCreateInfo pl = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc};
    vkCreatePipelineLayout(app->device, &pl, NULL, &app->cursor_pipeline_layout);

    VkShaderModule vs = load_shader(app, "cursor.vert.spv");
    VkShaderModule fs = load_shader(app, "cursor.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {.x = 0, .y = 0, .width = (float)app->extent.width, .height = (float)app->extent.height, .minDepth = 0, .maxDepth = 1};
    VkRect2D scissor = {.extent = app->extent};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend = {.blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend};
    VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vp, .pRasterizationState = &rs, .pMultisampleState = &ms, .pColorBlendState = &cb, .layout = app->cursor_pipeline_layout, .renderPass = app->render_pass};
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &gp, NULL, &app->cursor_pipeline) != VK_SUCCESS) die("cursor pipeline failed");
    vkDestroyShaderModule(app->device, vs, NULL);
    vkDestroyShaderModule(app->device, fs, NULL);
}

static void create_framebuffers(VkApp *app) {
    for (uint32_t i = 0; i < app->image_count; i++) {
        VkImageView attachments[] = {app->images[i].view};
        VkFramebufferCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = app->render_pass, .attachmentCount = 1, .pAttachments = attachments, .width = app->extent.width, .height = app->extent.height, .layers = 1};
        vkCreateFramebuffer(app->device, &fi, NULL, &app->images[i].framebuffer);
    }
}

static void allocate_command_buffers(VkApp *app) {
    app->cmds = xmalloc(sizeof(*app->cmds) * app->image_count);
    VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = app->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = app->image_count};
    vkAllocateCommandBuffers(app->device, &ai, app->cmds);
}

static void create_descriptors(VkApp *app) {
    VkDescriptorPoolSize size = {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1};
    VkDescriptorPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size};
    vkCreateDescriptorPool(app->device, &pi, NULL, &app->desc_pool);
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = app->desc_pool, .descriptorSetCount = 1, .pSetLayouts = &app->desc_layout};
    vkAllocateDescriptorSets(app->device, &ai, &app->desc_set);
    VkDescriptorImageInfo img = {.sampler = app->font.sampler, .imageView = app->font.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = app->desc_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &img};
    vkUpdateDescriptorSets(app->device, 1, &wr, 0, NULL);
}

static void create_commands_sync(VkApp *app) {
    VkCommandPoolCreateInfo cp = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = app->graphics_family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    vkCreateCommandPool(app->device, &cp, NULL, &app->cmd_pool);
    allocate_command_buffers(app);
    for (int i = 0; i < MAX_FRAMES; i++) {
        create_buffer(app, sizeof(Vertex) * MAX_VERTICES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->vertex_buffers[i]);
        VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        vkCreateSemaphore(app->device, &si, NULL, &app->image_available[i]);
        vkCreateSemaphore(app->device, &si, NULL, &app->render_finished[i]);
        vkCreateFence(app->device, &fi, NULL, &app->in_flight[i]);
    }
}

static void cleanup_swapchain(VkApp *app) {
    if (app->cmds) {
        vkFreeCommandBuffers(app->device, app->cmd_pool, app->image_count, app->cmds);
        free(app->cmds);
        app->cmds = NULL;
    }
    if (app->images) {
        for (uint32_t i = 0; i < app->image_count; i++) {
            if (app->images[i].framebuffer) vkDestroyFramebuffer(app->device, app->images[i].framebuffer, NULL);
            if (app->images[i].view) vkDestroyImageView(app->device, app->images[i].view, NULL);
        }
        free(app->images);
        app->images = NULL;
    }
    if (app->pipeline) {
        vkDestroyPipeline(app->device, app->pipeline, NULL);
        app->pipeline = VK_NULL_HANDLE;
    }
    if (app->pipeline_layout) {
        vkDestroyPipelineLayout(app->device, app->pipeline_layout, NULL);
        app->pipeline_layout = VK_NULL_HANDLE;
    }
    if (app->cursor_pipeline) {
        vkDestroyPipeline(app->device, app->cursor_pipeline, NULL);
        app->cursor_pipeline = VK_NULL_HANDLE;
    }
    if (app->cursor_pipeline_layout) {
        vkDestroyPipelineLayout(app->device, app->cursor_pipeline_layout, NULL);
        app->cursor_pipeline_layout = VK_NULL_HANDLE;
    }
    if (app->render_pass) {
        vkDestroyRenderPass(app->device, app->render_pass, NULL);
        app->render_pass = VK_NULL_HANDLE;
    }
    if (app->swapchain) {
        vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
        app->swapchain = VK_NULL_HANDLE;
    }
    app->image_count = 0;
}

static void recreate_swapchain(VkApp *app) {
    vkDeviceWaitIdle(app->device);
    cleanup_swapchain(app);
    create_swapchain(app);
    create_render_pass(app);
    create_pipeline(app);
    create_cursor_pipeline(app);
    create_framebuffers(app);
    allocate_command_buffers(app);
    app->framebuffer_resized = false;
}

static void framebuffer_cb(GLFWwindow *window, int w, int h) {
    (void)w;
    (void)h;
    App *app = glfwGetWindowUserPointer(window);
    app->vk.framebuffer_resized = true;
}

static float tab_bar_height(const Editor *e, float line_h);

static int wrapped_line_count(const char *s, int cols) {
    if (cols < 1) cols = 1;
    int lines = 1;
    int col = 0;
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '\n') {
            lines++;
            col = 0;
            continue;
        }
        col++;
        if (col >= cols) {
            lines++;
            col = 0;
        }
    }
    return lines;
}

static int command_area_lines_for_width(const Editor *e, float width, float cell) {
    int cols = cell > 0.0f ? (int)((width - 16.0f) / cell) : 80;
    if (cols < 1) cols = 1;
    int lines = 1;
    if (e->mode == MODE_COMMAND) {
        lines = wrapped_line_count(e->command, cols - 1);
    } else if (e->status[0]) {
        lines = wrapped_line_count(e->status, cols);
    }
    if (lines < 1) lines = 1;
    if (lines > 8) lines = 8;
    return lines;
}

static void char_cb(GLFWwindow *window, unsigned int cp) {
    App *app = glfwGetWindowUserPointer(window);
    Editor *e = &app->editor;
    int old_line = byte_line(&e->text, e->cursor);
    int old_col = byte_col(&e->text, e->cursor);
    Mode old_mode = e->mode;
    bool track_cursor = e->mode != MODE_COMMAND && !e->suppress_next_char;
    if (e->suppress_next_char) {
        e->suppress_next_char = false;
        return;
    }
    if (e->waiting_char && cp >= 32 && cp <= 126) {
        editor_handle_waiting_char(e, (char)cp);
    } else if (e->mode == MODE_COMMAND) {
        if (cp >= 32 && cp <= 126 && e->command_len + 1 < sizeof(e->command)) {
            e->command[e->command_len++] = (char)cp;
            e->command[e->command_len] = 0;
        }
    } else if (e->mode == MODE_INSERT) {
        editor_insert_char(e, cp);
    } else if (e->mode == MODE_NORMAL && cp == ':') {
        e->mode = MODE_COMMAND;
        e->command_len = 0;
        e->command[0] = 0;
    }
    if (track_cursor) editor_record_cursor_if_moved(e, old_line, old_col, old_mode, glfwGetTime());
}

static void key_cb(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)scancode;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    App *app = glfwGetWindowUserPointer(window);
    Editor *e = &app->editor;
    int old_line = byte_line(&e->text, e->cursor);
    int old_col = byte_col(&e->text, e->cursor);
    Mode old_mode = e->mode;
    bool track_cursor = e->mode != MODE_COMMAND;
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    float tab_h = tab_bar_height(e, app->vk.font.line_h);
    int command_lines = command_area_lines_for_width(e, (float)fbw, app->vk.font.cell_w);
    int rows = app->vk.font.line_h > 0 ? (int)(((float)fbh - tab_h - app->vk.font.line_h * (1.0f + (float)command_lines)) / app->vk.font.line_h) : 1;
    if (rows < 1) rows = 1;
    if (e->waiting_char && key != GLFW_KEY_ESCAPE) return;
    if (e->mode == MODE_INSERT && e->lsp.completion_visible) {
        if (key == GLFW_KEY_TAB || ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_N)) {
            lsp_completion_move(e, 1);
            return;
        }
        if (((mods & GLFW_MOD_SHIFT) && key == GLFW_KEY_TAB) || ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_P)) {
            lsp_completion_move(e, -1);
            return;
        }
        if (key == GLFW_KEY_ENTER || ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_Y)) {
            if (lsp_completion_accept(e)) return;
        }
        if (key == GLFW_KEY_ESCAPE) {
            e->lsp.completion_visible = false;
        }
    }
    if (e->mode == MODE_COMMAND) {
        if (key == GLFW_KEY_ESCAPE) {
            e->mode = MODE_NORMAL;
            e->command_len = 0;
            e->command[0] = 0;
            return;
        }
        if (key == GLFW_KEY_ENTER) {
            if ((mods & (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL)) && e->command_len + 1 < sizeof(e->command)) {
                e->command[e->command_len++] = '\n';
                e->command[e->command_len] = 0;
                return;
            }
            bool search_cmd = e->command[0] == '/' || e->command[0] == '?';
            app_execute_command(app);
            if (search_cmd) editor_record_cursor_if_moved(e, old_line, old_col, MODE_NORMAL, glfwGetTime());
            return;
        }
        if (key == GLFW_KEY_BACKSPACE) {
            if (e->command_len > 0) e->command[--e->command_len] = 0;
            else e->mode = MODE_NORMAL;
            return;
        }
        return;
    }
    editor_key(e, key, mods, rows);
    if (track_cursor) editor_record_cursor_if_moved(e, old_line, old_col, old_mode, glfwGetTime());
}

void vk_init(App *owner) {
    VkApp *app = &owner->vk;
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    app->window = glfwCreateWindow(1100, 760, "med", NULL, NULL);
    if (!app->window) die("failed to create window");
    glfwSetWindowUserPointer(app->window, owner);
    glfwSetFramebufferSizeCallback(app->window, framebuffer_cb);
    glfwSetCharCallback(app->window, char_cb);
    glfwSetKeyCallback(app->window, key_cb);
    uint32_t ext_count = 0;
    const char **exts = glfwGetRequiredInstanceExtensions(&ext_count);
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "med", .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai, .enabledExtensionCount = ext_count, .ppEnabledExtensionNames = exts};
    if (vkCreateInstance(&ici, NULL, &app->instance) != VK_SUCCESS) die("vkCreateInstance failed");
    if (glfwCreateWindowSurface(app->instance, app->window, NULL, &app->surface) != VK_SUCCESS) die("surface failed");
    pick_physical(app);
    create_device(app);
    create_swapchain(app);
    create_render_pass(app);
    create_commands_sync(app);
    font_create(app);
    create_pipeline(app);
    create_cursor_pipeline(app);
    create_framebuffers(app);
    create_descriptors(app);
}

static void dl_push(DrawList *dl, Vertex v) {
    if (dl->count < dl->cap) dl->vertices[dl->count++] = v;
}

static void draw_rect(DrawList *dl, float x, float y, float w, float h, Color c) {
    Vertex v[6] = {
        {x, y, 0, 0, c.r, c.g, c.b, c.a, 0}, {x + w, y, 0, 0, c.r, c.g, c.b, c.a, 0}, {x + w, y + h, 0, 0, c.r, c.g, c.b, c.a, 0},
        {x, y, 0, 0, c.r, c.g, c.b, c.a, 0}, {x + w, y + h, 0, 0, c.r, c.g, c.b, c.a, 0}, {x, y + h, 0, 0, c.r, c.g, c.b, c.a, 0},
    };
    for (int i = 0; i < 6; i++) dl_push(dl, v[i]);
}

static void draw_glyph_quad(DrawList *dl, Glyph *g, float x0_top, float x1_top, float x0_bottom, float x1_bottom, float y0, float y1, Color c) {
    Vertex v[6] = {
        {x0_top, y0, g->u0, g->v0, c.r, c.g, c.b, c.a, 1}, {x1_top, y0, g->u1, g->v0, c.r, c.g, c.b, c.a, 1}, {x1_bottom, y1, g->u1, g->v1, c.r, c.g, c.b, c.a, 1},
        {x0_top, y0, g->u0, g->v0, c.r, c.g, c.b, c.a, 1}, {x1_bottom, y1, g->u1, g->v1, c.r, c.g, c.b, c.a, 1}, {x0_bottom, y1, g->u0, g->v1, c.r, c.g, c.b, c.a, 1},
    };
    for (int i = 0; i < 6; i++) dl_push(dl, v[i]);
}

static void draw_glyph(DrawList *dl, FontAtlas *font, char ch, float x, float y, Color c, TextStyle style) {
    if ((unsigned char)ch >= 127 || ch < 32) return;
    Glyph *g = &font->glyphs[(int)ch];
    float x0 = x + g->bearing_x;
    float y0 = y + font->ascent - g->bearing_y;
    float x1 = x0 + g->x1;
    float y1 = y0 + g->y1;
    float top_shift = 0.0f;
    float bottom_shift = 0.0f;
    if (style & TEXT_STYLE_ITALIC) {
        float slant = 0.22f;
        top_shift = g->y1 * slant;
    }
    draw_glyph_quad(dl, g, x0 + top_shift, x1 + top_shift, x0 + bottom_shift, x1 + bottom_shift, y0, y1, c);
    if (style & TEXT_STYLE_BOLD) {
        draw_glyph_quad(dl, g, x0 + top_shift + 0.9f, x1 + top_shift + 0.9f, x0 + bottom_shift + 0.9f, x1 + bottom_shift + 0.9f, y0, y1, c);
    }
}

static void draw_text(DrawList *dl, FontAtlas *font, const char *s, float x, float y, Color c, TextStyle style) {
    for (size_t i = 0; s[i]; i++) {
        draw_glyph(dl, font, s[i], x, y, c, style);
        x += font->cell_w;
    }
}

static void draw_wrapped_text(DrawList *dl, FontAtlas *font, const char *s, float x, float y, int cols, int max_lines, Color c, TextStyle style) {
    if (cols < 1) cols = 1;
    int col = 0;
    int line = 0;
    float origin_x = x;
    bool last_newline = false;
    for (size_t i = 0; s[i] && line < max_lines; i++) {
        if (s[i] == '\r') continue;
        if (s[i] == '\n') {
            if (last_newline) continue;
            last_newline = true;
            line++;
            col = 0;
            x = origin_x;
            y += font->line_h;
            continue;
        }
        last_newline = false;
        draw_glyph(dl, font, s[i], x, y, c, style);
        x += font->cell_w;
        col++;
        if (col >= cols) {
            line++;
            col = 0;
            x = origin_x;
            y += font->line_h;
        }
    }
}

static size_t bounded_strlen(const char *s, size_t max_len) {
    size_t n = 0;
    while (n < max_len && s[n]) n++;
    return n;
}

static void measure_wrapped_text(const char *s, int max_cols, int max_lines, int *out_cols, int *out_lines) {
    int col = 0;
    int line = 1;
    int max_seen = 0;
    bool last_newline = false;
    if (max_cols < 1) max_cols = 1;
    if (max_lines < 1) max_lines = 1;
    for (size_t i = 0; s[i] && line <= max_lines; i++) {
        if (s[i] == '\r') continue;
        if (s[i] == '\n') {
            if (last_newline) continue;
            if (col > max_seen) max_seen = col;
            col = 0;
            line++;
            last_newline = true;
            continue;
        }
        last_newline = false;
        col++;
        if (col >= max_cols) {
            if (col > max_seen) max_seen = col;
            col = 0;
            line++;
        }
    }
    if (col > max_seen) max_seen = col;
    if (line > max_lines) line = max_lines;
    if (max_seen < 1) max_seen = 1;
    *out_cols = max_seen;
    *out_lines = line;
}

static void command_cursor_position(const Editor *e, float command_y, float cell, float line_h, int cols, float *x, float *y) {
    if (cols < 2) cols = 2;
    int col = 1;
    int line = 0;
    for (size_t i = 0; i < e->command_len; i++) {
        if (e->command[i] == '\n') {
            line++;
            col = 0;
            continue;
        }
        col++;
        if (col >= cols) {
            line++;
            col = 0;
        }
    }
    if (line > 7) line = 7;
    *x = 8.0f + (float)col * cell + 1.0f;
    *y = command_y + 1.0f + (float)line * line_h + line_h * 0.5f;
}

static bool in_selection(const Editor *e, size_t pos) {
    if (e->mode != MODE_VISUAL) return false;
    if (e->visual_block) {
        int a_line = byte_line(&e->text, e->visual_anchor);
        int b_line = byte_line(&e->text, e->cursor);
        int a_col = byte_col(&e->text, e->visual_anchor);
        int b_col = byte_col(&e->text, e->cursor);
        int line = byte_line(&e->text, pos);
        int col = byte_col(&e->text, pos);
        if (a_line > b_line) {
            int t = a_line;
            a_line = b_line;
            b_line = t;
        }
        if (a_col > b_col) {
            int t = a_col;
            a_col = b_col;
            b_col = t;
        }
        return line >= a_line && line <= b_line && col >= a_col && col <= b_col;
    }
    size_t a = e->visual_anchor, b = e->cursor;
    if (a > b) {
        size_t t = a;
        a = b;
        b = t;
    }
    if (e->visual_line) {
        a = line_start_at(&e->text, a);
        b = line_end_at(&e->text, b);
    }
    return pos >= a && pos <= b;
}

static bool in_search_match(const Editor *e, size_t pos) {
    if (!e->search_active || e->search_len == 0) return false;
    for (size_t i = 0; i < e->search_len; i++) {
        if (pos < i) break;
        size_t start = pos - i;
        if (start + e->search_len <= e->text.len && memcmp(e->text.data + start, e->search, e->search_len) == 0) return true;
    }
    return false;
}

static const char *mode_name(const Editor *e) {
    if (e->mode == MODE_INSERT) return "-- INSERT --";
    if (e->mode == MODE_COMMAND) return "COMMAND";
    if (e->mode == MODE_VISUAL && e->visual_block) return "-- VISUAL BLOCK --";
    if (e->mode == MODE_VISUAL && e->visual_line) return "-- VISUAL LINE --";
    if (e->mode == MODE_VISUAL) return "-- VISUAL --";
    return "NORMAL";
}

static void append_key(char *dst, size_t dst_size, char key) {
    size_t len = strlen(dst);
    if (len + 2 >= dst_size) return;
    dst[len] = key;
    dst[len + 1] = 0;
}

static void build_pending_keys(const Editor *e, char *dst, size_t dst_size) {
    dst[0] = 0;
    if (e->operator_pending) append_key(dst, dst_size, e->operator_pending);
    if (e->waiting_char == 'i' || e->waiting_char == 'a') append_key(dst, dst_size, e->waiting_char);
    else if (e->waiting_char) append_key(dst, dst_size, e->waiting_char);
    if (e->pending && !e->operator_pending && !e->waiting_char) append_key(dst, dst_size, e->pending);
    if (e->count > 0 && !e->operator_pending && !e->pending && !e->waiting_char) {
        snprintf(dst, dst_size, "%d", e->count);
    }
}

static void apply_view_for_render(Editor *dst, const Editor *src, size_t view_index) {
    *dst = *src;
    if (src->tab_count == 0 || src->current_tab >= src->tab_count) return;
    const EditorTab *tab = &src->tabs[src->current_tab];
    if (view_index >= tab->view_count) return;
    const EditorView *v = &tab->views[view_index];
    if (v->buffer_index >= src->buffer_count) return;
    const BufferSlot *b = &src->buffers[v->buffer_index];
    dst->text = b->text;
    memcpy(dst->path, b->path, sizeof(dst->path));
    dst->dirty = b->dirty;
    dst->cursor = v->cursor <= b->text.len ? v->cursor : b->text.len;
    dst->visual_anchor = v->visual_anchor <= b->text.len ? v->visual_anchor : dst->cursor;
    dst->desired_col = v->desired_col;
    dst->top_line = v->top_line;
    dst->left_col = v->left_col;
    dst->tree = b->tree;
    dst->highlights = b->highlights;
    dst->current_buffer = v->buffer_index;
    if (view_index != tab->active_view) {
        dst->mode = MODE_NORMAL;
        dst->visual_line = false;
        dst->visual_block = false;
    }
}

static void collect_panes(const EditorTab *tab, int node_index, float x, float y, float w, float h, PaneRect *out, size_t *count) {
    if (node_index < 0 || (size_t)node_index >= tab->node_count || *count >= EDITOR_MAX_VIEWS) return;
    const SplitNode *n = &tab->nodes[node_index];
    if (n->kind == SPLIT_LEAF) {
        out[(*count)++] = (PaneRect){x, y, w, h, n->view_index};
        return;
    }
    float gap = 3.0f;
    if (n->kind == SPLIT_COL) {
        float first_w = floorf((w - gap) * n->ratio);
        collect_panes(tab, n->first, x, y, first_w, h, out, count);
        collect_panes(tab, n->second, x + first_w + gap, y, w - first_w - gap, h, out, count);
    } else {
        float first_h = floorf((h - gap) * n->ratio);
        collect_panes(tab, n->first, x, y, w, first_h, out, count);
        collect_panes(tab, n->second, x, y + first_h + gap, w, h - first_h - gap, out, count);
    }
}

static const char *buffer_display_name(const BufferSlot *b) {
    if (!b->path[0]) return "[No Name]";
    const char *slash = strrchr(b->path, '/');
    return slash ? slash + 1 : b->path;
}

static void draw_tab_bar(App *owner, DrawList *dl, float w, float line_h) {
    Editor *e = &owner->editor;
    if (e->tab_count <= 1) return;
    VkApp *vk = &owner->vk;
    draw_rect(dl, 0, 0, w, line_h + 4.0f, gruvbox.gutter_bg);
    float x = 8.0f;
    for (size_t i = 0; i < e->tab_count; i++) {
        EditorTab *tab = &e->tabs[i];
        size_t view = tab->active_view < tab->view_count ? tab->active_view : 0;
        size_t buffer = tab->view_count ? tab->views[view].buffer_index : 0;
        if (buffer >= e->buffer_count) buffer = 0;
        const BufferSlot *b = &e->buffers[buffer];
        char label[160];
        snprintf(label, sizeof(label), "%zu  %s%s", i + 1, buffer_display_name(b), b->dirty ? " +" : "");
        float tab_w = (float)strlen(label) * vk->font.cell_w + 22.0f;
        Color bg = i == e->current_tab ? gruvbox.bg : gruvbox.gutter_bg;
        draw_rect(dl, x, 3.0f, tab_w, line_h - 2.0f, bg);
        if (i == e->current_tab) draw_rect(dl, x, line_h + 1.0f, tab_w, 2.0f, gruvbox.line_no_current);
        draw_text(dl, &vk->font, label, x + 10.0f, 4.0f, i == e->current_tab ? gruvbox.line_no_current : gruvbox.gutter_fg, TEXT_STYLE_NORMAL);
        x += tab_w + 4.0f;
        if (x > w) break;
    }
}

static float tab_bar_height(const Editor *e, float line_h) {
    return e->tab_count > 1 ? line_h + 4.0f : 0.0f;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void update_cursor_animation(Editor *e, float target_x, float target_y, double now) {
    AnimatedCursor *a = &e->cursor_anim;
    if (!a->initialized) {
        a->x = target_x;
        a->y = target_y;
        a->velocity_x = 0.0f;
        a->velocity_y = 0.0f;
        a->last_time = now;
        a->initialized = true;
        return;
    }
    float dt = (float)(now - a->last_time);
    a->last_time = now;
    dt = clampf(dt, 0.0f, 1.0f / 30.0f);
    float dx = target_x - a->x;
    float dy = target_y - a->y;
    float old_dot = dx * a->velocity_x + dy * a->velocity_y;
    a->velocity_x += dx * CURSOR_ANIM_STIFFNESS * dt;
    a->velocity_y += dy * CURSOR_ANIM_STIFFNESS * dt;
    float damping = powf(CURSOR_ANIM_DAMPING, dt * 60.0f);
    a->velocity_x *= damping;
    a->velocity_y *= damping;
    a->x += a->velocity_x * dt;
    a->y += a->velocity_y * dt;
    float new_dx = target_x - a->x;
    float new_dy = target_y - a->y;
    float new_dot = new_dx * a->velocity_x + new_dy * a->velocity_y;
    float dist = sqrtf(new_dx * new_dx + new_dy * new_dy);
    float speed = sqrtf(a->velocity_x * a->velocity_x + a->velocity_y * a->velocity_y);
    if ((old_dot > 0.0f && new_dot < 0.0f && dist < 4.0f) || (dist < 0.55f && speed < 24.0f)) {
        a->x = target_x;
        a->y = target_y;
        a->velocity_x = 0.0f;
        a->velocity_y = 0.0f;
    }
}

static void build_cursor_segments(App *owner, Editor *e, float pane_x, float pane_y, float pane_w, float pane_h, float gutter_w, float cell, float line_h, int rows, float command_y) {
    VkApp *vk = &owner->vk;
    vk->cursor_segment_count = 0;
    bool command = e->mode == MODE_COMMAND;
    bool insert = e->mode == MODE_INSERT || command;
    float half_w = insert ? 1.4f : cell * 0.5f;
    float half_h = insert ? line_h * 0.5f - 2.0f : line_h * 0.5f;
    if (half_h < 2.0f) half_h = 2.0f;
    float p1x = 0.0f;
    float p1y = 0.0f;
    if (command) {
        int cols = cell > 0.0f ? (int)(((float)vk->extent.width - 16.0f) / cell) : 80;
        command_cursor_position(e, command_y, cell, line_h, cols, &p1x, &p1y);
    } else {
        int target_line_i = byte_line(&e->text, e->cursor);
        int target_col_i = byte_col(&e->text, e->cursor);
        float target_row = (float)(target_line_i - e->top_line);
        float target_screen_col = (float)(target_col_i - e->left_col);
        if (target_row < 0.0f || target_row >= (float)rows || target_screen_col < 0.0f) return;
        p1x = pane_x + gutter_w + 8.0f + target_screen_col * cell + (insert ? 1.0f : cell * 0.5f);
        p1y = pane_y + target_row * line_h + 2.0f + line_h * 0.5f;
    }
    double now = glfwGetTime();
    update_cursor_animation(e, p1x, p1y, now);

    float dx = p1x - e->cursor_anim.x;
    float dy = p1y - e->cursor_anim.y;
    float dist_px = sqrtf(dx * dx + dy * dy);
    float max_dist_px = CURSOR_MAX_TRAIL_CELLS * fmaxf(cell, line_h);
    float p0x = e->cursor_anim.x;
    float p0y = e->cursor_anim.y;
    if (dist_px > max_dist_px) {
        float scale = max_dist_px / dist_px;
        p0x = p1x - dx * scale;
        p0y = p1y - dy * scale;
    }
    float margin = fmaxf(half_w, half_h) + 12.0f;
    CursorSegment *s = &vk->cursor_segments[vk->cursor_segment_count++];
    s->rect_min[0] = fminf(p0x, p1x) - margin;
    s->rect_min[1] = fminf(p0y, p1y) - margin;
    s->rect_max[0] = fmaxf(p0x, p1x) + margin;
    s->rect_max[1] = fmaxf(p0y, p1y) + margin;
    s->rect_min[0] = clampf(s->rect_min[0], command ? 0.0f : pane_x, command ? (float)vk->extent.width : pane_x + pane_w);
    s->rect_min[1] = clampf(s->rect_min[1], command ? 0.0f : pane_y, command ? (float)vk->extent.height : pane_y + pane_h);
    s->rect_max[0] = clampf(s->rect_max[0], command ? 0.0f : pane_x, command ? (float)vk->extent.width : pane_x + pane_w);
    s->rect_max[1] = clampf(s->rect_max[1], command ? 0.0f : pane_y, command ? (float)vk->extent.height : pane_y + pane_h);
    if (s->rect_min[0] >= s->rect_max[0] || s->rect_min[1] >= s->rect_max[1]) {
        vk->cursor_segment_count--;
        return;
    }
    s->p0[0] = p0x;
    s->p0[1] = p0y;
    s->p1[0] = p1x;
    s->p1[1] = p1y;
    s->half_size[0] = half_w;
    s->half_size[1] = half_h;
    Color c = insert ? gruvbox.cursor_insert : gruvbox.cursor;
    s->color[0] = c.r;
    s->color[1] = c.g;
    s->color[2] = c.b;
    s->color[3] = c.a;
    s->softness = insert ? 5.0f : 8.0f;
    s->intensity = dist_px > 0.75f ? 1.0f : 0.0f;
    s->mode = insert ? 1.0f : 0.0f;
    s->_pad = 0.0f;
}

static void draw_editor_pane(App *owner, DrawList *dl, Editor *pe, float x0, float y0, float w, float h, bool active, float command_y) {
    VkApp *vk = &owner->vk;
    float line_h = vk->font.line_h;
    float cell = vk->font.cell_w;
    int rows = (int)(h / line_h);
    if (rows < 1) rows = 1;
    int gutter_digits = (int)log10((double)(line_count(&pe->text) + 1)) + 2;
    float gutter_w = (float)gutter_digits * cell + 18.0f;
    int cols = (int)((w - gutter_w - 12.0f) / cell);
    if (active) {
        editor_ensure_visible(pe, rows, cols);
        build_cursor_segments(owner, pe, x0, y0, w, h, gutter_w, cell, line_h, rows, command_y);
    }
    draw_rect(dl, x0, y0, w, h, gruvbox.bg);
    draw_rect(dl, x0, y0, gutter_w, h, gruvbox.gutter_bg);
    Color border = active ? gruvbox.line_no_current : gruvbox.gutter_fg;
    border.a = active ? 0.9f : 0.25f;
    draw_rect(dl, x0, y0, w, 1.0f, border);
    draw_rect(dl, x0, y0 + h - 1.0f, w, 1.0f, border);
    draw_rect(dl, x0, y0, 1.0f, h, border);
    draw_rect(dl, x0 + w - 1.0f, y0, 1.0f, h, border);

    int cursor_line = byte_line(&pe->text, pe->cursor);
    for (int row = 0; row < rows; row++) {
        int line_no = pe->top_line + row;
        if (line_no >= line_count(&pe->text)) break;
        float y = y0 + row * line_h + 2.0f;
        char num[32];
        int rel = abs(line_no - cursor_line);
        int width = gutter_digits - 1;
        if (width > 16) width = 16;
        char raw[24];
        int shown_no = pe->relative_number && rel != 0 ? rel : line_no + 1;
        snprintf(raw, sizeof(raw), "%d", pe->show_number ? shown_no : 0);
        int raw_len = (int)strlen(raw);
        int pad = width > raw_len ? width - raw_len : 0;
        memset(num, ' ', (size_t)pad);
        snprintf(num + pad, sizeof(num) - (size_t)pad, "%s", raw);
        if (pe->show_number) draw_text(dl, &vk->font, num, x0 + 8.0f, y, rel == 0 && active ? gruvbox.line_no_current : gruvbox.gutter_fg, TEXT_STYLE_NORMAL);
        size_t start = line_start_by_number(&pe->text, line_no);
        size_t end = line_end_at(&pe->text, start);
        float x = x0 + gutter_w + 8.0f;
        for (size_t p = start + (size_t)pe->left_col; p < end; p++) {
            if (in_selection(pe, p)) draw_rect(dl, x, y, cell, line_h, gruvbox.selection);
            else if (in_search_match(pe, p)) draw_rect(dl, x, y, cell, line_h, gruvbox.search_match);
            HighlightKind hl = highlight_at(pe, p);
            Color c = color_for_highlight(hl);
            TextStyle style = style_for_highlight(hl);
            char ch = pe->text.data[p];
            if (ch == '\t') x += cell * 4.0f;
            else {
                draw_glyph(dl, &vk->font, ch, x, y, c, style);
                x += cell;
            }
            if (x > x0 + w) break;
        }
    }
    char label[192];
    const BufferSlot *buffer = pe->current_buffer < pe->buffer_count ? &pe->buffers[pe->current_buffer] : NULL;
    snprintf(label, sizeof(label), " %zu:%s%s ", pe->current_buffer + 1, buffer ? buffer_display_name(buffer) : "[No Name]", pe->dirty ? " +" : "");
    draw_rect(dl, x0 + 1.0f, y0 + h - line_h, fminf((float)strlen(label) * cell + 8.0f, w - 2.0f), line_h - 1.0f, gruvbox.gutter_bg);
    draw_text(dl, &vk->font, label, x0 + 5.0f, y0 + h - line_h + 1.0f, active ? gruvbox.line_no_current : gruvbox.gutter_fg, TEXT_STYLE_NORMAL);
}

static void draw_lsp_popups(App *owner, DrawList *dl, float editor_y, float editor_h) {
    Editor *e = &owner->editor;
    VkApp *vk = &owner->vk;
    float cell = vk->font.cell_w;
    float line_h = vk->font.line_h;
    int line = byte_line(&e->text, e->cursor);
    int col = byte_col(&e->text, e->cursor);
    int gutter_digits = (int)log10((double)(line_count(&e->text) + 1)) + 2;
    float gutter_w = (float)gutter_digits * cell + 18.0f;
    float x = gutter_w + 8.0f + (float)(col - e->left_col) * cell;
    float y = editor_y + (float)(line - e->top_line + 1) * line_h + 2.0f;
    if (y < editor_y || y > editor_y + editor_h) return;
    Color popup_bg = gruvbox.gutter_bg;
    popup_bg.r += 0.035f;
    popup_bg.g += 0.035f;
    popup_bg.b += 0.035f;
    if (popup_bg.r > 1.0f) popup_bg.r = 1.0f;
    if (popup_bg.g > 1.0f) popup_bg.g = 1.0f;
    if (popup_bg.b > 1.0f) popup_bg.b = 1.0f;
    if (e->lsp.completion_visible && e->lsp.completion_count > 0) {
        size_t rows = e->lsp.completion_count < 8 ? e->lsp.completion_count : 8;
        size_t first = e->lsp.completion_scroll;
        if (first >= e->lsp.completion_count) first = 0;
        if (first + rows > e->lsp.completion_count) rows = e->lsp.completion_count - first;
        size_t max_cols = 1;
        for (size_t i = 0; i < rows; i++) {
            size_t item_index = first + i;
            size_t label_len = bounded_strlen(e->lsp.completions[item_index], 64);
            size_t detail_len = bounded_strlen(e->lsp.completion_details[item_index], 63);
            size_t cols = label_len + (detail_len ? 1 + detail_len : 0);
            if (cols > max_cols) max_cols = cols;
        }
        if (max_cols < 18) max_cols = 18;
        if (max_cols > 128) max_cols = 128;
        float w = (float)max_cols * cell + 14.0f;
        float max_w = (float)vk->extent.width - 16.0f;
        if (w > max_w) w = max_w;
        float h = (float)rows * line_h + 6.0f;
        if (x < 8.0f) x = 8.0f;
        if (x + w > (float)vk->extent.width) x = (float)vk->extent.width - w - 8.0f;
        draw_rect(dl, x, y, w, h, popup_bg);
        draw_rect(dl, x, y, w, 1.0f, gruvbox.gutter_fg);
        draw_rect(dl, x, y + h - 1.0f, w, 1.0f, gruvbox.gutter_fg);
        for (size_t i = 0; i < rows; i++) {
            size_t item_index = first + i;
            float row_y = y + 3.0f + (float)i * line_h;
            if (item_index == e->lsp.completion_selected) draw_rect(dl, x + 2.0f, row_y, w - 4.0f, line_h, gruvbox.selection);
            char item[320];
            if (e->lsp.completion_details[item_index][0]) snprintf(item, sizeof(item), "%.64s %.63s", e->lsp.completions[item_index], e->lsp.completion_details[item_index]);
            else snprintf(item, sizeof(item), "%.128s", e->lsp.completions[item_index]);
            draw_text(dl, &vk->font, item, x + 6.0f, row_y + 1.0f, gruvbox.fg, TEXT_STYLE_NORMAL);
        }
    }
    if (e->lsp.hover_visible && e->lsp.hover[0]) {
        float hx = x;
        float hy = y + line_h;
        int max_cols = (int)(((float)vk->extent.width - 32.0f) / cell);
        if (max_cols > 128) max_cols = 128;
        int measured_cols = 0;
        int measured_lines = 0;
        measure_wrapped_text(e->lsp.hover, max_cols, 18, &measured_cols, &measured_lines);
        if (measured_cols < 24) measured_cols = 24;
        float hw = (float)measured_cols * cell + 16.0f;
        float max_hw = (float)vk->extent.width - 16.0f;
        if (hw > max_hw) hw = max_hw;
        float hh = (float)measured_lines * line_h + 10.0f;
        float max_hh = editor_h - line_h;
        if (hh > max_hh) hh = max_hh > line_h ? max_hh : line_h;
        if (hy + hh > editor_y + editor_h) hy = y - hh - line_h;
        if (hx < 8.0f) hx = 8.0f;
        if (hx + hw > (float)vk->extent.width) hx = (float)vk->extent.width - hw - 8.0f;
        draw_rect(dl, hx, hy, hw, hh, popup_bg);
        draw_rect(dl, hx, hy, hw, 1.0f, gruvbox.gutter_fg);
        int hover_cols = (int)((hw - 16.0f) / cell);
        int hover_lines = (int)((hh - 8.0f) / line_h);
        draw_wrapped_text(dl, &vk->font, e->lsp.hover, hx + 8.0f, hy + 5.0f, hover_cols, hover_lines, gruvbox.fg, TEXT_STYLE_NORMAL);
    }
}

static void build_draw_list(App *owner, DrawList *dl) {
    Editor *e = &owner->editor;
    VkApp *vk = &owner->vk;
    dl->count = 0;
    float w = (float)vk->extent.width;
    float h = (float)vk->extent.height;
    float line_h = vk->font.line_h;
    draw_rect(dl, 0, 0, w, h, gruvbox.bg);
    draw_tab_bar(owner, dl, w, line_h);
    int command_lines = command_area_lines_for_width(e, w, vk->font.cell_w);
    float command_h = line_h * (float)command_lines;
    float status_y = h - line_h - command_h;
    float command_y = h - command_h;
    float tab_h = tab_bar_height(e, line_h);
    float editor_y = tab_h;
    float editor_h = h - tab_h - line_h - command_h;
    if (editor_h < line_h) editor_h = line_h;
    EditorTab *tab = e->tab_count ? &e->tabs[e->current_tab] : NULL;
    PaneRect panes[EDITOR_MAX_VIEWS];
    size_t pane_count = 0;
    if (tab) collect_panes(tab, tab->root, 0.0f, editor_y, w, editor_h, panes, &pane_count);
    for (size_t i = 0; i < pane_count; i++) {
        bool active = tab && panes[i].view_index == tab->active_view;
        if (active) {
            draw_editor_pane(owner, dl, e, panes[i].x, panes[i].y, panes[i].w, panes[i].h, true, command_y);
            editor_sync_active_view(e);
        } else {
            Editor tmp;
            apply_view_for_render(&tmp, e, panes[i].view_index);
            draw_editor_pane(owner, dl, &tmp, panes[i].x, panes[i].y, panes[i].w, panes[i].h, false, command_y);
        }
    }
    draw_lsp_popups(owner, dl, editor_y, editor_h);
    draw_rect(dl, 0, status_y, w, line_h, gruvbox.gutter_bg);
    draw_rect(dl, 0, command_y, w, command_h, gruvbox.bg);
    char pending[64];
    build_pending_keys(e, pending, sizeof(pending));
    char status[768];
    const char *path = e->path[0] ? e->path : "[No Name]";
    int cursor_display_line = byte_line(&e->text, e->cursor) + 1;
    int cursor_display_col = byte_col(&e->text, e->cursor) + 1;
    char diagnostic[256] = "";
    for (size_t i = 0; i < e->lsp.diagnostic_count; i++) {
        if (e->lsp.diagnostics[i].line == cursor_display_line - 1) {
            snprintf(diagnostic, sizeof(diagnostic), "  E: %.120s", e->lsp.diagnostics[i].message);
            break;
        }
    }
    snprintf(status, sizeof(status), "%s  [%zu/%zu]%s  %.360s  Ln %d, Col %d  %s%.48s%.160s",
             mode_name(e), e->current_buffer + 1, e->buffer_count, e->dirty ? " +" : "",
             path, cursor_display_line, cursor_display_col, pending[0] ? "keys: " : "",
             pending, diagnostic);
    draw_text(dl, &vk->font, status, 8.0f, status_y + 1.0f, gruvbox.line_no_current, TEXT_STYLE_NORMAL);
    if (e->mode == MODE_COMMAND) {
        char command[EDITOR_COMMAND_MAX + 2];
        snprintf(command, sizeof(command), ":%s", e->command);
        int cols = vk->font.cell_w > 0.0f ? (int)((w - 16.0f) / vk->font.cell_w) : 80;
        draw_wrapped_text(dl, &vk->font, command, 8.0f, command_y + 1.0f, cols, command_lines, gruvbox.line_no_current, TEXT_STYLE_NORMAL);
    } else if (e->status[0]) {
        int cols = vk->font.cell_w > 0.0f ? (int)((w - 16.0f) / vk->font.cell_w) : 80;
        draw_wrapped_text(dl, &vk->font, e->status, 8.0f, command_y + 1.0f, cols, command_lines, gruvbox.line_no_current, TEXT_STYLE_NORMAL);
    }
}

static void record_cmd(App *owner, uint32_t image, uint32_t vcount) {
    VkApp *app = &owner->vk;
    VkCommandBuffer cmd = app->cmds[image];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);
    VkClearValue clear = {.color = {{gruvbox.bg.r, gruvbox.bg.g, gruvbox.bg.b, 1.0f}}};
    VkRenderPassBeginInfo rp = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = app->render_pass, .framebuffer = app->images[image].framebuffer, .renderArea = {.extent = app->extent}, .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline_layout, 0, 1, &app->desc_set, 0, NULL);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &app->vertex_buffers[app->frame].buffer, &off);
    float screen[2] = {(float)app->extent.width, (float)app->extent.height};
    vkCmdPushConstants(cmd, app->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen), screen);
    vkCmdDraw(cmd, vcount, 1, 0, 0);
    if (app->cursor_pipeline && app->cursor_segment_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->cursor_pipeline);
        for (uint32_t i = 0; i < app->cursor_segment_count; i++) {
            CursorSegment *s = &app->cursor_segments[i];
            CursorPush push = {
                .screen_size = {(float)app->extent.width, (float)app->extent.height},
                .rect_min = {s->rect_min[0], s->rect_min[1]},
                .rect_max = {s->rect_max[0], s->rect_max[1]},
                .p0 = {s->p0[0], s->p0[1]},
                .p1 = {s->p1[0], s->p1[1]},
                .half_size = {s->half_size[0], s->half_size[1]},
                .color = {s->color[0], s->color[1], s->color[2], s->color[3]},
                .softness = s->softness,
                .intensity = s->intensity,
                .mode = s->mode,
            };
            vkCmdPushConstants(cmd, app->cursor_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

void draw_frame(App *owner) {
    VkApp *app = &owner->vk;
    vkWaitForFences(app->device, 1, &app->in_flight[app->frame], VK_TRUE, UINT64_MAX);
    if (app->framebuffer_resized) {
        recreate_swapchain(app);
        return;
    }
    uint32_t image = 0;
    VkResult res = vkAcquireNextImageKHR(app->device, app->swapchain, UINT64_MAX, app->image_available[app->frame], VK_NULL_HANDLE, &image);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain(app);
        return;
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) return;
    vkResetFences(app->device, 1, &app->in_flight[app->frame]);
    DrawList dl = {.vertices = xmalloc(sizeof(Vertex) * MAX_VERTICES), .cap = MAX_VERTICES};
    build_draw_list(owner, &dl);
    void *mapped;
    vkMapMemory(app->device, app->vertex_buffers[app->frame].memory, 0, sizeof(Vertex) * dl.count, 0, &mapped);
    memcpy(mapped, dl.vertices, sizeof(Vertex) * dl.count);
    vkUnmapMemory(app->device, app->vertex_buffers[app->frame].memory);
    record_cmd(owner, image, dl.count);
    free(dl.vertices);
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &app->image_available[app->frame],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &app->cmds[image],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &app->render_finished[app->frame],
    };
    if (vkQueueSubmit(app->graphics_queue, 1, &submit, app->in_flight[app->frame]) != VK_SUCCESS) die("vkQueueSubmit failed");
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &app->render_finished[app->frame], .swapchainCount = 1, .pSwapchains = &app->swapchain, .pImageIndices = &image};
    res = vkQueuePresentKHR(app->present_queue, &present);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || app->framebuffer_resized) {
        recreate_swapchain(app);
    } else if (res != VK_SUCCESS) {
        die("vkQueuePresentKHR failed");
    }
    app->frame = (app->frame + 1) % MAX_FRAMES;
}

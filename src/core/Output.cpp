#include "Output.hpp"
#include "../helpers/Log.hpp"

static void handleGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char* make,
                           const char* model, int32_t transform) {
    const auto POUTPUT = (COutput*)data;
    POUTPUT->transform = (wl_output_transform)transform;

    Debug::log(LOG, "output {} make {} model {}", POUTPUT->name, make ? make : "", model ? model : "");
}

static void handleMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    const auto POUTPUT = (COutput*)data;

    // handle portrait mode and flipped cases
    if (POUTPUT->transform == WL_OUTPUT_TRANSFORM_270 || POUTPUT->transform == WL_OUTPUT_TRANSFORM_90 ||
        POUTPUT->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270 || POUTPUT->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90)
        POUTPUT->size = {height, width};
    else
        POUTPUT->size = {width, height};
}

static void handleDone(void* data, wl_output* output) {
    ;
}

static void handleScale(void* data, wl_output* output, int32_t factor) {
    const auto POUTPUT = (COutput*)data;
    POUTPUT->scale     = factor;
}

static void handleName(void* data, wl_output* output, const char* name) {
    const auto POUTPUT  = (COutput*)data;
    POUTPUT->stringName = std::string{name} + POUTPUT->stringName;
    POUTPUT->stringPort = std::string{name};
    Debug::log(LOG, "output {} name {}", POUTPUT->name, name);
}

static void handleDescription(void* data, wl_output* output, const char* description) {
    const auto POUTPUT = (COutput*)data;
    Debug::log(LOG, "output {} description {}", POUTPUT->name, description ? description : "");
}

static const wl_output_listener outputListener = {
    .geometry    = handleGeometry,
    .mode        = handleMode,
    .done        = handleDone,
    .scale       = handleScale,
    .name        = handleName,
    .description = handleDescription,
};

COutput::COutput(wl_output* output, uint32_t name) : name(name), output(output) {
    wl_output_add_listener(output, &outputListener, this);
}
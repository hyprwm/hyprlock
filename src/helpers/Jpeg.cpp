#include "Jpeg.hpp"
#include "Log.hpp"
#include <jpeglib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

cairo_surface_t* JPEG::createSurfaceFromJPEG(const std::filesystem::path& path) {

    if (!std::filesystem::exists(path)) {
        Debug::log(ERR, "createSurfaceFromJPEG: file doesn't exist??");
        return nullptr;
    }

    if (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__) {
        Debug::log(CRIT, "tried to load a jpeg on a big endian system! ping vaxry he is lazy.");
        return nullptr;
    }

    void*       imageRawData;
    struct stat fileInfo = {};
    const auto  FD       = open(path.c_str(), O_RDONLY);

    fstat(FD, &fileInfo);

    imageRawData = malloc(fileInfo.st_size);

    read(FD, imageRawData, fileInfo.st_size);
    close(FD);

    // now the JPEG is in the memory

    jpeg_decompress_struct decompressStruct = {};
    jpeg_error_mgr         errorManager     = {};

    decompressStruct.err = jpeg_std_error(&errorManager);
    jpeg_create_decompress(&decompressStruct);
    jpeg_mem_src(&decompressStruct, (const unsigned char*)imageRawData, fileInfo.st_size);
    jpeg_read_header(&decompressStruct, true);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    decompressStruct.out_color_space = JCS_EXT_BGRA;
#else
    decompressStruct.out_color_space = JCS_EXT_ARGB;
#endif

    // decompress
    jpeg_start_decompress(&decompressStruct);

    auto cairoSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, decompressStruct.output_width, decompressStruct.output_height);

    if (cairo_surface_status(cairoSurface) != CAIRO_STATUS_SUCCESS) {
        Debug::log(ERR, "createSurfaceFromJPEG: Cairo Failed (?)");
        return nullptr;
    }

    const auto CAIRODATA   = cairo_image_surface_get_data(cairoSurface);
    const auto CAIROSTRIDE = cairo_image_surface_get_stride(cairoSurface);
    JSAMPROW   rowRead;

    while (decompressStruct.output_scanline < decompressStruct.output_height) {
        const auto PROW = CAIRODATA + (decompressStruct.output_scanline * CAIROSTRIDE);
        rowRead         = PROW;
        jpeg_read_scanlines(&decompressStruct, &rowRead, 1);
    }

    cairo_surface_flush(cairoSurface);
    cairo_surface_mark_dirty(cairoSurface);
    cairo_surface_set_mime_data(cairoSurface, CAIRO_MIME_TYPE_JPEG, (const unsigned char*)imageRawData, fileInfo.st_size, free, imageRawData);
    jpeg_finish_decompress(&decompressStruct);
    jpeg_destroy_decompress(&decompressStruct);

    return cairoSurface;
}

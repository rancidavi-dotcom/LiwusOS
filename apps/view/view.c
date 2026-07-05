#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libliw.h>
#include <png.h>
#include <jpeglib.h>
#include <strings.h>
#include <unistd.h>
#include <setjmp.h>

void display_png(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("view: nao foi possivel abrir %s\n", filename);
        return;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return;
    }
    
    if (setjmp(png_jmpbuf(png))) {
        printf("view: erro ao processar PNG\n");
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);
    
    png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_byte *)malloc(png_get_rowbytes(png, info));
    }

    png_read_image(png, row_pointers);

    uint32_t *img_buffer = liw_create_buffer(width, height);
    if (!img_buffer) {
        printf("view: erro de memoria ao criar buffer\n");
        return;
    }

    printf("Renderizando PNG: %dx%d\n", width, height);

    for (int y = 0; y < height; y++) {
        png_bytep row = row_pointers[y];
        for (int x = 0; x < width; x++) {
            png_bytep px = &(row[x * 4]);
            // Convert RGBA to ARGB/XRGB for LiwusOS
            img_buffer[y * width + x] = (0xFF << 24) | (px[0] << 16) | (px[1] << 8) | px[2];
        }
    }

    liw_present_frame(img_buffer, width, height);
    free(img_buffer);

    for (int y = 0; y < height; y++) free(row_pointers[y]);
    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
}

struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr *my_error_ptr;

void my_error_exit(j_common_ptr cinfo) {
    my_error_ptr myerr = (my_error_ptr)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

void display_jpg(const char *filename) {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("view: nao foi possivel abrir %s\n", filename);
        return;
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int components = cinfo.output_components;

    uint32_t *img_buffer = liw_create_buffer(width, height);
    if (!img_buffer) {
        printf("view: erro de memoria ao criar buffer\n");
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return;
    }

    printf("Renderizando JPG: %dx%d\n", width, height);

    JSAMPROW row_pointer[1];
    unsigned char *line_buffer = malloc(width * components);

    while (cinfo.output_scanline < (uint32_t)height) {
        row_pointer[0] = line_buffer;
        int y = cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, row_pointer, 1);
        for (int x = 0; x < width; x++) {
            uint32_t color;
            if (components == 3) {
                color = (0xFF << 24) | (line_buffer[x * 3] << 16) | (line_buffer[x * 3 + 1] << 8) | line_buffer[x * 3 + 2];
            } else if (components == 1) {
                color = (0xFF << 24) | (line_buffer[x] << 16) | (line_buffer[x] << 8) | line_buffer[x];
            } else {
                color = 0;
            }
            img_buffer[y * width + x] = color;
        }
    }

    liw_present_frame(img_buffer, width, height);

    free(line_buffer);
    free(img_buffer);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Uso: view <arquivo.png/jpg>\n");
        return 1;
    }

    const char *ext = strrchr(argv[1], '.');
    if (!ext) {
        printf("view: extensao desconhecida\n");
        return 1;
    }

    printf("LiwusOS ImageViewer v1.1\n");
    if (strcasecmp(ext, ".png") == 0) {
        display_png(argv[1]);
    } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        display_jpg(argv[1]);
    } else {
        printf("view: formato %s nao suportado\n", ext);
        return 1;
    }
    
    printf("Pressione qualquer tecla para sair...\n");
    char c;
    read(0, &c, 1);

    return 0;
}

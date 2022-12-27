#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <MagickCore/MagickCore.h>

typedef struct {
    unsigned int r, g, b;
} Pixel;

static void resize_image(Image *image, ImageInfo *image_info, float resize_factor, char *new_filename, ExceptionInfo *exception) {
    Image *resize_image;
    Image *thumbnails = NewImageList();
    int new_width  = image->columns * resize_factor;
    int new_height = image->rows * resize_factor;
    resize_image = ResizeImage(image, new_width, new_height, LanczosFilter, exception);
    printf("%zu x %zu\n", resize_image->columns, resize_image->rows);
    if(!resize_image)
        MagickError(exception->severity, exception->reason, exception->description);
    AppendImageToList(&thumbnails, resize_image);
    DestroyImage(image);

    strcpy(thumbnails->filename, new_filename);
    WriteImage(image_info, thumbnails, exception);

    DestroyImageList(thumbnails);
}

static void print_pixel_info(Image *image, int x, int y, ExceptionInfo *exception) {
    unsigned char pixels[3];
    if(ExportImagePixels(image, x, y, 1, 1, "RGB", CharPixel, pixels, exception))
        printf("RGB: %d, %d, %d\n", pixels[0], pixels[1], pixels[2]);
/*        printf("RGB: 0x%02hhx, 0x%02hhx, 0x%02hhx\n", pixels[0], pixels[1], pixels[2]);*/
}

static Pixel get_avg_color(unsigned char *pixels, const size_t pixels_column_cnt, int x, int y, int width, int height) {
    Pixel p = {0};
    int i = y * pixels_column_cnt + x * 3;
    for(int c=0; c < width*height;) {
        p.r += pixels[i++];
        p.g += pixels[i++];
        p.b += pixels[i++];
        if(++c % width == 0)
            i += (pixels_column_cnt - width) * 3; //next row ...
    }

    p.r /= width*height;
    p.g /= width*height;
    p.b /= width*height;
    return p;
}

static void print_avg_color(Image *image, unsigned int x, int y, int width, int height, ExceptionInfo *exception) {
    unsigned char *pixels = malloc(width * height * 3);
    if(!ExportImagePixels(image, x, y, width, height, "RGB", CharPixel, pixels, exception)) {
        free(pixels);
        exit(1);
    }
    Pixel p = get_avg_color(pixels, width * 3, x, y, width, height);
    printf("RGB: %d, %d, %d\n", p.r, p.g, p.b);
    free(pixels);
}

/*static Image *make_img_avg_colors(Image *image, const ssize_t first_x, const ssize_t first_y, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {*/
/*    unsigned char *ps = malloc((image->columns / each_width) * (image->rows / each_height) * 3);*/
/*    int i = 0;*/
/*    for(size_t y=first_y; y < image->rows; y+=each_height) {*/
/*        for(size_t x=first_x; x < image->columns; x+=each_width, i+=3) {*/
/*            Pixel p = get_avg_color(image, x, y, each_width, each_height, exception);*/
/*            ps[i] = p.r;*/
/*            ps[i+1] = p.g;*/
/*            ps[i+2] = p.b;*/
/*        }*/
/*    }*/
/*    Image *new_image = ConstituteImage(image->columns / each_width, image->rows / each_height, "RGB", CharPixel, ps, exception);*/
/*    free(ps);*/
/*    if(!new_image)*/
/*        exit(1);*/
/*    return new_image;*/
/*}*/

static Image *splotch(Image *image, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {
    const size_t pixel_cnt = image->columns * image->rows;
    unsigned char *pixels = malloc(pixel_cnt * 3);
    if(!ExportImagePixels(image, 0, 0, image->columns, image->rows, "RGB", CharPixel, pixels, exception)) {
        free(pixels);
        exit(1);
    }
    for(size_t i=0, j=0; i < pixel_cnt;) {
        /* Specifying 0 for y allows us to automatically use i to "roll over" into next row*/
        Pixel p = get_avg_color(pixels, image->columns, i, 0, each_width, each_height);
        for(size_t c=0; c < each_width*each_height;) {
            pixels[j++] = p.r;
            pixels[j++] = p.g;
            pixels[j++] = p.b;
            if(++c % each_width == 0)
                j += (image->columns - each_width) * 3; //next row ...
        }
        i += each_width; //next splotch
        /* If this row is done, skip over all the rows we just splotched */
        if(i % image->columns == 0)
            i += image->columns * (each_height - 1);
        j = i * 3;
    }
    Image *new_image = ConstituteImage(image->columns, image->rows, "RGB", CharPixel, pixels, exception);
    free(pixels);
    if(!new_image)
        exit(1);
    return new_image;
}

int main(int argc, char **argv) {
    ExceptionInfo *exception;
    Image *images, *image;
    ImageInfo *image_info;
/*    float resize_factor = 0.0;*/
/*    char *endptr;*/
/*    if(argc == 4) {*/
/*        const char *old_locale = setlocale(LC_ALL, NULL);*/
/*        setlocale(LC_ALL|~LC_NUMERIC, "");*/
/*        resize_factor = strtof(argv[3], &endptr);*/
/*        setlocale(LC_ALL, old_locale);*/
/*    }*/
/*    if(argc < 4 || !strncmp(argv[3], endptr, strlen(argv[3])) || resize_factor < 0.0) {*/
/*        fprintf(stderr, "Usage: %s old_image new_image resize_factor\n", argv[0]);*/
/*        exit(2);*/
/*    }*/
/*    if(resize_factor > 10.0) {*/
/*        fprintf(stderr, "resize_factor %.1f is too big. Please don't break my computer.\n", resize_factor);*/
/*        exit(1);*/
/*    }*/

    MagickCoreGenesis(*argv, MagickTrue);
    exception = AcquireExceptionInfo();
    image_info = CloneImageInfo((ImageInfo *)NULL);
    strcpy(image_info->filename, argv[1]);
    images = ReadImage(image_info, exception);
    if(exception->severity != UndefinedException)
        CatchException(exception);
    if(!images)
        exit(1);
    image=RemoveFirstImageFromList(&images);
    if(!image)
        exit(1);
/*    resize_image(image, image_info, resize_factor, argv[2], exception);*/
/*    for(unsigned int i=0; i < image->columns; i++)*/
/*        print_pixel_info(image, i, 0, exception);*/
/*    for(unsigned int i=0; i < image->columns; i++)*/
/*        print_avg_color(image, i, 0, 1, 1, exception);*/
    Image *new_image = make_img_avg_colors(image, 0, 0, 100, 100, exception);
    ImageInfo *new_image_info = CloneImageInfo((ImageInfo *)NULL);
    strcpy(new_image->filename, argv[2]);
    WriteImage(new_image_info, new_image, exception);

    DestroyImage(image);
    DestroyImage(new_image);
    DestroyImageInfo(image_info);
    DestroyImageInfo(new_image_info);
    DestroyExceptionInfo(exception);
    MagickCoreTerminus();
    return 0;
}

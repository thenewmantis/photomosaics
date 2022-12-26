#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <MagickCore/MagickCore.h>

typedef struct {
    unsigned int r, g, b;
} Pixel;

void resize_image(Image *image, ImageInfo *image_info, float resize_factor, char *new_filename, ExceptionInfo *exception) {
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

void print_pixel_info(Image *image, int x, int y, ExceptionInfo *exception) {
    unsigned char pixels[3];
    char *map = "RGB";
    if(ExportImagePixels(image, x, y, 1, 1, map, CharPixel, pixels, exception))
        printf("RGB: %d, %d, %d\n", pixels[0], pixels[1], pixels[2]);
/*        printf("RGB: 0x%02hhx, 0x%02hhx, 0x%02hhx\n", pixels[0], pixels[1], pixels[2]);*/
}

Pixel get_avg_color(Image *image, unsigned int x, int y, int width, int height, ExceptionInfo *exception) {
    unsigned char *pixels = malloc(width * height * 3);
    char *map = "RGB";
    Pixel p = {0};
    if(!ExportImagePixels(image, x, y, width, height, map, CharPixel, pixels, exception)) {
        free(pixels);
        exit(1);
    }
    for(int i=0; i < width*height*3; i+=3) {
        p.r += pixels[i];
        p.g += pixels[i+1];
        p.b += pixels[i+2];
    }
    free(pixels);

    p.r /= width*height;
    p.g /= width*height;
    p.b /= width*height;
    return p;
}

void print_avg_color(Image *image, unsigned int x, int y, int width, int height, ExceptionInfo *exception) {
    Pixel p = get_avg_color(image, x, y, width, height, exception);
    printf("RGB: %d, %d, %d\n", p.r, p.g, p.b);
}

Image *make_img_avg_colors(Image *image, const ssize_t first_x, const ssize_t first_y, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {
    unsigned char *ps = malloc((image->columns / each_width) * (image->rows / each_height) * 3);
    char *map = "RGB";
    int i = 0;
    for(size_t y=first_y; y < image->rows; y+=each_height) {
        for(size_t x=first_x; x < image->columns; x+=each_width, i+=3) {
            Pixel p = get_avg_color(image, x, y, each_width, each_height, exception);
            ps[i] = p.r;
            ps[i+1] = p.g;
            ps[i+2] = p.b;
        }
    }
    Image *new_image = ConstituteImage(image->columns / each_width, image->rows / each_height, map, CharPixel, ps, exception);
    free(ps);
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

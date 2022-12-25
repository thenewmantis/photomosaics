#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <MagickCore/MagickCore.h>

void resize_image(Image *images, ImageInfo *image_info, float resize_factor, char *new_filename, ExceptionInfo *exception) {
    Image *image, *resize_image;
    Image *thumbnails = NewImageList();
    while((image=RemoveFirstImageFromList(&images))) {
        int new_width  = image->columns * resize_factor;
        int new_height = image->rows * resize_factor;
        resize_image = ResizeImage(image, new_width, new_height, LanczosFilter, exception);
        printf("%zu x %zu\n", resize_image->columns, resize_image->rows);
        if(!resize_image)
            MagickError(exception->severity, exception->reason, exception->description);
        AppendImageToList(&thumbnails, resize_image);
        DestroyImage(image);
    }

    strcpy(thumbnails->filename, new_filename);
    WriteImage(image_info, thumbnails, exception);

    DestroyImageList(thumbnails);
}

int main(int argc, char **argv) {
    ExceptionInfo *exception;
    Image *images;
    ImageInfo *image_info;
    float resize_factor = 0.0;
    char *endptr;
    if(argc == 4) {
        const char *old_locale = setlocale(LC_ALL, NULL);
        setlocale(LC_ALL|~LC_NUMERIC, "");
        resize_factor = strtof(argv[3], &endptr);
        setlocale(LC_ALL, old_locale);
    }
    if(argc < 4 || !strncmp(argv[3], endptr, strlen(argv[3])) || resize_factor < 0.0) {
        fprintf(stderr, "Usage: %s old_image new_image resize_factor\n", argv[0]);
        exit(2);
    }
    if(resize_factor > 10.0) {
        fprintf(stderr, "resize_factor %.1f is too big. Please don't break my computer.\n", resize_factor);
        exit(1);
    }

    MagickCoreGenesis(*argv, MagickTrue);
    exception = AcquireExceptionInfo();
    image_info = CloneImageInfo((ImageInfo *)NULL);
    strcpy(image_info->filename, argv[1]);
    images = ReadImage(image_info, exception);
    if(exception->severity != UndefinedException)
        CatchException(exception);
    if(!images)
        exit(1);
    resize_image(images, image_info, resize_factor, argv[2], exception);

    DestroyImageInfo(image_info);
    DestroyExceptionInfo(exception);
    MagickCoreTerminus();
    return 0;
}

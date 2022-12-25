#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <MagickCore/MagickCore.h>

int main(int argc, char **argv) {
    ExceptionInfo *exception;
    Image *image, *images, *resize_image, *thumbnails;
    ImageInfo *image_info;
    if(argc != 3) {
        fprintf(stdout, "Usage: %s old_image new_image\n", argv[0]);
        exit(2);
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

    thumbnails = NewImageList();
    while((image=RemoveFirstImageFromList(&images))) {
        resize_image = ResizeImage(image, 237, 282, LanczosFilter, exception);
        if(!resize_image)
            MagickError(exception->severity, exception->reason, exception->description);
        AppendImageToList(&thumbnails, resize_image);
        DestroyImage(image);
    }
    
    strcpy(thumbnails->filename, argv[2]);
    WriteImage(image_info, thumbnails, exception);

    DestroyImageList(thumbnails);
    DestroyImageInfo(image_info);
    DestroyExceptionInfo(exception);
    MagickCoreTerminus();
    return 0;
}

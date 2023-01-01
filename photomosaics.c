#include <getopt.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <MagickCore/MagickCore.h>

#define DIE(rc, ...)    { fprintf(stderr, __VA_ARGS__); return rc; }

typedef struct {
    unsigned int r, g, b;
} Pixel;

static FILE *cache = NULL;

static bool parse_float(char *str, float *out) {
    char *endptr;
    const char *old_locale = setlocale(LC_ALL, NULL);
    setlocale(LC_ALL|~LC_NUMERIC, "");
    *out = strtof(str, &endptr);
    setlocale(LC_ALL, old_locale);
    return strncmp(str, endptr, strlen(str));
}
static bool parse_long(char *str, long *out) {
    char *endptr;
    const char *old_locale = setlocale(LC_ALL, NULL);
    setlocale(LC_ALL|~LC_NUMERIC, "");
    *out = strtol(str, &endptr, 10);
    setlocale(LC_ALL, old_locale);
    return strncmp(str, endptr, strlen(str));
}
static bool parse_hex_tol(char *str, long *out) {
    char *endptr;
    const char *old_locale = setlocale(LC_ALL, NULL);
    setlocale(LC_ALL|~LC_NUMERIC, "");
    *out = strtol(str, &endptr, 16);
    setlocale(LC_ALL, old_locale);
    return strncmp(str, endptr, strlen(str));
}
static bool parse_ulong(char *str, unsigned long *out) {
    char *endptr;
    const char *old_locale = setlocale(LC_ALL, NULL);
    setlocale(LC_ALL|~LC_NUMERIC, "");
    *out = strtoul(str, &endptr, 10);
    setlocale(LC_ALL, old_locale);
    return strncmp(str, endptr, strlen(str));
}

static bool cache_put(int nargs, ...) {
    if(!cache) cache = fopen("~/.cache/photomosaics/avgs");
    if(!cache) return false;
    char *buf;
    int ind = 0, chars_written;
    va_list vl;
    va_start(vl, nargs);
    for(int i=0; i < nargs-1; i++, ind += chars_written)
        chars_written = sprintf(buf[ind], "%s\t", va_arg(vl, char *));
    sprintf(buf[ind], "%s\n", va_arg(vl, char *));
    va_end(vl);
    return true;
}


static Image *resize_image_to(Image *image, const size_t new_width, const size_t new_height, ExceptionInfo  *exception) {
    Image *new_image = ResizeImage(image, new_width, new_height, LanczosFilter, exception);
    if(!new_image)
        MagickError(exception->severity, exception->reason, exception->description);
    return new_image;
}

static Image *resize_image_by_factor(Image *image, float resize_factor, ExceptionInfo *exception) {
    return resize_image_to(image, image->columns * resize_factor, image->rows * resize_factor, exception);
}

static void print_pixel_info(Image *image, const ssize_t x, const ssize_t y, ExceptionInfo *exception) {
    unsigned char pixels[3];
    if(ExportImagePixels(image, x, y, 1, 1, "RGB", CharPixel, pixels, exception))
        printf("RGB: %d, %d, %d\n", pixels[0], pixels[1], pixels[2]);
/*        printf("RGB: 0x%02hhx, 0x%02hhx, 0x%02hhx\n", pixels[0], pixels[1], pixels[2]);*/
}

static Pixel get_avg_color(unsigned char *pixels, const size_t pixels_column_cnt, const ssize_t x, const ssize_t y, const size_t width, const size_t height) {
    Pixel p = {0};
    int i = y * pixels_column_cnt + x * 3;
    for(unsigned long c=0; c < width*height;) {
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
static Pixel get_img_avg_color(Image *image, const ssize_t x, const ssize_t y, const size_t width, const size_t height, ExceptionInfo *exception) {
    unsigned char *pixels = malloc(width * height * 3);
    if(!ExportImagePixels(image, x, y, width, height, "RGB", CharPixel, pixels, exception)) {
        free(pixels);
        exit(1);
    }
    Pixel p = get_avg_color(pixels, width, 0, y, width, height);
    free(pixels);
    return p;
}

static void print_avg_color(Image *image, unsigned int x, unsigned int y, const size_t width, const size_t height, ExceptionInfo *exception) {
    Pixel p = get_img_avg_color(image, x, y, width, height, exception);
    printf("RGB: %d, %d, %d\n", p.r, p.g, p.b);
}

static Image *make_img_avg_colors(Image *image, const ssize_t first_x, const ssize_t first_y, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {
    unsigned char *ps = malloc((image->columns / each_width) * (image->rows / each_height) * 3);
    int i = 0;
    for(size_t y=first_y; y < image->rows; y+=each_height) {
        for(size_t x=first_x; x < image->columns; x+=each_width, i+=3) {
            Pixel p = get_img_avg_color(image, x, y, each_width, each_height, exception);
            ps[i] = p.r;
            ps[i+1] = p.g;
            ps[i+2] = p.b;
        }
    }
    Image *new_image = ConstituteImage(image->columns / each_width, image->rows / each_height, "RGB", CharPixel, ps, exception);
    free(ps);
    if(!new_image)
        exit(1);
    return new_image;
}

static Image *splotch_img(Image *image, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {
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
        MagickError(exception->severity, exception->reason, exception->description);
    return new_image;
}

static bool get_closest_pixel(Pixel p, const size_t width, const size_t height, unsigned char *pixels_out, ExceptionInfo *exception) {
    FILE *avgs_list = fopen("my_avgs", "r");
    if(!avgs_list) return false;
#define MY_AVGS_SIZE 5931
    char buf[MY_AVGS_SIZE];
    size_t z = fread(buf, 1, MY_AVGS_SIZE, avgs_list);
/*    printf("Read %zu bytes from my_avgs.\n", z);*/
    fclose(avgs_list);
    char *filename = strtok(buf, " ");
    char *closest_file = malloc(150);
    float distance_of_closest = sqrtf(powf(0xff, 2) * 3); //max diff value
    do {
        char *avg_p = strtok(NULL, "\n");
        char rstr[3] = {avg_p[0], avg_p[1],};
        char gstr[3] = {avg_p[2], avg_p[3],};
        char bstr[3] = {avg_p[4], avg_p[5],};
        long rdiff, gdiff, bdiff;
        parse_hex_tol(rstr, &rdiff);
        parse_hex_tol(gstr, &gdiff);
        parse_hex_tol(bstr, &bdiff);
        rdiff -= p.r;
        gdiff -= p.g;
        bdiff -= p.b;
        float new_distance = sqrtf(powf(rdiff, 2) + powf(gdiff, 2) + powf(bdiff, 2));
        if(new_distance < distance_of_closest) {
            distance_of_closest = new_distance;
            closest_file[0] = 0;
            strncat(closest_file, filename, 150);
        }
    } while((filename=strtok(NULL, " ")) && filename < buf + MY_AVGS_SIZE);

    size_t len = strnlen(closest_file, 150);
    if(len < 1 || len >= 150)
        return false;


    closest_file[len-1] = 0; //Get rid of the colon
    ImageInfo *image_info = CloneImageInfo((ImageInfo *)NULL);
    strcpy(image_info->filename, closest_file);
    Image *src_img = ReadImage(image_info, exception);
    Image *src_img_r = resize_image_to(src_img, width, height, exception);
    ExportImagePixels(src_img_r, 0, 0, width, height, "RGB", CharPixel, pixels_out, exception);
    DestroyImage(src_img);
    DestroyImage(src_img_r);
    DestroyImageInfo(image_info);
    return true;
}

static unsigned char *get_img_with_closest_avg(Pixel p, const size_t width, const size_t height, ExceptionInfo *exception) {
    Pixel img_avgs[105];
    FILE *f = popen("find $(find ~/pics -type d | grep -vE 'redacted|not_real') -maxdepth 1 -type f -print0", "r");
#define IMG_LIST_SIZE 5091
    char buf[IMG_LIST_SIZE];
    fread(buf, 1, IMG_LIST_SIZE, f);
    if(pclose(f)) exit(1);
    for(int c=0, k=0; c < IMG_LIST_SIZE; k++) {
        ImageInfo *image_info = CloneImageInfo((ImageInfo *)NULL);
        image_info->filename[0] = 0;
        strncat(image_info->filename, &buf[c], IMG_LIST_SIZE - c);
        Image *src_img = ReadImage(image_info, exception);
        Image *src_img_r = resize_image_to(src_img, width, height, exception);
        unsigned char *pixels = malloc(width * height * 3);
        ExportImagePixels(src_img_r, 0, 0, width, height, "RGB", CharPixel, pixels, exception);
        img_avgs[k] = get_avg_color(pixels, width, 0, 0, width, height);
        c += strnlen(&buf[c], IMG_LIST_SIZE - c) + 1;
        DestroyImage(src_img);
        DestroyImage(src_img_r);
        DestroyImageInfo(image_info);
    }
/*    FILE *avgs_list = fopen("al", "wb");*/
/*    if(!avgs_list) exit(1);*/
/*    for(int i=0; i < 105; i++) {*/
/*        char str[4] = {img_avgs[i].r, img_avgs[i].g, img_avgs[i].b,};*/
/*        fwrite(str, 1, 3, avgs_list);*/
/*    }*/
/*    fclose(avgs_list);*/
    FILE *my_avgs = fopen("my_avgs", "w");
    if(!my_avgs) exit(1);
    for(int i=0, c=0; i < 105 && c < IMG_LIST_SIZE; i++) {
        fprintf(my_avgs, "%s: %02x%02x%02x\n", &buf[c], img_avgs[i].r, img_avgs[i].g, img_avgs[i].b);
        c += strnlen(&buf[c], IMG_LIST_SIZE - c) + 1;
    }
    fclose(my_avgs);
    return NULL;
}

static Image *photomosaic(Image *image, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {
    const size_t pixel_cnt = image->columns * image->rows;
    unsigned char *pixels = malloc(pixel_cnt * 3);
    if(!ExportImagePixels(image, 0, 0, image->columns, image->rows, "RGB", CharPixel, pixels, exception)) {
        free(pixels);
        exit(1);
    }
    for(size_t i=0, j=0; i < pixel_cnt;) {
        /*Specifying 0 for y allows us to automatically use i to "roll over" into next row*/
        Pixel p = get_avg_color(pixels, image->columns, i, 0, each_width, each_height);
        unsigned char *new_pixels = malloc(each_width * each_height * 3);
        if(!get_closest_pixel(p, each_width, each_height, new_pixels, exception)) exit(1);
        for(size_t c=0; c < each_width*each_height;) {
            pixels[j] = new_pixels[c*3];
            pixels[j+1] = new_pixels[c*3+1];
            pixels[j+2] = new_pixels[c*3+2];
            j += 3;
            if(++c % each_width == 0)
                j += (image->columns - each_width) * 3; //next row ...
        }
        i += each_width; //next splotch
        /*If this row is done, skip over all the rows we just splotched*/
        if(i % image->columns == 0)
            i += image->columns * (each_height - 1);
        j = i * 3;
        free(new_pixels);
    }
    Image *new_image = ConstituteImage(image->columns, image->rows, "RGB", CharPixel, pixels, exception);
    free(pixels);
    if(!new_image)
        MagickError(exception->severity, exception->reason, exception->description);
    return new_image;
}

int usage() {
    return 0;
}

int main(int argc, char **argv) {
    ExceptionInfo *exception;
    Image *input_img, *output_img = NULL;
    char input_img_filename[400];
    input_img_filename[0] = 0;
    char output_img_filename[400];
    output_img_filename[0] = 0;
    ImageInfo *image_info, *new_image_info = NULL;
    float resize_factor = 0.0;
    bool prn_avg_color = false;
    bool dumb_shrink = false;
    bool prn_pixel_info = false;
    bool resize = false;
    bool splotch = false;
    bool mosaic = false;
    ssize_t x = 0, y = 0;
    size_t length = 1, width = 1;

    int opt;
    while((opt=getopt(argc, argv, "adhi:l:mno:Rr:sw:x:y:")) > -1) {
        switch(opt) {
        case 'a':
            prn_avg_color = true;
            break;
        case 'd':
            dumb_shrink = true;
            break;
        case 'h':
            return usage();
            break;
        case 'i':
            strcpy(input_img_filename, optarg);
            break;
        case 'l':
            if(!parse_ulong(optarg, &length))
                DIE(2, "FATAL: Argument \"%s\" to option -l could not be parsed to a long long int.\n", optarg);
            break;
        case 'm':
            mosaic = true;
            break;
        case 'n':
            prn_pixel_info = true;
            break;
        case 'o':
            strcpy(output_img_filename, optarg);
            break;
        case 'R':
            resize=true;
            break;
        case 'r':
            if(!parse_float(optarg, &resize_factor))
                DIE(2, "FATAL: Argument \"%s\" to option -r could not be parsed to a float.\n", optarg);
            // TODO implement a more robust maximum
            if(resize_factor < 0.01 || resize_factor > 10.0)
                DIE(2, "resize_factor %.1f is out of bounds. Should be greater than 0 and no more than 10.\n", resize_factor);
            resize = true;
            break;
        case 's':
            splotch = true;
            break;
        case 'w':
            if(!parse_ulong(optarg, &width))
                DIE(2, "FATAL: Argument \"%s\" to option -w could not be parsed to a long long int.\n", optarg);
            break;
        case 'x':
            if(!parse_long(optarg, &x))
                DIE(2, "FATAL: Argument \"%s\" to option -x could not be parsed to a long int.\n", optarg);
            break;
        case 'y':
            if(!parse_long(optarg, &y))
                DIE(2, "FATAL: Argument \"%s\" to option -x could not be parsed to a long int.\n", optarg);
            break;
        }
    }


    if(!(prn_avg_color ^ dumb_shrink ^ prn_pixel_info  ^ resize ^ splotch ^ mosaic))
        DIE(2, "FATAL: must specify exactly one of: -a, -d, -n, (-r|-R), -s, -m\n");
    if(strnlen(input_img_filename, 400) < 1)
        DIE(2, "FATAL: no input image specified.\n");
    if((resize || splotch) && strnlen(output_img_filename, 400) < 1)
        DIE(2, "FATAL: Must specify output image to resize or splotch.\n");
    if(prn_pixel_info || prn_avg_color)
        fprintf(stderr, "point: %zu, %zu\n", x, y);
    if(prn_avg_color || dumb_shrink || splotch || mosaic || (resize && resize_factor < 0.01))
        fprintf(stderr, "dimensions: %zu x %zu\n", width, length);

    MagickCoreGenesis(*argv, MagickTrue);
    exception = AcquireExceptionInfo();
    image_info = CloneImageInfo((ImageInfo *)NULL);
    strcpy(image_info->filename, input_img_filename);
    input_img = ReadImage(image_info, exception);
    if(exception->severity != UndefinedException)
        CatchException(exception);
    if(!input_img)
        return 1;

    if(resize) {
        if(resize_factor < 0.01)
            output_img = resize_image_to(input_img, width, length, exception);
        else
            output_img = resize_image_by_factor(input_img, resize_factor, exception);
    }
    else if(prn_pixel_info)
        print_pixel_info(input_img, x, y, exception);
    else if(prn_avg_color)
        print_avg_color(input_img, x, y, width, length, exception);
    else if(dumb_shrink)
        output_img = make_img_avg_colors(input_img, 0, 0, width, length, exception);
    else if(splotch)
        output_img = splotch_img(input_img, width, length, exception);
    else if(mosaic)
        output_img = photomosaic(input_img, width, length, exception);

    if(exception->severity != UndefinedException)
        CatchException(exception);

    if(cache) fclose(cache);
    DestroyImage(input_img);
    DestroyImageInfo(image_info);
    if(output_img) {
        new_image_info = CloneImageInfo((ImageInfo *)NULL);
        strcpy(output_img->filename, output_img_filename);
        WriteImage(new_image_info, output_img, exception);
        DestroyImage(output_img);
        DestroyImageInfo(new_image_info);
    }
    DestroyExceptionInfo(exception);
    MagickCoreTerminus();
    return 0;
}

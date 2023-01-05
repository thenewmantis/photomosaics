#include <assert.h>
#include <getopt.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <MagickCore/MagickCore.h>

#define DIE(rc, ...)    { fprintf(stderr, __VA_ARGS__); return rc; }
#define WARN(fmt, ...)    { fprintf(stderr, "WARN: "fmt, __VA_ARGS__); }

typedef struct {
    unsigned int r, g, b;
} Pixel;

static const char *cache_filename = "/home/wilson/.cache/photomosaics/avgs";
static FILE *cache = NULL;
static long cache_size = 0;
static time_t cache_mtime;
long *deletables;
long deletables_ind = 0;

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
static bool parse_hex_tou(char *str, unsigned int *out) {
    char *endptr;
    const char *old_locale = setlocale(LC_ALL, NULL);
    setlocale(LC_ALL|~LC_NUMERIC, "");
    *out = strtoul(str, &endptr, 16);
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

static Pixel hexstr_top(char *hs) {
    char rstr[3] = {hs[0], hs[1],};
    char gstr[3] = {hs[2], hs[3],};
    char bstr[3] = {hs[4], hs[5],};
    Pixel p;
    parse_hex_tou(rstr, &p.r);
    parse_hex_tou(gstr, &p.g);
    parse_hex_tou(bstr, &p.b);
    return p;
}

static long cache_grep(char *key) {
    if(cache_size < 0)
        return -1;
    if(!cache) {
        /* init cache */
        cache = fopen(cache_filename, "a+");
        struct stat tmp_st;
        if((!cache) || stat(cache_filename, &tmp_st)) {
            WARN("Couldn't open cache file '%s'. Please ensure the directory exists. Will stop attempting to cache for the remainder of execution.", cache_filename);
            cache_size = -1;
            return -1;
        }
        cache_mtime = tmp_st.st_mtim.tv_sec;
        fseek(cache, 0, SEEK_END);
        cache_size = ftell(cache);
        /* Just a guess, will realloc later if needed */
        deletables = malloc(50 * sizeof(long));
    }
    if(cache_size == 0)
        return -1;
    rewind(cache);
    char filename[150];
    struct stat file_st;
    long filename_pos = 0;
    for(int i=0, fn_ind=0; i < cache_size;) {
        // TODO handle this better in case EOF is an error
        if((filename[fn_ind]=fgetc(cache)) == EOF) return -1;
        i++;
        if(filename[fn_ind] == '\t') {
            filename[fn_ind] = 0;
            if(!strncmp(filename, key, fn_ind)) {
                //Already exists in cache
                assert(!stat(filename, &file_st));
                if(file_st.st_mtim.tv_sec < cache_mtime) {
                    /* Cache entry is up to date */
                    return filename_pos;
                }
                /* Not up to date. Caller will create a new cache entry,
                   then we will delete this line at the end of the program */
                if(deletables_ind > 49) {
                    deletables = realloc(deletables, (deletables_ind + 1) * sizeof(deletables[0]));
                    assert(deletables != NULL);
                }
                deletables[deletables_ind++] = filename_pos;
                return -1;
            }
            fn_ind = 0;
            for(char tmp=fgetc(cache); i < cache_size && tmp != '\n'; i++) {
                if(tmp == EOF) return -1;
                tmp = fgetc(cache);
            }
            /* Capture beginning of this line, in case this is the file in question */
            filename_pos = ftell(cache);
        }
        else fn_ind++;
    }
    return -1;
}
static bool cache_fetch_pixel(char *key, Pixel *value) {
    long filename_pos = cache_grep(key);
    if(filename_pos < 0) return false;
    char hexstr[7];
    assert(fgets(hexstr, 7, cache) && strlen(hexstr) == 6);
    *value = hexstr_top(hexstr);
    return true;
}
static bool cache_put_pixel(char *key, Pixel value) {
    if(!cache) return false;
    fseek(cache, 0, SEEK_END);
    fprintf(cache, "%s\t%02x%02x%02x\n", key, value.r, value.g, value.b); 
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
    assert(ExportImagePixels(image, x, y, width, height, "RGB", CharPixel, pixels, exception));
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
    assert(new_image);
    return new_image;
}

static Image *splotch_img(Image *image, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {
    const size_t pixel_cnt = image->columns * image->rows;
    unsigned char *pixels = malloc(pixel_cnt * 3);
    assert(ExportImagePixels(image, 0, 0, image->columns, image->rows, "RGB", CharPixel, pixels, exception));
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
    fread(buf, 1, MY_AVGS_SIZE, avgs_list);
    fclose(avgs_list);
    char *filename = strtok(buf, " ");
    char *closest_file = malloc(150);
    float distance_of_closest = sqrtf(powf(0xff, 2) * 3); //max diff value
    do {
        //TODO test after changes
        Pixel tmp = hexstr_top(strtok(NULL, "\n"));
        long rdiff = (long)tmp.r - p.r;
        long gdiff = (long)tmp.g - p.g;
        long bdiff = (long)tmp.b - p.b;
        float new_distance = sqrtf(powf(rdiff, 2) + powf(gdiff, 2) + powf(bdiff, 2));
        if(new_distance < distance_of_closest) {
            distance_of_closest = new_distance;
            closest_file[0] = 0;
            strncat(closest_file, filename, 149);
        }
    } while((filename=strtok(NULL, " ")) && filename < buf + MY_AVGS_SIZE);

    size_t len = strnlen(closest_file, 150);
    if(len < 1 || len >= 150)
        return false;


    closest_file[len-1] = 0; //Get rid of the colon
    ImageInfo *image_info = CloneImageInfo((ImageInfo *)NULL);
    strcpy(image_info->filename, closest_file);
    free(closest_file);
    Image *src_img = ReadImage(image_info, exception);
    Image *src_img_r = resize_image_to(src_img, width, height, exception);
    ExportImagePixels(src_img_r, 0, 0, width, height, "RGB", CharPixel, pixels_out, exception);
    DestroyImage(src_img);
    DestroyImage(src_img_r);
    DestroyImageInfo(image_info);
    return true;
}

static unsigned char *get_img_with_closest_avg(const size_t width, const size_t height, ExceptionInfo *exception) {
    Pixel img_avgs[105];
    FILE *f = popen("find $(find ~/pics -type d | grep -vE 'redacted|not_real') -maxdepth 1 -type f -print0", "r");
#define IMG_LIST_SIZE 5091
    char buf[IMG_LIST_SIZE];
    fread(buf, 1, IMG_LIST_SIZE, f);
    assert(!pclose(f));
    for(int c=0, k=0; c < IMG_LIST_SIZE; k++) {
        if(!cache_fetch_pixel(&buf[c], &img_avgs[k])) {
            ImageInfo *image_info = CloneImageInfo((ImageInfo *)NULL);
            image_info->filename[0] = 0;
            strncat(image_info->filename, &buf[c], IMG_LIST_SIZE - c - 1);
            Image *src_img = ReadImage(image_info, exception);
            Image *src_img_r = resize_image_to(src_img, width, height, exception);
            unsigned char *pixels = malloc(width * height * 3);
            ExportImagePixels(src_img_r, 0, 0, width, height, "RGB", CharPixel, pixels, exception);
            img_avgs[k] = get_avg_color(pixels, width, 0, 0, width, height);
            free(pixels);
            assert(cache_put_pixel(&buf[c], img_avgs[k]));
            DestroyImage(src_img);
            DestroyImage(src_img_r);
            DestroyImageInfo(image_info);
        }
        c += strnlen(&buf[c], IMG_LIST_SIZE - c) + 1;
    }
    return NULL;
}

static Image *photomosaic(Image *image, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {
    const size_t pixel_cnt = image->columns * image->rows;
    unsigned char *pixels = malloc(pixel_cnt * 3);
    assert(ExportImagePixels(image, 0, 0, image->columns, image->rows, "RGB", CharPixel, pixels, exception));
    for(size_t i=0, j=0; i < pixel_cnt;) {
        /*Specifying 0 for y allows us to automatically use i to "roll over" into next row*/
        Pixel p = get_avg_color(pixels, image->columns, i, 0, each_width, each_height);
        unsigned char *new_pixels = malloc(each_width * each_height * 3);
        assert(get_closest_pixel(p, each_width, each_height, new_pixels, exception));
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

    if(cache) {
        fclose(cache);
        char line[150];
        size_t len = strlen(cache_filename);
        char *new_cache_name = malloc(len + 2);
        strcpy(new_cache_name, cache_filename);
        new_cache_name[len] = '2';
        new_cache_name[len+1] = 0;
        cache = fopen(cache_filename, "r");
        FILE *new_cache = fopen(new_cache_name, "w");
        if(!cache || !new_cache) {
            if(!cache) {
                WARN("Failed to reopen the cache file '%s' for reading "
                    "in order to update the cache properly.", cache_filename);
            }
            else {
                WARN("Failed to open file '%s' in order to update the cache properly.",
                    new_cache_name);
            }
            WARN("The cache at '%s' may now contain duplicate entries.", cache_filename);
        }
        else while(1) {
            long pos = ftell(cache);
            if(!fgets(line, 150, cache)) break;
            bool keep = true;
            for(int i=0; i < deletables_ind; i++) {
                if(pos == deletables[i]) {
                    keep = false;
                    break;
                }
            }
            if(keep) fputs(line, new_cache);
        }
        free(deletables);
        fclose(cache);
        fclose(new_cache);
        // TODO troubleshoot this rename
        if(rename(new_cache_name, cache_filename))
            WARN("Failed to overwrite cache file '%s'."
                "The cache may now contain duplicate entries.", cache_filename);
        free(new_cache_name);
    }
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

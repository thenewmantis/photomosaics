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
#include <unistd.h>
#include <MagickCore/MagickCore.h>

#define DIE(rc, ...)    { fprintf(stderr, __VA_ARGS__); return rc; }
#define WARN(fmt, ...)    { fprintf(stderr, "WARN: "fmt, __VA_ARGS__); }

typedef struct {
    unsigned int r, g, b;
} Pixel;

#define MAX_FN_LEN 150
#define IMG_LIST_MAX_SIZE 5091

static const char *cache_filename = "/home/wilson/.cache/photomosaics/avgs";
static FILE *cache = NULL;
static long cache_size = 0;
static time_t cache_mtime;
long *deletables;
size_t deletables_ind = 0;
char temp_dirname[] = "/tmp/photomosaics-XXXXXX";
char **inner_cache_tmp_files;
char **files_inner_cached = NULL;
size_t files_inner_cached_ind = 0;

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
    char filename[MAX_FN_LEN];
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
                    assert(deletables);
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

static bool get_resized_pixel_info(char *filename, const size_t width, const size_t height, unsigned char *pixels_out, ExceptionInfo *exception) {
    if(!files_inner_cached) {
        inner_cache_tmp_files = malloc(IMG_LIST_MAX_SIZE * sizeof(char*));
        files_inner_cached = malloc(IMG_LIST_MAX_SIZE * sizeof(char*));
    }
    const size_t pixels_arr_size = width * height * 3;
    bool file_is_cached = false;
    size_t i;
    for(i=0; i < files_inner_cached_ind; i++) {
        if(!strcmp(files_inner_cached[i], filename)) {
            file_is_cached = true;
            break;
        }
    }

    if(file_is_cached) {
        FILE *inner_cache = fopen(inner_cache_tmp_files[i], "rb");
        assert(inner_cache);
        size_t z = fread(pixels_out, 1, pixels_arr_size, inner_cache);
        fclose(inner_cache);
        return z == pixels_arr_size;
    }
    else {
        /* TODO delete tempdir at end of execution */
        if(files_inner_cached_ind == 0)
            mkdtemp(temp_dirname);
        const size_t filename_len = strlen(filename);
        const size_t dirname_len = strlen(temp_dirname);
        char *temp_name = malloc(filename_len);
        char *temp_path = malloc(filename_len + dirname_len + 2);
        temp_name[0] = 0;
        strncat(temp_name, filename, filename_len);
        /* TODO gracefully handle slashes (?) and percents in the filenames themselves */
        for(size_t c=0; c < filename_len; ++c) if(temp_name[c] == '/') temp_name[c] = '%';
        temp_path[0] = 0;
        strncat(temp_path, temp_dirname, dirname_len);
        temp_path[dirname_len] = '/';
        temp_path[dirname_len+1] = 0;
        strncat(temp_path, temp_name, filename_len);
        free(temp_name);

        FILE *inner_cache = fopen(temp_path, "wb");
        assert(inner_cache);
        ImageInfo *image_info = CloneImageInfo((ImageInfo *)NULL);
        image_info->filename[0] = 0;
        strncat(image_info->filename, filename, filename_len);
        Image *src_img = ReadImage(image_info, exception);
        Image *src_img_r = resize_image_to(src_img, width, height, exception);
        ExportImagePixels(src_img_r, 0, 0, width, height, "RGB", CharPixel, pixels_out, exception);
        if(exception->severity != UndefinedException) CatchException(exception);
        DestroyImage(src_img);
        DestroyImage(src_img_r);
        DestroyImageInfo(image_info);
        assert(fwrite(pixels_out, 3, pixels_arr_size / 3, inner_cache) == pixels_arr_size / 3);
        fclose(inner_cache);
        inner_cache_tmp_files[files_inner_cached_ind] = malloc(strlen(temp_path) + 1);
        strcpy(inner_cache_tmp_files[files_inner_cached_ind], temp_path);
        files_inner_cached[files_inner_cached_ind] = malloc(strlen(filename) + 1);
        files_inner_cached[files_inner_cached_ind][0] = 0;
        strncat(files_inner_cached[files_inner_cached_ind++], filename, filename_len);
        return true;
    }
}

static unsigned char *get_img_with_closest_avg(char *img_list, size_t img_list_size, Pixel p, const size_t width, const size_t height, ExceptionInfo *exception) {
    const size_t pixels_arr_size = width * height * 3;
    unsigned char *pixels_of_closest = malloc(pixels_arr_size);
    unsigned char *pixels = malloc(pixels_arr_size);
    float distance_of_closest = sqrtf(powf(0xff, 2) * 3); //max diff value
    bool test_pxofcls_populated = false;

    for(size_t c=0; c < img_list_size;) {
        Pixel avg;
        bool fetched_avg_from_cache = cache_fetch_pixel(&img_list[c], &avg);
        if(!fetched_avg_from_cache) {
            assert(get_resized_pixel_info(&img_list[c], width, height, pixels, exception));
            avg = get_avg_color(pixels, width, 0, 0, width, height);
            assert(cache_put_pixel(&img_list[c], avg));
        }
        long rdiff = (long)avg.r - p.r;
        long gdiff = (long)avg.g - p.g;
        long bdiff = (long)avg.b - p.b;
        float new_distance = sqrtf(powf(rdiff, 2) + powf(gdiff, 2) + powf(bdiff, 2));
        if(new_distance < distance_of_closest) {
            distance_of_closest = new_distance;
            if(fetched_avg_from_cache)
                assert(get_resized_pixel_info(&img_list[c], width, height, pixels, exception));
            // For now, return any perfect match
            if(new_distance < 0.01f) {
               free(pixels_of_closest);
               return pixels;
            }
            memcpy(pixels_of_closest, pixels, pixels_arr_size);
            test_pxofcls_populated = true;
        }
        c += strnlen(&img_list[c], img_list_size - c) + 1;
    }
    assert(test_pxofcls_populated);
    free(pixels);
    return pixels_of_closest;
}

static Image *photomosaic(Image *image, const size_t each_width, const size_t each_height, ExceptionInfo *exception) {
    const size_t pixel_cnt = image->columns * image->rows;
    unsigned char *pixels = malloc(pixel_cnt * 3);
    FILE *f = popen("find $(find ~/pics -type d | grep -vE 'redacted|not_real') -maxdepth 1 -type f -print0", "r");
    char buf[IMG_LIST_MAX_SIZE];
    size_t bytes_read = fread(buf, 1, IMG_LIST_MAX_SIZE, f);
    assert(!pclose(f));

    assert(ExportImagePixels(image, 0, 0, image->columns, image->rows, "RGB", CharPixel, pixels, exception));

    for(size_t i=0, j=0; i < pixel_cnt;) {
        /*Specifying 0 for y allows us to automatically use i to "roll over" into next row*/
        Pixel p = get_avg_color(pixels, image->columns, i, 0, each_width, each_height);
        unsigned char *new_pixels = get_img_with_closest_avg(buf, bytes_read, p, each_width, each_height, exception);
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

    if(prn_avg_color)
        print_avg_color(input_img, x, y, width, length, exception);
    else if(dumb_shrink)
        output_img = make_img_avg_colors(input_img, 0, 0, width, length, exception);
    else if(prn_pixel_info)
        print_pixel_info(input_img, x, y, exception);
    else if(resize) {
        if(resize_factor < 0.01)
            output_img = resize_image_to(input_img, width, length, exception);
        else
            output_img = resize_image_by_factor(input_img, resize_factor, exception);
    }
    else if(splotch)
        output_img = splotch_img(input_img, width, length, exception);
    else if(mosaic)
        output_img = photomosaic(input_img, width, length, exception);

    if(exception->severity != UndefinedException)
        CatchException(exception);

    if(files_inner_cached) {
        for(size_t i=0; i < files_inner_cached_ind; i++) {
            free(inner_cache_tmp_files[i]);
            free(files_inner_cached[i]);
        }
        free(inner_cache_tmp_files);
        free(files_inner_cached);
    }
    if(cache) {
        fclose(cache);
        char line[MAX_FN_LEN];
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
            if(!fgets(line, MAX_FN_LEN, cache)) break;
            bool keep = true;
            for(size_t i=0; i < deletables_ind; i++) {
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

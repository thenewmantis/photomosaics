#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
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

#define DIE(rc, fmt, ...)               { fprintf(stderr, "FATAL: "fmt"\n", __VA_ARGS__); exit(rc); }
#define WARN(fmt, ...)                  { fprintf(stderr, "WARN: " fmt"\n", __VA_ARGS__); }
#define assert_error(expression, s)     if(!expression) { perror(s); abort(); }

typedef struct {
    unsigned int r, g, b;
} Pixel;
typedef enum { L, UL, XU, XUL, F } NUM_TYPES;

#define MAX_FN_LEN 150
#define IMG_LIST_MAX_SIZE 5091

static const char *cache_filename = "/home/wilson/.cache/photomosaics/avgs";
static char *cache_buf = NULL;
static size_t cache_max_size;
static ssize_t initial_cache_size = 1;
static ssize_t cache_size = 0;
static time_t cache_mtime;
static long *deletables;
static size_t deletables_ind = 0;
static char temp_dirname[] = "/tmp/photomosaics-XXXXXX";
static char **inner_cache_tmp_files;
static char **files_inner_cached = NULL;
static size_t files_inner_cached_ind = 0;

static void try(int exit_code, char *function_name) {
    if(exit_code != 0) perror(function_name);
}
static size_t slen(const char *s, size_t maxlen) {
    char *pos = memchr(s, '\0', maxlen);
    return pos ? (size_t)(pos - s) : maxlen;
}
static size_t indof(const char *s, char ch, size_t maxlen) {
    char *pos = memchr(s, ch, maxlen);
    return pos ? (size_t)(pos - s) : maxlen;
}

static bool parse_num(const char *str, NUM_TYPES type, void *out) {
    char *endptr;
    const char *old_locale = setlocale(LC_ALL, NULL);
    setlocale(LC_ALL|~LC_NUMERIC, "");
    errno = 0;
    int my_errno = 0;

    switch(type) {
    case L:
        *((long *)out) = strtol(str, &endptr, 10);
        break;
    case UL:
        *((unsigned long *)out) = strtoul(str, &endptr, 10);
        break;
    case XU: {
            unsigned long tmp = strtoul(str, &endptr, 16);
            if(tmp > UINT_MAX) my_errno = ERANGE;
            else *((unsigned int *)out) = tmp;
        }
        break;
    case XUL:
        *((unsigned long *)out) = strtoul(str, &endptr, 16);
        break;
    case F:
        *((float *)out) = strtof(str, &endptr);
        break;
    }

    setlocale(LC_ALL, old_locale);
    if(errno) return false;
    if(my_errno) { 
        errno = my_errno;
        return false;
    }
    /*N.B. fails on "partial" conversions or if str is empty*/
    return *str != '\0' && *endptr == '\0';
}
/*static bool parse_float(char *str, float *out) {*/
/*    return parse_num(str, F, out);*/
/*}*/
/*static bool parse_long(char *str, long *out) {*/
/*    return parse_num(str, L, out);*/
/*}*/
static bool parse_hex_tou(char *str, unsigned int *out) {
    return parse_num(str, XU, out);
}
static bool parse_ulong(char *str, unsigned long *out) {
    return parse_num(str, UL, out);
}

static Pixel hexstr_top(const char *hs) {
    char rstr[3] = {hs[0], hs[1],};
    char gstr[3] = {hs[2], hs[3],};
    char bstr[3] = {hs[4], hs[5],};
    Pixel p;
    parse_hex_tou(rstr, &p.r);
    parse_hex_tou(gstr, &p.g);
    parse_hex_tou(bstr, &p.b);
    return p;
}

static ssize_t cache_grep(char *key) {
    if(cache_size == -1)
        return -1;
    if(!cache_buf) {
        /* init cache */
        errno = 0;
        FILE *cache_file = fopen(cache_filename, "r");
        struct stat cache_st;
        if(!cache_file) {
            WARN("Couldn't open cache file '%s'. "
                "Please ensure the directory exists.", cache_filename);
            perror("fopen");
        }
        else {
            if(stat(cache_filename, &cache_st) == 0)
                errno = 0;
            else if(errno == ENOENT) {
                /* create the file and try again just in case that's the only error */
                errno = 0;
                FILE *tmp_cache_file = fopen(cache_filename, "a");
                if(!tmp_cache_file) {
                    WARN("Couldn't open cache file '%s'. "
                        "Please ensure the directory exists.", cache_filename);
                    perror("fopen");
                }
                else {
                    try(fclose(tmp_cache_file), "fclose");
                    if(stat(cache_filename, &cache_st) != 0) {
                        WARN("Could not stat cache file '%s'", cache_filename);
                        perror("stat");
                    }
                }
            }
        }

        if(errno) {
            WARN("Will stop attempting to cache to '%s' for the remainder of execution.", cache_filename);
            cache_size = -1;
            return -1;
        }

        /* No errors, proceed to populate cache_buf */

        cache_mtime = cache_st.st_mtime;
        long cache_file_size = cache_st.st_size;
        /* The following 2 mallocs are guesses; will realloc later if needed */
        cache_max_size = (cache_file_size < 5822 ? 5822 : cache_file_size) + 5 * MAX_FN_LEN;
        cache_buf = malloc(cache_max_size);
        deletables = malloc(50 * sizeof(long));
        initial_cache_size = cache_size = fread(cache_buf, 1, cache_file_size, cache_file);
        cache_buf[cache_size] = 0; /* For the initial strncat later */

        assert(cache_size == cache_file_size);
        try(fclose(cache_file), "fclose");
    }

    if(cache_size == 0) return -1;

    char filename[MAX_FN_LEN];
    struct stat file_st;

    for(ssize_t i=0; i < cache_size; i += indof(cache_buf + i, '\n', cache_size - i) + 1) {
        /* If we already marked it for deletion, we want the image's cache entry which
           we put at the bottom of the buffer, in case the avg color has changed. */
        bool skip = false;
        for(size_t j=0; j < deletables_ind; j++) {
            if(deletables[j] == i) {
                skip = true;
                break;
            }
        }
        if(skip) continue;
        size_t fn_len = 0;
        size_t fn_begin = i;
        for(; i < cache_size; i++) {
            if((filename[i-fn_begin]=cache_buf[i]) == '\t') {
                fn_len = i - fn_begin;
                filename[fn_len] = '\0';
                i++;
                break;
            }
        }
        assert(fn_len);
        if(!strncmp(filename, key, fn_len)) {
            //Already exists in cache
            try(stat(filename, &file_st), "stat");
            //The sole use of `initial_...`. Prevents the caller from re- and recaching newly-added files
            if(i > initial_cache_size - 1 || file_st.st_mtime < cache_mtime) {
                /* Cache entry is up to date */
                return i;
            }
            /* Not up to date. Caller will create a new cache entry,
               then we will delete this line at the end of the program */
            if(deletables_ind > 49) {
                deletables = realloc(deletables, (deletables_ind + 1) * sizeof(deletables[0]));
                assert_error(deletables, "realloc");
            }
            deletables[deletables_ind++] = fn_begin;
            return -1;
        }
    }
    return -1;
}
static bool cache_fetch(char *key, Pixel *value) {
    ssize_t i = cache_grep(key);
    if(i == -1) return false;
    char hexstr[7];
    assert(indof(cache_buf + i, '\n', cache_size - i) == 6);
    hexstr[0] = 0;
    strncat(hexstr, cache_buf + i, 6);
    *value = hexstr_top(hexstr);
    return true;
}
static bool cache_put(char *key, Pixel value) {
    if(!cache_buf) return false;
    char entry[MAX_FN_LEN + 9];
    int entry_length = sprintf(entry, "%s\t%02x%02x%02x\n", key, value.r, value.g, value.b);
    size_t new_size_of_cache = cache_size + entry_length + 1;
    if(new_size_of_cache > cache_max_size) {
        cache_buf = realloc(cache_buf, new_size_of_cache);
        assert_error(cache_buf, "realloc");
        cache_max_size = new_size_of_cache;
    }
    strncat(cache_buf, entry, entry_length);
    cache_size = new_size_of_cache - 1;
    return true;
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
        assert_error(inner_cache, "fopen");
        size_t z = fread(pixels_out, 1, pixels_arr_size, inner_cache);
        fclose(inner_cache);
        return z == pixels_arr_size;
    }
    else {
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
        assert_error(inner_cache, "fopen");
        ImageInfo *image_info = CloneImageInfo((ImageInfo *)NULL);
        image_info->filename[0] = 0;
        strncat(image_info->filename, filename, filename_len);
        Image *src_img = ReadImage(image_info, exception);
        Image *src_img_r = ResizeImage(src_img, width, height, LanczosFilter, exception);

        if(!src_img_r) MagickError(exception->severity, exception->reason, exception->description);
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
        free(temp_path);
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
        bool fetched_avg_from_cache = cache_fetch(&img_list[c], &avg);
        if(!fetched_avg_from_cache) {
            assert(get_resized_pixel_info(&img_list[c], width, height, pixels, exception));
            avg = get_avg_color(pixels, width, 0, 0, width, height);
            assert(cache_put(&img_list[c], avg));
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
        c += slen(&img_list[c], img_list_size - c) + 1;
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
    try(pclose(f), "pclose");

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

void usage(char *progname) {
    fprintf(stderr,
        "Usage: %s (-h | (-i <input_file> -o <output_file> -w <width> -l <length>))\n"
        "\t-h\tPrint this help message and exit.\n"
        "\tThis program creates a photomosaic by replacing each block\n\t"
        "of 'input_file' of size 'width' x 'length' by the resized\n\t"
        "version of some image with a similar average color.\n\t"
        "Writes the new image to the filename specified by 'output_file'.\n"
        "\n\nExit status:\n"
        "\t0\tSpecified operation succeeded\n"
        "\t1\tError reading or performing some operation on an image\n"
        "\t2\tError parsing command line arguments\n"
        , progname);
}

int main(int argc, char **argv) {
    ExceptionInfo *exception;
    Image *input_img, *output_img = NULL;
    const size_t max_fn_len = 400;
    char input_img_filename[max_fn_len];
    input_img_filename[0] = 0;
    char output_img_filename[max_fn_len];
    output_img_filename[0] = 0;
    ImageInfo *image_info, *new_image_info = NULL;
    size_t length = 1, width = 1;

    int opt;
    while((opt=getopt(argc, argv, "hi:o:l:w:")) > -1) {
        switch(opt) {
        case 'h':
            usage(argv[0]);
            return 0;
        case 'i':
            if(slen(optarg, max_fn_len) == max_fn_len) DIE(2, "Argument \"%s\" to option -i should be less than %zu characters.", optarg, max_fn_len)
            strncat(input_img_filename, optarg, max_fn_len - 1);
            break;
        case 'l':
            if(!parse_ulong(optarg, &length))
                DIE(2, "Argument \"%s\" to option -l could not be parsed to an unsigned long int.", optarg);
            break;
        case 'o':
            if(slen(optarg, max_fn_len) == max_fn_len) DIE(2, "Argument \"%s\" to option -o should be less than %zu characters.", optarg, max_fn_len)
            strncat(output_img_filename, optarg, max_fn_len - 1);
            break;
        case 'w':
            if(!parse_ulong(optarg, &width))
                DIE(2, "Argument \"%s\" to option -w could not be parsed to an unsigned long int.", optarg);
            break;
        }
    }

    if(slen(input_img_filename, max_fn_len) < 1)  DIE(2, "No input image specified.%s", "");
    if(slen(output_img_filename, max_fn_len) < 1) DIE(2, "No output image specified.%s", "");

    MagickCoreGenesis(*argv, MagickTrue);
    exception = AcquireExceptionInfo();

    image_info = CloneImageInfo((ImageInfo *)NULL);
    strcpy(image_info->filename, input_img_filename);
    input_img = ReadImage(image_info, exception);
    if(exception->severity != UndefinedException)
        CatchException(exception);
    if(!input_img)
        DIE(1, "Input image %s could not be read.", input_img_filename);

    output_img = photomosaic(input_img, width, length, exception);

    if(exception->severity != UndefinedException)
        CatchException(exception);


    /* Teardown */
    if(files_inner_cached) {
        for(size_t i=0; i < files_inner_cached_ind; i++) {
            try(remove(inner_cache_tmp_files[i]), "remove");
            free(inner_cache_tmp_files[i]);
            free(files_inner_cached[i]);
        }
        try(remove(temp_dirname), "remove");
    }
    if(cache_buf) {
        FILE *cache = fopen(cache_filename, "w");
        if(!cache) {
            WARN("Failed to reopen the cache file '%s' for writing "
                "in order to update the cache properly:", cache_filename);
            perror("fopen");
            WARN("The cache at '%s' may now contain duplicate entries.", cache_filename);
        }
        else for(ssize_t i=0; i < cache_size;) {
            bool keep = true;
            for(size_t j=0; j < deletables_ind; j++) {
                if(deletables[j] == i) {
                    keep = false;
                    break;
                }
            }
            size_t line_len = indof(cache_buf + i, '\n', cache_size - i);
            if(keep) for(size_t j=0; j <= line_len; j++) assert(fputc(cache_buf[i+j], cache) != EOF);
            i += line_len + 1;
        }
        if(cache) fclose(cache);
        free(deletables);
        free(cache_buf);
    }

    if(output_img) {
        new_image_info = CloneImageInfo((ImageInfo *)NULL);
        strcpy(output_img->filename, output_img_filename);
        WriteImage(new_image_info, output_img, exception);
        DestroyImage(output_img);
        DestroyImageInfo(new_image_info);
    }
    DestroyImage(input_img);
    DestroyImageInfo(image_info);
    DestroyExceptionInfo(exception);
    MagickCoreTerminus();
    return 0;
}

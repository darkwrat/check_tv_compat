/*
 * Samsung Frame 2024 TV Video File Checker
 * ----------------------------------------
 * Checks if video/audio/subtitle streams and containers are supported by Samsung Frame 2024 TV.
 * Suggests ffmpeg remuxing or transcoding commands for unsupported files.
 *
 * Usage:
 *   check_tv_compat <file-or-directory> [--exclude dir1 ...] [--fullpath] [--brief] [--skip-ok] [--skip-unfixable]
 *
 * Limitations:
 *   - Supported codec/container lists are based on public Samsung documentation, but may not be exhaustive.
 *   - POSIX only (uses dirent.h, unistd.h, etc).
 *   - Requires FFmpeg development libraries.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fnmatch.h>

#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET  "\033[0m"

#define PATH_BUF_SIZE 4096

typedef struct {
    int total;
    int ok;
    int not_supported;
    int errors;
} Summary;

int brief_mode = 0;
int skip_ok = 0;
int skip_unfixable = 0;

void quiet_ffmpeg_log(void *ptr, int level, const char *fmt, va_list vl) {
    (void)ptr; (void)level; (void)fmt; (void)vl;
}

int is_video_codec_supported(AVCodecParameters *par) {
    enum AVCodecID id = par->codec_id;

    return id == AV_CODEC_ID_H264 ||
           id == AV_CODEC_ID_HEVC ||
           id == AV_CODEC_ID_MPEG2VIDEO ||
           id == AV_CODEC_ID_VP9 ||
           id == AV_CODEC_ID_AV1 ||
           id == AV_CODEC_ID_MJPEG ||
           id == AV_CODEC_ID_PNG ||
           (id == AV_CODEC_ID_MPEG4 &&
            par->codec_tag != MKTAG('X','V','I','D') &&
            par->codec_tag != MKTAG('x','v','i','d') &&
            par->codec_tag != MKTAG('D','I','V','X') &&
            par->codec_tag != MKTAG('d','i','v','x') &&
            par->codec_tag != MKTAG('D','X','5','0') &&
            par->codec_tag != MKTAG('M','P','4','V') &&
            par->codec_tag != MKTAG('m','p','4','v') &&
            par->codec_tag != MKTAG('F','M','P','4') &&
            par->codec_tag != MKTAG('f','m','p','4') &&
            !(par->profile == FF_PROFILE_MPEG4_ADVANCED_SIMPLE ||
              par->profile == FF_PROFILE_MPEG4_SIMPLE_STUDIO)
           );
}

int is_audio_codec_supported(enum AVCodecID id) {
    return id == AV_CODEC_ID_AAC ||
           id == AV_CODEC_ID_AC3 ||
           id == AV_CODEC_ID_EAC3 ||
           id == AV_CODEC_ID_MP3 ||
           id == AV_CODEC_ID_PCM_S16LE ||
           id == AV_CODEC_ID_FLAC ||
           id == AV_CODEC_ID_VORBIS ||
           id == AV_CODEC_ID_OPUS ||
           id == AV_CODEC_ID_WMAV2;
}

int is_container_supported(const char *format_name) {
    return format_name &&
           (strstr(format_name, "matroska") || // MKV
            strstr(format_name, "mp4") ||
            strstr(format_name, "mov") ||
            strstr(format_name, "mpegts") ||
            strstr(format_name, "webm") ||
            strstr(format_name, "avi") ||
            strstr(format_name, "asf") ||
            strstr(format_name, "wav") ||
            strstr(format_name, "flac") ||
            strstr(format_name, "mp3") ||
            strstr(format_name, "ogg") ||
            strstr(format_name, "wmv"));
}

int is_subtitle_codec_supported(enum AVCodecID id) {
    return id == AV_CODEC_ID_SUBRIP ||   // .srt
           id == AV_CODEC_ID_ASS ||      // .ass
           id == AV_CODEC_ID_SSA ||      // .ssa
           id == AV_CODEC_ID_WEBVTT ||   // .vtt
           id == AV_CODEC_ID_MOV_TEXT || // .movtext
           id == AV_CODEC_ID_MICRODVD || // .sub
           id == AV_CODEC_ID_TEXT;
}

int is_text_subtitle(enum AVCodecID id) {
    return id == AV_CODEC_ID_SUBRIP ||
           id == AV_CODEC_ID_ASS ||
           id == AV_CODEC_ID_SSA ||
           id == AV_CODEC_ID_WEBVTT ||
           id == AV_CODEC_ID_MOV_TEXT ||
           id == AV_CODEC_ID_MICRODVD ||
           id == AV_CODEC_ID_TEXT;
}

int is_bitmap_subtitle(enum AVCodecID id) {
    return id == AV_CODEC_ID_HDMV_PGS_SUBTITLE ||
           id == AV_CODEC_ID_DVD_SUBTITLE;
}

int has_supported_extension(const char *filename) {
    static const char *exts[] = {
        ".mkv", ".mp4", ".mov", ".webm", ".avi"
    };
    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;
    for (size_t i = 0; i < sizeof(exts)/sizeof(exts[0]); ++i) {
        if (strcasecmp(dot, exts[i]) == 0)
            return 1;
    }
    return 0;
}

const char *get_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void print_ffmpeg_error(const char *prefix, int errnum) {
    char errbuf[256];
    av_strerror(errnum, errbuf, sizeof(errbuf));
    printf("%s: " COLOR_YELLOW "error: %s" COLOR_RESET "\n", prefix, errbuf);
}

int is_media_stream(enum AVMediaType type) {
    return type == AVMEDIA_TYPE_VIDEO ||
           type == AVMEDIA_TYPE_AUDIO ||
           type == AVMEDIA_TYPE_SUBTITLE;
}

// Returns a newly allocated string with shell-safe single-quote escaping
char *shell_escape_single(const char *input) {
    size_t len = strlen(input);
    // Worst case: every character is a single quote, so 4x size + 3 for outer quotes + 1 for null
    char *out = malloc(len * 4 + 4);
    char *p = out;
    *p++ = '\'';
    for (; *input; input++) {
        if (*input == '\'') {
            strcpy(p, "'\\''");
            p += 4;
        } else {
            *p++ = *input;
        }
    }
    *p++ = '\'';
    *p = '\0';
    return out;
}

void check_file(const char *filepath, int show_full_path, Summary *summary) {
    AVFormatContext *fmt_ctx = NULL;
    int ret, i;
    int has_unsupported = 0;
    char line[8192] = {0}; // For brief output line
    size_t linelen = 0;
    const char *filename = show_full_path ? filepath : get_basename(filepath);

    if (!has_supported_extension(filepath))
        return;

    if ((ret = avformat_open_input(&fmt_ctx, filepath, NULL, NULL)) < 0) {
        if (!brief_mode)
            print_ffmpeg_error(filename, ret);
        else
            printf("%s: " COLOR_YELLOW "error: could not open (%d)\n" COLOR_RESET, filename, ret);
        summary->errors++;
        return;
    }
    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        if (!brief_mode)
            print_ffmpeg_error(filename, ret);
        else
            printf("%s: " COLOR_YELLOW "error: could not read stream info (%d)\n" COLOR_RESET, filename, ret);
        avformat_close_input(&fmt_ctx);
        summary->errors++;
        return;
    }

    const char *container = (fmt_ctx->iformat && fmt_ctx->iformat->name) ? fmt_ctx->iformat->name : "unknown";
    int container_ok = is_container_supported(container);

    if (brief_mode) {
        // Brief output: one line per file, all tracks, color-coded
        if (!container_ok) {
            linelen += snprintf(line + linelen, sizeof(line) - linelen, COLOR_RED "[container:%s]" COLOR_RESET, container);
            has_unsupported = 1;
        }
        for (i = 0; i < fmt_ctx->nb_streams; i++) {
            AVStream *st = fmt_ctx->streams[i];
            AVCodecParameters *par = st->codecpar;
            if (!is_media_stream(par->codec_type)) continue;
            const char *lang = NULL;
            AVDictionaryEntry *tag = av_dict_get(st->metadata, "language", NULL, 0);
            if (tag) lang = tag->value;
            else lang = "und";
            const char *codec = avcodec_get_name(par->codec_id);
            int supported = 1;
            const char *type = NULL;

            if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                type = "video";
                supported = is_video_codec_supported(par);
            } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
                type = "audio";
                supported = is_audio_codec_supported(par->codec_id);
            } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                type = "subtitle";
                supported = is_subtitle_codec_supported(par->codec_id);
            }

            if (!supported) has_unsupported = 1;
            linelen += snprintf(line + linelen, sizeof(line) - linelen,
                "%s[%d:%s:%s:%s]%s",
                supported ? COLOR_GREEN : COLOR_RED,
                i, type, codec, lang,
                COLOR_RESET
            );
        }
        avformat_close_input(&fmt_ctx);

        if (has_unsupported) {
            printf("%s:%s\n", filename, line);
            summary->not_supported++;
        } else {
            summary->ok++;
        }
        summary->total++;
        return;
    }

    // Verbose/tree output
    int all_supported = container_ok;
    int has_video = 0, has_audio = 0;
    int can_transcode = 0;
    int has_unsupported_bitmap_subtitle = 0;
    // First pass: determine if all supported, and count real streams
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *st = fmt_ctx->streams[i];
        AVCodecParameters *par = st->codecpar;
        if (!is_media_stream(par->codec_type)) continue;
        int supported = 1;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            supported = is_video_codec_supported(par);
            has_video = 1;
            if (!supported) can_transcode = 1;
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            supported = is_audio_codec_supported(par->codec_id);
            has_audio = 1;
            if (!supported) can_transcode = 1;
        } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            supported = is_subtitle_codec_supported(par->codec_id);
            if (!supported) {
                if (is_bitmap_subtitle(par->codec_id))
                    has_unsupported_bitmap_subtitle = 1;
                else
                    can_transcode = 1;
            }
        }
        if (!supported) all_supported = 0;
    }
    if (skip_ok && all_supported) {
        avformat_close_input(&fmt_ctx);
        summary->ok++;
        summary->total++;
        return;
    }
    if (skip_unfixable && !all_supported && !can_transcode && has_unsupported_bitmap_subtitle) {
        avformat_close_input(&fmt_ctx);
        summary->not_supported++;
        summary->total++;
        return;
    }

    printf("----------------\n\n%s\n", filename);
    printf("  container: %s | %s\n", container, container_ok ? COLOR_GREEN "OK" COLOR_RESET : COLOR_RED "NOT SUPPORTED" COLOR_RESET);
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *st = fmt_ctx->streams[i];
        AVCodecParameters *par = st->codecpar;
        if (!is_media_stream(par->codec_type)) continue;
        const char *lang = NULL;
        AVDictionaryEntry *tag = av_dict_get(st->metadata, "language", NULL, 0);
        if (tag) lang = tag->value;
        else lang = "und";
        const char *codec = avcodec_get_name(par->codec_id);
        int supported = 1;
        const char *type = NULL;

        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            type = "video";
            supported = is_video_codec_supported(par);
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            type = "audio";
            supported = is_audio_codec_supported(par->codec_id);
        } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            type = "subtitle";
            supported = is_subtitle_codec_supported(par->codec_id);
        }

        printf("    [%d] %s | %s | %s | %s%s%s\n",
            i, type, codec, lang,
            supported ? COLOR_GREEN : COLOR_RED,
            supported ? "OK" : "NOT SUPPORTED",
            COLOR_RESET
        );
        if (!supported && par->codec_type == AVMEDIA_TYPE_SUBTITLE && is_bitmap_subtitle(par->codec_id)) {
            printf(COLOR_YELLOW "  Note: Subtitle stream %d (%s) is bitmap-based and cannot be converted to srt. It will be copied as-is (may not be supported on your TV).\n" COLOR_RESET, i, avcodec_get_name(par->codec_id));
        }
    }
    printf("  overall: %s%s%s\n", 
        all_supported ? COLOR_GREEN : COLOR_RED,
        all_supported ? "ALL TRACKS SUPPORTED" : "SOME TRACKS UNSUPPORTED",
        COLOR_RESET);

    // Suggested remuxing command for unsupported files (only if video or audio present)
    if (!all_supported && (has_video || has_audio)) {
        char remux_cmd[8192] = {0};
        char remuxed_basename[PATH_BUF_SIZE];
        snprintf(remuxed_basename, sizeof(remuxed_basename), "remuxed_%s.mkv", get_basename(filepath));
        char *escaped_in = shell_escape_single(filepath);
        char *escaped_out = shell_escape_single(remuxed_basename);
        snprintf(remux_cmd, sizeof(remux_cmd),
            "ffmpeg -i %s -map 0 -c copy %s",
            escaped_in, escaped_out);
        printf("\n  Suggested remuxing command:\n    %s\n", remux_cmd);
        printf(COLOR_YELLOW "    (This changes only the container; streams are copied without re-encoding)\n" COLOR_RESET);
        free(escaped_in);
        free(escaped_out);
    }

    // Only suggest ffmpeg command if re-encoding can help
    if (!all_supported && (has_video || has_audio) && can_transcode) {
        char cmd[8192] = {0};
        char fixed_basename[PATH_BUF_SIZE];
        // Always output to .mkv for transcoded files
        const char *base = get_basename(filepath);
        const char *dot = strrchr(base, '.');
        if (dot) {
            snprintf(fixed_basename, sizeof(fixed_basename), "fixed_%.*s.mkv", (int)(dot - base), base);
        } else {
            snprintf(fixed_basename, sizeof(fixed_basename), "fixed_%s.mkv", base);
        }
        char *escaped_in = shell_escape_single(filepath);
        char *escaped_out = shell_escape_single(fixed_basename);

        snprintf(cmd, sizeof(cmd), "ffmpeg -i %s", escaped_in);

        int v_cnt = 0, a_cnt = 0, s_cnt = 0;
        char v_opts[1024] = {0}, a_opts[1024] = {0}, s_opts[1024] = {0};
        int had_video = 0, had_audio = 0, had_sub = 0;

        for (i = 0; i < fmt_ctx->nb_streams; i++) {
            AVStream *st = fmt_ctx->streams[i];
            AVCodecParameters *par = st->codecpar;
            if (!is_media_stream(par->codec_type)) continue;
            int supported = 1;
            if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                supported = is_video_codec_supported(par);
                if (!had_video) { strcat(cmd, " -map 0:v"); had_video = 1; }
                snprintf(v_opts + strlen(v_opts), sizeof(v_opts) - strlen(v_opts),
                    " -c:v:%d %s", v_cnt, supported ? "copy" : "libx264");
                v_cnt++;
            } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
                supported = is_audio_codec_supported(par->codec_id);
                if (!had_audio) { strcat(cmd, " -map 0:a"); had_audio = 1; }
                snprintf(a_opts + strlen(a_opts), sizeof(a_opts) - strlen(a_opts),
                    " -c:a:%d %s", a_cnt, supported ? "copy" : "aac");
                a_cnt++;
            } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                supported = is_subtitle_codec_supported(par->codec_id);
                if (!had_sub) { strcat(cmd, " -map 0:s"); had_sub = 1; }
                if (!supported) {
                    if (is_text_subtitle(par->codec_id)) {
                        snprintf(s_opts + strlen(s_opts), sizeof(s_opts) - strlen(s_opts),
                            " -c:s:%d srt", s_cnt);
                    } else {
                        snprintf(s_opts + strlen(s_opts), sizeof(s_opts) - strlen(s_opts),
                            " -c:s:%d copy", s_cnt);
                    }
                } else {
                    snprintf(s_opts + strlen(s_opts), sizeof(s_opts) - strlen(s_opts),
                        " -c:s:%d copy", s_cnt);
                }
                s_cnt++;
            }
        }

        strcat(cmd, v_opts);
        strcat(cmd, a_opts);
        strcat(cmd, s_opts);

        snprintf(cmd + strlen(cmd), sizeof(cmd) - strlen(cmd), " %s", escaped_out);

        printf("\n  Suggested ffmpeg command:\n    %s\n", cmd);

        free(escaped_in);
        free(escaped_out);
    }

    printf("\n");
    avformat_close_input(&fmt_ctx);
    if (all_supported) summary->ok++;
    else summary->not_supported++;
    summary->total++;
}

void scan_dir(const char *dirpath, char **excludes, int num_excludes, int show_full_path, Summary *summary);

int is_excluded(const char *path, char **excludes, int num_excludes) {
    for (int i = 0; i < num_excludes; ++i) {
        // Match directory or file name with pattern
        if (fnmatch(excludes[i], path, 0) == 0)
            return 1;
    }
    return 0;
}

void scan_dir(const char *dirpath, char **excludes, int num_excludes, int show_full_path, Summary *summary) {
    struct dirent *entry;
    DIR *dp = opendir(dirpath);
    if (!dp) {
        fprintf(stderr, "Could not open directory: %s (%s)\n", dirpath, strerror(errno));
        return;
    }
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_BUF_SIZE];
        snprintf(path, sizeof(path), "%s/%s", dirpath, entry->d_name);

        struct stat st;
        if (stat(path, &st) == -1) continue;
        if (S_ISDIR(st.st_mode)) {
            if (is_excluded(path, excludes, num_excludes)) continue;
            scan_dir(path, excludes, num_excludes, show_full_path, summary);
        } else if (S_ISREG(st.st_mode)) {
            check_file(path, show_full_path, summary);
        }
    }
    closedir(dp);
}

int main(int argc, char *argv[]) {
    av_log_set_callback(quiet_ffmpeg_log);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file-or-directory> [--exclude dir1 ...] [--fullpath] [--brief] [--skip-ok] [--skip-unfixable]\n", argv[0]);
        return 1;
    }

    char **excludes = NULL;
    int num_excludes = 0;
    int show_full_path = 0;
    const char *input = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--exclude") == 0 && i + 1 < argc) {
            excludes = realloc(excludes, (num_excludes + 1) * sizeof(char *));
            excludes[num_excludes++] = argv[++i];
        } else if (strcmp(argv[i], "--fullpath") == 0) {
            show_full_path = 1;
        } else if (strcmp(argv[i], "--brief") == 0) {
            brief_mode = 1;
        } else if (strcmp(argv[i], "--skip-ok") == 0) {
            skip_ok = 1;
        } else if (strcmp(argv[i], "--skip-unfixable") == 0) {
            skip_unfixable = 1;
        } else if (!input) {
            input = argv[i];
        }
    }

    if (!input) {
        fprintf(stderr, "No file or directory specified.\n");
        return 1;
    }

    struct stat st;
    if (stat(input, &st) == -1) {
        fprintf(stderr, "Could not stat '%s': %s\n", input, strerror(errno));
        return 1;
    }

    Summary summary = {0};

    if (S_ISDIR(st.st_mode)) {
        scan_dir(input, excludes, num_excludes, show_full_path, &summary);
    } else if (S_ISREG(st.st_mode)) {
        check_file(input, show_full_path, &summary);
    } else {
        fprintf(stderr, "'%s' is not a regular file or directory.\n", input);
        return 1;
    }

    if (!brief_mode) {
        printf("\n--- Summary ---\n");
        printf("Total checked: %d\n", summary.total);
        printf(COLOR_GREEN "OK: %d\n" COLOR_RESET, summary.ok);
        printf(COLOR_RED "NOT SUPPORTED: %d\n" COLOR_RESET, summary.not_supported);
        printf(COLOR_YELLOW "Errors: %d\n" COLOR_RESET, summary.errors);
    }

    free(excludes);
    return 0;
}
/* vim: set ts=4 sts=4 sw=4 et : */

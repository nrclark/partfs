/*
 *  PartFS: A FUSE filesystem for mounting parts of a file.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *  Copyright 2018, Nicholas Clark */

#define _POSIX_C_SOURCE 200809L
#define FUSE_USE_VERSION 26

#include <errno.h>
#include <fuse.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "fdisk_access.h"

#define DISABLE_WRITES (~0222U)
#define DEFAULT_PERMS (0644U)

#define KILO (0x1ULL << 10U)
#define MEGA (0x1ULL << 20U)
#define GIGA (0x1ULL << 30U)
#define TERA (0x1ULL << 40U)

/*----------------------------------------------------------------------------*/

static char progname[NAME_MAX + 1] = {0};

struct partfs_context {
    const char *created_file;
    int dir_fd;
    int read_only;
    int source_fd;
    mode_t source_mode;
    size_t max_size;
    size_t source_offset;
    size_t current_size;
    struct fuse_args *args;
};

struct partfs_config {
    size_t offset;
    size_t size;
    int read_only;
    int nonempty;
    int print_table;
    char *offset_string;
    char *size_string;
    char *partition_string;
    char source[PATH_MAX + 1];
    char mountpoint[PATH_MAX + 1];
};

#define PARTFS_OPT(t, p, v) {t, offsetof(struct partfs_config, p), v}

enum {
    KEY_VERSION,
    KEY_HELP,
    KEY_PRINT_PARTITION
};

static struct fuse_opt partfs_opts[] = {
    PARTFS_OPT("offset=%s", offset_string, 0),
    PARTFS_OPT("sizelimit=%s", size_string, 0),
    PARTFS_OPT("partition=%s", partition_string, 0),
    PARTFS_OPT("ro", read_only, 1),
    PARTFS_OPT("nonempty", nonempty, 1),
    FUSE_OPT_KEY("-p", KEY_PRINT_PARTITION),
    FUSE_OPT_KEY("--print-partitions", KEY_PRINT_PARTITION),
    FUSE_OPT_KEY("-V", KEY_VERSION),
    FUSE_OPT_KEY("--version", KEY_VERSION),
    FUSE_OPT_KEY("-h", KEY_HELP),
    FUSE_OPT_KEY("--help", KEY_HELP),
    FUSE_OPT_END
};

/*----------------------------------------------------------------------------*/

static int parse_number(const char *input, size_t *output)
{
    char *endptr = NULL;
    uintmax_t value_umax = 0;
    size_t value = 0;
    char mult = 0;
    int prev_errno = errno;

    if (input == NULL) {
        return -1;
    }

    errno = 0;
    value_umax = strtoumax(input, &endptr, 0);

    if ((errno != 0) || (endptr == input)) {
        errno = prev_errno;
        return -1;
    }

    value = value_umax;

    if (endptr != NULL) {
        mult = endptr[0];

        if (mult >= 'a') {
            mult -= ('a' - 'A');
        }
    }

    switch (mult) {
        case 0:
            break;

        case 'K':
            value *= KILO;
            break;

        case 'M':
            value *= MEGA;
            break;

        case 'G':
            value *= GIGA;
            break;

        case 'T':
            value *= TERA;
            break;

        case 'B':
            break;

        default:
            errno = prev_errno;
            return -1;
    }

    *output = value;
    errno = prev_errno;
    return 0;
}

static void controlled_exit(struct partfs_context *ctx, int exit_code)
{
    if (ctx == NULL) {
        exit(exit_code);
    }

    if (ctx->source_fd >= 0) {
        close(ctx->source_fd);
        ctx->source_fd = -1;
    }

    if (ctx->args != NULL) {
        fuse_opt_free_args(ctx->args);
    }

    if (ctx->dir_fd >= 0) {
        if (ctx->created_file) {
            if (unlinkat(ctx->dir_fd, ctx->created_file, 0)) {
                fprintf(stderr, "warning: couldn't remove tempfile [%s]",
                        ctx->created_file);
                fprintf(stderr, " (%s)\n", strerror(errno));
            }

            ctx->created_file = NULL;
        }

        close(ctx->dir_fd);
        ctx->dir_fd = -1;
    }

    exit(exit_code);
}

static ssize_t read_noeintr(int fildes, void *buf, size_t nbyte)
{
    ssize_t result = 0;

    do {
        result = read(fildes, buf, nbyte);
    } while ((result == -1) && (errno == EINTR));

    return result;
}

static ssize_t write_noeintr(int fildes, const void *buf, size_t nbyte)
{
    ssize_t result = 0;

    do {
        result = write(fildes, buf, nbyte);
    } while ((result == -1) && (errno == EINTR));

    return result;
}

static ssize_t read_count(int filedes, char *buf, size_t nbyte)
{
    size_t total = 0;

    while (total < nbyte) {
        ssize_t result = read_noeintr(filedes, buf, (nbyte - total));

        if (result < 0) {
            return result;
        }

        total += (size_t) result;
    }

    return (ssize_t) nbyte;
}

static ssize_t write_count(int filedes, const char *buf, size_t nbyte)
{
    size_t remaining = nbyte;

    while (remaining != 0) {
        ssize_t result = write_noeintr(filedes, buf, remaining);

        if (result < 0) {
            return result;
        }

        remaining -= (size_t) result;
        buf += result;
    }

    return (ssize_t) nbyte;
}

static inline void safecopy(char *dest, const char *src, unsigned int maxlen)
{
    size_t length = strnlen(src, maxlen - 1);
    memcpy(dest, src, length);
    dest[length] = '\x00';
}

static void exit_help(void)
{
    const struct fuse_operations dummy_ops = {0};
    char help_opt[4] = "-ho";
    char *argv[2] = {progname, help_opt};

    const char *partfs_help =
        "Mount part of SOURCE as a different file at MOUNTPOINT.\n"
        "\n"
        "Usage: %s SOURCE MOUNTPOINT [options]\n"
        "\n"
        "General options:\n"
        "    -o opt,[opt...]        mount options\n"
        "    -h   --help            print help\n"
        "    -V   --version         print version\n"
        "\n"
        "PartFS options:\n"
        "    -o offset=NBYTES       offset into SOURCE (in bytes)\n"
        "    -o sizelimit=NBYTES    max length of MOUNT (in bytes)"
#ifdef ENABLE_PARTITIONS
        "\n\n"
        "    -o partition=PARTNUM   partition to mount from SOURCE\n"
        "    -p/--print-partitions  print partition table and exit"
#endif
        ;

    fprintf(stderr, partfs_help, progname);
    fprintf(stderr, "\n\n");
    fuse_main(2, argv, &dummy_ops, NULL);
    exit(1);
}

static int partfs_opt_proc(void *data, const char *arg, int key,
                           struct fuse_args *outargs)
{
    struct partfs_config *config = (struct partfs_config *) data;
    const struct fuse_operations dummy_ops = {0};

    switch (key) {
        case KEY_PRINT_PARTITION:
            config->print_table = 1;
            strcpy(config->mountpoint, "/dev/null");
            break;

        case KEY_VERSION:
            fprintf(stderr, "PartFS version: %s", PACKAGE_VERSION);
            fprintf(stderr, "\n");
            fuse_opt_add_arg(outargs, "--version");
            fuse_main(outargs->argc, outargs->argv, &dummy_ops, NULL);
            fuse_opt_free_args(outargs);
            exit(0);
            break;

        case FUSE_OPT_KEY_NONOPT:
            if (config->source[0] == '\x00') {
                if (arg[0] == '\x00') {
                    fprintf(stderr, "%s: ", progname);
                    fprintf(stderr, "%s\n",
                            "error: source must not be an empty string.");
                    fuse_opt_free_args(outargs);
                    exit(1);
                }
                safecopy(config->source, arg, sizeof(config->source));
                return 0;
            }

            if (config->mountpoint[0] == '\x00') {
                if (arg[0] == '\x00') {
                    fprintf(stderr, "%s: ", progname);
                    fprintf(stderr, "%s\n",
                            "error: mount-point must not be an empty string."
                            );
                    fuse_opt_free_args(outargs);
                    exit(1);
                }
                safecopy(config->mountpoint, arg, sizeof(config->mountpoint));
                return 1;
            }

            fprintf(stderr, "%s: %s [%s]\n", progname,
                    "error: invalid additional argument", arg);

            fuse_opt_free_args(outargs);
            exit(1);
            break;

        default:
            break;
    }
    return 1;
}

static struct partfs_context * partfs_get_context(void)
{
    struct fuse_context *fuse_context = fuse_get_context();
    struct partfs_context *result = NULL;

    if (fuse_context == NULL) {
        fprintf(stderr, "%s: ", progname);
        fprintf(stderr, "%s\n", "error: couldn't retrieve FUSE context.");
        exit(0);
    }

    result = (struct partfs_context *)(fuse_context->private_data);

    if (result == NULL) {
        fprintf(stderr, "%s: ", progname);
        fprintf(stderr, "%s\n", "error: couldn't retrieve PartFS context.");
        exit(0);
    }

    return result;
}

/*----------------------------------------------------------------------------*/

static int partfs_open(const char *path, struct fuse_file_info *info)
{
    (void) path;
    (void) info;
    struct partfs_context *ctx = partfs_get_context();

    if (ctx->read_only && ((info->flags & O_ACCMODE) != O_RDONLY)) {
        return -EACCES;
    }

    return 0;
}

static int partfs_getattr(const char *path, struct stat *stbuf)
{
    (void) path;
    struct stat source_stat = {0};
    int result = 0;
    struct partfs_context *ctx = partfs_get_context();

    memset(stbuf, 0, sizeof(struct stat));

    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    stbuf->st_mode = S_IFREG | ctx->source_mode;

    if (ctx->read_only) {
        stbuf->st_mode &= DISABLE_WRITES;
    }

    stbuf->st_nlink = 1;
    stbuf->st_size = (off_t) ctx->current_size;

    result = fstat(ctx->source_fd, &source_stat);

    if (result < 0) {
        return -errno;
    }

    stbuf->st_ino = source_stat.st_ino;
    memcpy(&(stbuf->st_atim), &(source_stat.st_atim), sizeof(stbuf->st_atim));
    memcpy(&(stbuf->st_mtim), &(source_stat.st_mtim), sizeof(stbuf->st_mtim));
    memcpy(&(stbuf->st_ctim), &(source_stat.st_ctim), sizeof(stbuf->st_ctim));

    return 0;
}

static int partfs_read(const char *path, char *buf, size_t size,
                       off_t offset, struct fuse_file_info *info)
{
    (void) path;
    off_t lseek_result = 0;
    ssize_t read_result = 0;
    struct partfs_context *ctx = partfs_get_context();

    if (((size_t)offset + size) < (size_t)offset) {
        /* Size + offset overflowed */
        return -EINVAL;
    }

    if (((size_t)offset + size) > ctx->current_size) {
        size = ctx->current_size - (size_t) offset;
    }

    if (size == 0) {
        return 0;
    }

    lseek_result = lseek(ctx->source_fd, offset + (off_t) ctx->source_offset,
                         SEEK_SET);

    if (lseek_result < 0) {
        if (info->direct_io) {
            return (int) lseek_result;
        }
        return -errno;
    }

    if (info->direct_io) {
        return (int) read(ctx->source_fd, buf, size);
    }

    read_result = read_count(ctx->source_fd, buf, size);

    if (read_result < 0) {
        return -errno;
    }

    return (int) read_result;
}

static int partfs_write(const char *path, const char *buf, size_t size,
                        off_t offset, struct fuse_file_info *info)
{
    (void) path;
    off_t lseek_result = 0;
    ssize_t write_result = 0;
    size_t stop_byte = 0;

    struct partfs_context *ctx = partfs_get_context();

    if (((size_t)offset + size) < (size_t)offset) {
        /* Size + offset overflowed */
        return -EINVAL;
    }

    if ((size_t)offset > ctx->max_size) {
        return -EIO;
    }

    if (((size_t)offset == ctx->max_size) && (size != 0)) {
        return -EIO;
    }

    if (((size_t)offset + size) > ctx->max_size) {
        size = ctx->max_size - (size_t) offset;
    }

    stop_byte = (size_t) offset + size;

    if (stop_byte > ctx->current_size) {
        ctx->current_size = stop_byte;
    }

    lseek_result = lseek(ctx->source_fd, offset + (off_t) ctx->source_offset,
                         SEEK_SET);

    if (lseek_result < 0) {
        if (info->direct_io) {
            return (int) lseek_result;
        }
        return -errno;
    }

    if (info->direct_io) {
        return (int) write(ctx->source_fd, buf, size);
    }

    write_result = write_count(ctx->source_fd, buf, size);

    if (write_result < 0) {
        return -errno;
    }

    return (int) write_result;
}

static int partfs_access(const char *path, int amode)
{
    (void) path;
    struct partfs_context *ctx = partfs_get_context();

    if ((amode & W_OK) && ctx->read_only) {
        return -EACCES;
    }

    if (amode & X_OK) {
        return -EACCES;
    }

    return 0;
}

static int partfs_utimens(const char *path, const struct timespec tv[2])
{
    (void) path;
    struct partfs_context *ctx = partfs_get_context();

    int result = futimens(ctx->source_fd, tv);

    if (result < 0) {
        return -errno;
    }
    return 0;
}

static int partfs_truncate(const char *path, off_t length)
{
    (void) path;
    struct partfs_context *ctx = partfs_get_context();

    ctx->current_size = (size_t) length;
    return 0;
}

static int partfs_chown(const char *path, uid_t uid, gid_t gid)
{
    (void) path;
    (void) uid;
    (void) gid;
    return -EPERM;
}

static int partfs_chmod(const char *path, mode_t mode)
{
    (void) path;
    (void) mode;
    return 0;
}

static int partfs_fsync(const char *path, int datasync,
                        struct fuse_file_info *info)
{
    (void) path;
    (void) datasync;
    (void) info;
    struct partfs_context *ctx = partfs_get_context();

    int result = fsync(ctx->source_fd);

    if (result < 0) {
        return -errno;
    }

    return result;
}

/*----------------------------------------------------------------------------*/

static struct fuse_operations partfs_operations = {
    .getattr = partfs_getattr,
    .open = partfs_open,
    .read = partfs_read,
    .write = partfs_write,
    .access = partfs_access,
    .utimens = partfs_utimens,
    .truncate = partfs_truncate,
    .chown = partfs_chown,
    .chmod = partfs_chmod,
    .fsync = partfs_fsync,
};

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct partfs_config config = {.size = (size_t) -1};
    struct partfs_context context = {.source_fd = -1, .args = &args};

    char arg_buffer[sizeof("-ofsname=") + NAME_MAX + 2] = "-ofsname=";
    unsigned int arg_offset = sizeof("-ofsname=") - 1;
    unsigned int arg_maxlen = (unsigned int) sizeof(arg_buffer) - arg_offset;

    struct stat stat_buffer = {0};
    size_t partition = (size_t) -1;
    int result = 0;

    safecopy(progname, basename(argv[0]), sizeof(progname));

    for (int x = 1; x < argc; x++) {
        if ((strcmp(argv[x], "--help") == 0) || (strcmp(argv[x], "-h") == 0)) {
            fuse_opt_free_args(&args);
            exit_help();
        }
    }

    fuse_opt_parse(&args, &config, partfs_opts, partfs_opt_proc);

    if ((config.partition_string != NULL) || (config.print_table != 0)) {
#ifndef ENABLE_PARTITIONS
        fprintf(stderr, "%s: %s\n", progname,
                "error: not compiled with partition-table support.");
        controlled_exit(&context, 1);
#endif
    }

    if (config.partition_string != NULL) {
        if (config.size_string || config.offset_string) {
            fprintf(stderr, "%s: %s\n", progname,
                    "error: 'partition' can't be specified along with"
                    "'offset or 'sizelimit'");
            controlled_exit(&context, 1);
        }

        if (parse_number(config.partition_string, &partition)) {
            fprintf(stderr, "%s: %s [%s]\n", progname,
                    "error: invalid partition", config.size_string);
            controlled_exit(&context, 1);
        }

        if (partition == 0) {
            fprintf(stderr, "%s: %s.\n", progname,
                    "error: partition numbers start at 1");
            controlled_exit(&context, 1);
        }
    }

    if (config.size_string != NULL) {
        if (parse_number(config.size_string, &config.size)) {
            fprintf(stderr, "%s: %s [%s]\n", progname,
                    "error: invalid sizelimit", config.size_string);
            controlled_exit(&context, 1);
        }
    }

    if (config.offset_string != NULL) {
        if (parse_number(config.offset_string, &config.offset)) {
            fprintf(stderr, "%s: %s [%s]\n", progname,
                    "error: invalid offset", config.offset_string);
            controlled_exit(&context, 1);
        }
    }

    if (config.source[0] == '\x00') {
        fprintf(stderr, "%s: %s\n", progname,
                "error: source not specified.");
        controlled_exit(&context, 1);
    }

    if (config.mountpoint[0] == '\x00') {
        fprintf(stderr, "%s: %s\n", progname,
                "error: mount-point not specified.");
        controlled_exit(&context, 1);
    }

    context.dir_fd = openat(AT_FDCWD, ".", O_RDONLY | O_DIRECTORY);

    if (context.dir_fd < 0) {
        fprintf(stderr, "%s: ", progname);
        fprintf(stderr, "error: couldn't open cwd");
        fprintf(stderr, " (%s)\n", strerror(errno));
        controlled_exit(&context, 1);
    }

    result = faccessat(context.dir_fd, config.mountpoint, F_OK, AT_EACCESS);

    if (result != 0) {
        result = openat(context.dir_fd, config.mountpoint,
                        O_CREAT | O_WRONLY | O_TRUNC, DEFAULT_PERMS);

        if (result < 0) {
            fprintf(stderr, "%s: ", progname);
            fprintf(stderr, "error: couldn't create mount-point [%s]",
                    config.mountpoint);
            fprintf(stderr, " (%s)\n", strerror(errno));
            controlled_exit(&context, 1);
        }

        close(result);
        context.created_file = config.mountpoint;
    }

    result = fstatat(context.dir_fd, config.mountpoint, &stat_buffer,
                     AT_SYMLINK_NOFOLLOW);

    if (result < 0) {
        fprintf(stderr, "%s: ", progname);
        fprintf(stderr, "error: couldn't access mount-point [%s]",
                config.mountpoint);
        fprintf(stderr, " (%s)\n", strerror(errno));
        controlled_exit(&context, 1);
    }

    if (strcmp(config.mountpoint, "/dev/null") != 0) {
        if (((stat_buffer.st_size != 0) && (config.nonempty == 0)) ||
            (S_ISREG(stat_buffer.st_mode) == 0)) {
            fprintf(stderr, "%s: %s\n", progname,
                    "error: mount-point is not an empty file.");
            controlled_exit(&context, 1);
        }
    }

    if (config.read_only) {
        context.source_fd = openat(context.dir_fd, config.source, O_RDONLY);
    } else {
        context.source_fd = openat(context.dir_fd, config.source, O_RDWR);
    }

    if (context.source_fd < 0) {
        fprintf(stderr, "%s: ", progname);
        fprintf(stderr, "error: couldn't open file [%s]",
                config.source);
        fprintf(stderr, " (%s)\n", strerror(errno));
        controlled_exit(&context, 1);
    }

    result = fstat(context.source_fd, &stat_buffer);

    if (result != 0) {
        fprintf(stderr, "%s: ", progname);
        fprintf(stderr, "error: couldn't stat file [%s]",
                config.source);
        fprintf(stderr, " (%s)", strerror(errno));
        controlled_exit(&context, 1);
    }

#ifdef ENABLE_PARTITIONS
    if (config.print_table) {
        result = partition_count(config.source);
        struct part_info *info = NULL;

        if (result < 0) {
            fprintf(stderr, "%s: ", progname);
            fprintf(stderr, "error: couldn't find partition table in [%s]\n",
                    config.source);
            controlled_exit(&context, 1);
        }

        printf("Number:Name:UUID:Type:Offset:Size\n");

        for (unsigned int x = 0; x < (unsigned int) result; x++) {
            int prev_errno = errno;
            errno = 0;
            if (partition_get_info(config.source, x, &info) != 0) {
                fprintf(stderr, "%s: ", progname);
                fprintf(stderr, "error: couldn't read partition %u in [%s]\n",
                        x, config.source);
                if (errno) {
                    fprintf(stderr, "%s\n", strerror(errno));
                }
                errno = prev_errno;
                partition_dealloc_info(info);
                controlled_exit(&context, 1);
            }
            printf("%u:%s:%s:%s:%zd:%zd\n", x+1, info->name,
                   info->uuid, info->type, info->start, info->length);

            partition_dealloc_info(info);
        }

        controlled_exit(&context, 0);
    }

    if (partition != (size_t) -1) {
        result = partition_count(config.source);
        struct part_info *info = NULL;

        if (result < 0) {
            fprintf(stderr, "%s: ", progname);
            fprintf(stderr, "error: couldn't find partition table in [%s]\n",
                    config.source);
            controlled_exit(&context, 1);
        }

        if (result < (int) partition) {
            fprintf(stderr, "%s: ", progname);
            fprintf(stderr, "error: partition %d not found in [%s]\n",
                    (int)(partition), config.source);
            controlled_exit(&context, 1);
        }

        if (partition_get_info(config.source, partition - 1, &info) != 0) {
            fprintf(stderr, "%s: ", progname);
            fprintf(stderr, "error: couldn't detect position of partition %d"
                    "in [%s]\n", (int) partition, config.source);
            partition_dealloc_info(info);
            controlled_exit(&context, 1);
        }

        config.offset = (size_t) info->start;
        config.size = (size_t) info->length;
        partition_dealloc_info(info);
    }
#endif

    if (config.size == (size_t) -1) {
        config.size = (size_t) stat_buffer.st_size - config.offset;
    }

    if ((config.offset + config.size) > (size_t) stat_buffer.st_size) {
        fprintf(stderr, "%s: ", progname);
        fprintf(stderr, "error: requested size or offset extends past the"
                          " end of [%s]", basename(config.source));
        fprintf(stderr, "\n");
        controlled_exit(&context, 1);
    }

    context.source_mode = stat_buffer.st_mode;
    context.read_only = config.read_only;
    context.max_size = config.size;
    context.current_size = config.size;
    context.source_offset = config.offset;

    fuse_opt_add_arg(&args, "-s");

    if (config.nonempty) {
        fuse_opt_add_arg(&args, "-ononempty");
    }

    safecopy(arg_buffer + arg_offset, config.source, arg_maxlen);
    fuse_opt_add_arg(&args, arg_buffer);

    result = fuse_main(args.argc, args.argv, &partfs_operations, &context);
    controlled_exit(&context, result);

    return result;
}

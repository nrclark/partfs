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

#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "config.h"

/*----------------------------------------------------------------------------*/

static char progname[NAME_MAX + 1] = {0};

struct partfs_context {
    int read_only;
    int source_fd;
    mode_t source_mode;
    size_t mount_size;
    size_t source_offset;
    struct fuse_args *args;
};

struct partfs_config {
    size_t offset;
    size_t size;
    int read_only;
    int nonempty;
    char source[PATH_MAX + 1];
    char mountpoint[PATH_MAX + 1];
};

#define PARTFS_OPT(t, p, v) {t, offsetof(struct partfs_config, p), v}

enum {
    KEY_VERSION,
};

static struct fuse_opt partfs_opts[] = {
    PARTFS_OPT("offset=%zu", offset, 0),
    PARTFS_OPT("sizelimit=%zu", size, 0),
    PARTFS_OPT("ro", read_only, 1),
    PARTFS_OPT("nonempty", nonempty, 1),

    FUSE_OPT_KEY("-V", KEY_VERSION),
    FUSE_OPT_KEY("--version", KEY_VERSION),
    FUSE_OPT_KEY("-h", 0),
    FUSE_OPT_KEY("--help", 0),
    FUSE_OPT_END
};

static const char partfs_help[] =
"usage: %s mountpoint [options]\n"
"\n"
"general options:\n"
"    -o opt,[opt...]  mount options\n"
"    -h   --help      print help\n"
"    -V   --version   print version\n"
"\n"
"PartFS options:\n"
"    -o offset=NBYTES\n"
"    -o sizelimit=NBYTES\n"
"\n";

/*----------------------------------------------------------------------------*/

static void controlled_exit(struct partfs_context *ctx, int exit_code)
{
    if (ctx != NULL) {
        if (ctx->source_fd >= 0) {
            close(ctx->source_fd);
        }

        if (ctx->args != NULL) {
            fuse_opt_free_args(ctx->args);
        }
    }

    exit(exit_code);
}

static ssize_t read_noeintr(int fildes, void *buf, size_t nbyte)
{
    ssize_t result;

    do {
        result = read(fildes, buf, nbyte);
    } while ((result == -1) && (errno == EINTR));

    return result;
}

static ssize_t write_noeintr(int fildes, const void *buf, size_t nbyte)
{
    ssize_t result;

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

    fprintf(stderr, partfs_help, progname);
    fuse_main(2, argv, &dummy_ops, NULL);
    exit(1);
}

static int partfs_opt_proc(void *data, const char *arg, int key,
                           struct fuse_args *outargs)
{
    struct partfs_config *config = (struct partfs_config *) data;
    const struct fuse_operations dummy_ops = {0};

    switch (key) {
        case KEY_VERSION:
            fprintf(stderr, "PartFS version: "PACKAGE_VERSION "\n");
            fuse_opt_add_arg(outargs, "--version");
            fuse_main(outargs->argc, outargs->argv, &dummy_ops, NULL);
            fuse_opt_free_args(outargs);
            exit(0);
            break;

        case FUSE_OPT_KEY_NONOPT:
            if (config->source[0] == '\x00') {
                if (arg[0] == '\x00') {
                    fprintf(stderr, "%s: error: source must not be an empty "
                            "string.\n", progname);
                    fuse_opt_free_args(outargs);
                    exit(1);
                }
                safecopy(config->source, arg, sizeof(config->source));
                return 0;
            }

            if (config->mountpoint[0] == '\x00') {
                if (arg[0] == '\x00') {
                    fprintf(stderr, "%s: error: mount-point must not be an "
                            "empty string.\n", progname);
                    fuse_opt_free_args(outargs);
                    exit(1);
                }
                safecopy(config->mountpoint, arg, sizeof(config->mountpoint));
                return 1;
            }

            fprintf(stderr, "%s: error: invalid additional argument [%s].\n",
                    progname, arg);

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
    struct partfs_context *result;

    if (fuse_context == NULL) {
        fprintf(stderr, "%s: error: couldn't retrieve fuse context.\n",
                progname);
        exit(0);
    }

    result = (struct partfs_context *)(fuse_context->private_data);

    if (result == NULL) {
        fprintf(stderr, "%s: error: couldn't retrieve partfs context.\n",
                progname);
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
    struct stat source_stat;
    int result;
    struct partfs_context *ctx = partfs_get_context();

    memset(stbuf, 0, sizeof(struct stat));

    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    stbuf->st_mode = S_IFREG | ctx->source_mode;

    if (ctx->read_only) {
        stbuf->st_mode &= ~0222U;
    }

    stbuf->st_nlink = 1;
    stbuf->st_size = (off_t) ctx->mount_size;

    result = fstat(ctx->source_fd, &source_stat);

    if (result < 0) {
        return -errno;
    }

    memcpy(&(stbuf->st_atim), &(source_stat.st_atim), sizeof(stbuf->st_atim));
    memcpy(&(stbuf->st_mtim), &(source_stat.st_mtim), sizeof(stbuf->st_mtim));
    memcpy(&(stbuf->st_ctim), &(source_stat.st_ctim), sizeof(stbuf->st_ctim));

    return 0;
}

static int partfs_read(const char *path, char *buf, size_t size,
                       off_t offset, struct fuse_file_info *info)
{
    (void) path;
    off_t lseek_result;
    ssize_t read_result;
    struct partfs_context *ctx = partfs_get_context();

    if (((size_t)offset + size) < (size_t)offset) {
        /* Size + offset overflowed */
        return -EINVAL;
    }

    if (((size_t)offset + size) > ctx->mount_size) {
        size = ctx->mount_size - (size_t) offset;
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
    off_t lseek_result;
    ssize_t write_result;
    struct partfs_context *ctx = partfs_get_context();

    if (((size_t)offset + size) < (size_t)offset) {
        /* Size + offset overflowed */
        return -EINVAL;
    }

    if ((size_t)offset > ctx->mount_size) {
        return -EIO;
    }

    if (((size_t)offset == ctx->mount_size) && (size != 0)) {
        return -EIO;
    }

    if (((size_t)offset + size) > ctx->mount_size) {
        size = ctx->mount_size - (size_t) offset;
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

static int partfs_truncate(const char *path, off_t offset)
{
    (void) path;
    (void) offset;
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

    struct stat stat_buffer;
    int result;

    safecopy(progname, basename(argv[0]), sizeof(progname));

    for (int x = 1; x < argc; x++) {
        if ((strcmp(argv[x], "--help") == 0) || (strcmp(argv[x], "-h") == 0)) {
            fuse_opt_free_args(&args);
            exit_help();
        }
    }

    fuse_opt_parse(&args, &config, partfs_opts, partfs_opt_proc);

    if (config.source[0] == '\x00') {
        fprintf(stderr, "%s: error: source not specified.\n", progname);
        controlled_exit(&context, 1);
    }

    if (config.mountpoint[0] == '\x00') {
        fprintf(stderr, "%s: error: mount-point not specified.\n", progname);
        controlled_exit(&context, 1);
    }

    result = lstat(config.mountpoint, &stat_buffer);

    if (result != 0) {
        fprintf(stderr, "%s: error: couldn't open mount-point [%s] (%s)\n",
                progname, config.mountpoint, strerror(errno));
        controlled_exit(&context, 1);
    }

    if (((stat_buffer.st_size != 0) && (config.nonempty == 0)) ||
        (S_ISREG(stat_buffer.st_mode) == 0)) {
        fprintf(stderr, "%s: error: mount-point is not an empty file.\n",
                progname);
        controlled_exit(&context, 1);
    }

    if (config.read_only) {
        context.source_fd = open(config.source, O_RDONLY);
    } else {
        context.source_fd = open(config.source, O_RDWR);
    }

    if (context.source_fd < 0) {
        fprintf(stderr, "%s: error: couldn't open file [%s] (%s)\n",
                progname, config.source, strerror(errno));
        controlled_exit(&context, 1);
    }

    result = stat(config.source, &stat_buffer);

    if (result != 0) {
        fprintf(stderr, "%s: error: couldn't stat file [%s] (%s)\n",
                progname, config.source, strerror(errno));
        controlled_exit(&context, 1);
    }

    if (config.size == (size_t) -1) {
        config.size = (size_t) stat_buffer.st_size - config.offset;
    }

    if ((config.offset + config.size) > (size_t) stat_buffer.st_size) {
        fprintf(stderr, "%s: error: requested size or offset extends past the"
                " end of %s\n", progname, basename(config.source));
        controlled_exit(&context, 1);
    }

    context.source_mode = stat_buffer.st_mode;
    context.read_only = config.read_only;
    context.mount_size = config.size;
    context.source_offset = config.offset;

    fuse_opt_add_arg(&args, "-s");

    if (config.nonempty) {
        fuse_opt_add_arg(&args, "-ononempty");
    }

    result = fuse_main(args.argc, args.argv, &partfs_operations, &context);
    controlled_exit(&context, result);

    return result;
}

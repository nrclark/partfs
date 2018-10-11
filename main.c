#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>

#define FUSE_USE_VERSION 26
#include <fuse/fuse.h>

//-----------//-----------//-----------//-----------//-----------//-----------//

ssize_t read_noeintr(int fildes, void *buf, size_t nbyte)
{
    ssize_t result;

    do {
        result = read(fildes, buf, nbyte);
    } while ((result == -1) && (errno == EINTR));

    return result;
}

ssize_t write_noeintr(int fildes, const void *buf, size_t nbyte)
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

//-----------//-----------//-----------//-----------//-----------//-----------//

//XXX: implement ro/rw

static char progname[NAME_MAX+1] = {0};
static int read_only = 0;
static int source_fd;
mode_t source_mode;
static size_t mount_size = 0;
static size_t source_offset = 0;

struct partfs_config {
    size_t offset;
    size_t size;
    int read_only;
    char source[PATH_MAX+1];
    char mountpoint[PATH_MAX+1];
};

#define PARTFS_OPT(t, p, v) {t, offsetof(struct partfs_config, p), v}

enum {
     KEY_VERSION,
};

static int partfs_open(const char *path, struct fuse_file_info *info);

static int partfs_getattr(const char *path, struct stat *stbuf);

static int partfs_read(const char *path, char *buf, size_t size,
                      off_t offset, struct fuse_file_info *info);

static int partfs_write(const char *path, const char *buf, size_t size,
                        off_t offset, struct fuse_file_info *info);

static struct fuse_operations partfs_operations = {
    .getattr = partfs_getattr,
    .open = partfs_open,
    .read = partfs_read,
    .write = partfs_write,
};

static struct fuse_opt partfs_opts[] = {
    PARTFS_OPT("offset=%lu", offset, 0),
    PARTFS_OPT("size=%lu", size, 0),
    PARTFS_OPT("ro", read_only, 1),

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
    "    -o size=NBYTES\n"
    "\n";

static const char partfs_version[] = "0.0.1";

static inline void safecopy(char *dest, const char *src, unsigned int maxlen)
{
    size_t length = strnlen(src, maxlen-1);
    memcpy(dest, src, length);
    dest[length] = '\x00'; 
}

static void exit_help()
{
    fprintf(stderr, partfs_help, progname);
    char *argv[2] = {progname, "-ho"};
    fuse_main(2, argv, &partfs_operations, NULL);
    exit(1);
}

static int partfs_opt_proc(void *data, const char *arg, int key,
                           struct fuse_args *outargs)
{
    struct partfs_config *config = data;

    switch (key) {
        case KEY_VERSION:
            fprintf(stderr, "PartFS version %s\n", partfs_version);
            fuse_opt_add_arg(outargs, "--version");
            fuse_main(outargs->argc, outargs->argv, &partfs_operations, NULL);
            exit(0);
            break;

        case FUSE_OPT_KEY_NONOPT:
            if (config->source[0] == '\x00') {
                if (arg[0] == '\x00') {
                    fprintf(stderr, "%s: error: source must not be an empty "
                            "string.\n", progname);
                    exit(1);
                }
                safecopy(config->source, arg, sizeof(config->source));
                return 0;
            }

            if (config->mountpoint[0] == '\x00') {
                if (arg[0] == '\x00') {
                    fprintf(stderr, "%s: error: mount-point must not be an "
                            "empty string.\n", progname);
                    exit(1);
                }
                safecopy(config->mountpoint, arg, sizeof(config->mountpoint));
                return 1;
            }

            fprintf(stderr, "%s: error: invalid additional argument [%s].\n",
                    progname, arg);

            exit(1);
            break;
    }
    return 1;
}

//----------------------------------------------------------------------------//

static int partfs_open(const char *path, struct fuse_file_info *info)
{
    (void) path;
    (void) info;

    if (read_only && ((info->flags & O_ACCMODE) != O_RDONLY)) {
        return -EACCES;
    }

    if ((info->flags & O_ACCMODE) != O_RDWR) {
        return -EACCES;
    }

    return 0;
}

static int partfs_getattr(const char *path, struct stat *stbuf)
{
    (void) path;
    memset(stbuf, 0, sizeof(struct stat));

    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    stbuf->st_mode = S_IFREG | source_mode;

    if (read_only) {
        stbuf->st_mode &= ~0222;
    }

    stbuf->st_nlink = 1;
    stbuf->st_size = mount_size;

    return 0;
}

static int partfs_read(const char *path, char *buf, size_t size,
                       off_t offset, struct fuse_file_info *info)
{
    (void) path;
    off_t lseek_result;
    ssize_t read_result;

    if (((size_t)offset + size) < (size_t)offset) {
        return -EINVAL;
    }

    if ((offset + size) > mount_size) {
        size = mount_size - offset;
    }

    if (size == 0) {
        return 0;
    }

    lseek_result = lseek(source_fd, offset + source_offset, SEEK_SET);

    if (lseek_result < 0) {
        if (info->direct_io) {
            return lseek_result;
        }
        return -errno;
    }

    if (info->direct_io) {
        return read(source_fd, buf, size);
    }

    read_result = read_count(source_fd, buf, size);

    if (read_result < 0) {
        return -errno;
    }

    return read_result;

}

static int partfs_write(const char *path, const char *buf, size_t size,
                        off_t offset, struct fuse_file_info *info)
{
    (void) path;
    off_t lseek_result;
    ssize_t write_result;

    if (((size_t)offset + size) < (size_t)offset) {
        return -EINVAL;
    }

    if ((offset + size) > mount_size) {
        return -EIO;
    }

    lseek_result = lseek(source_fd, offset + source_offset, SEEK_SET);

    if (lseek_result < 0) {
        if (info->direct_io) {
            return lseek_result;
        }
        return -errno;
    }

    if (info->direct_io) {
        return write(source_fd, buf, size);
    }

    write_result = write_count(source_fd, buf, size);

    if (write_result < 0) {
        return -errno;
    }

    return write_result;
}

//----------------------------------------------------------------------------//

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct partfs_config config = {0};
    struct stat stat_buffer;
    int result;

    safecopy(progname, basename(argv[0]), sizeof(progname));

    for(int x = 1; x < argc; x++) {
        if ((strcmp(argv[x], "--help") == 0) || (strcmp(argv[x], "-h") == 0)) {
            exit_help();
        }
    }

    fuse_opt_parse(&args, &config, partfs_opts, partfs_opt_proc);

    if (config.mountpoint[0] == '\x00') {
        fprintf(stderr, "%s: error: mount-point not specified.\n", progname);
        exit(1);
    }

    result = lstat(config.mountpoint, &stat_buffer);

    if (result != 0) {
        fprintf(stderr, "%s: error: couldn't open mount-point [%s] (%s)\n",
                progname, config.mountpoint, strerror(errno));
        exit(1);
    }

    if ((S_ISREG(stat_buffer.st_mode) == 0) || (stat_buffer.st_size != 0)) {
        fprintf(stderr, "%s: error: mount-point is not an empty file.\n",
                progname);
        exit(1);
    }

    if (config.read_only) {
        source_fd = open(config.source, O_RDONLY);
    } else {
        source_fd = open(config.source, O_RDWR);
    }

    if (source_fd < 0) {
        fprintf(stderr, "%s: error: couldn't open file [%s] (%s)\n",
                progname, config.source, strerror(errno));
        exit(1);
    }

    result = stat(config.source, &stat_buffer);

    if (result != 0) {
        fprintf(stderr, "%s: error: couldn't stat file [%s] (%s)\n",
                progname, config.source, strerror(errno));
        exit(1);
    }

    if (config.size == 0) {
        config.size = stat_buffer.st_size - config.offset;
    }

    if ((config.offset + config.size) > (size_t) stat_buffer.st_size) {
        fprintf(stderr, "%s: error: requested size or offset extends past the"
                " end of %s\n", progname, basename(config.source));
        exit(1);
    }

    source_mode = stat_buffer.st_mode;
    read_only = config.read_only;
    mount_size = config.size;
    source_offset = config.offset;

    fuse_opt_add_arg(&args, "-s");

    result = fuse_main(args.argc, args.argv, &partfs_operations, NULL);

    close(source_fd);
    return result;
}

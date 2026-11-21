#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define osd_dir_gcfg czx_unused_upstream_osd_dir_gcfg
#define osd_dir_lcfg czx_unused_upstream_osd_dir_lcfg
#define osd_file_config_init czx_unused_upstream_osd_file_config_init
#define osd_file_config_exit czx_unused_upstream_osd_file_config_exit
#define get_file_access_mode czx_unused_upstream_get_file_access_mode
#define osd_fopen czx_unused_upstream_osd_fopen
#define osd_fclose czx_unused_upstream_osd_fclose
#define osd_fcloseall czx_unused_upstream_osd_fcloseall
#define osd_fflush czx_unused_upstream_osd_fflush
#define osd_rewind czx_unused_upstream_osd_rewind
#define osd_fread_diff czx_unused_upstream_osd_fread_diff
#define osd_fwrite_diff czx_unused_upstream_osd_fwrite_diff
#define osd_fread czx_unused_upstream_osd_fread
#define osd_fwrite czx_unused_upstream_osd_fwrite
#define osd_opendir czx_unused_upstream_osd_opendir
#define osd_readdir czx_unused_upstream_osd_readdir
#define osd_closedir czx_unused_upstream_osd_closedir

#include "LIBRETRO/file-op.c"

#undef osd_closedir
#undef osd_readdir
#undef osd_opendir
#undef osd_fwrite
#undef osd_fread
#undef osd_fwrite_diff
#undef osd_fread_diff
#undef osd_rewind
#undef osd_fflush
#undef osd_fcloseall
#undef osd_fclose
#undef osd_fopen
#undef get_file_access_mode
#undef osd_file_config_exit
#undef osd_file_config_init
#undef osd_dir_lcfg
#undef osd_dir_gcfg

static int czx_alloc_dir_slot(char **slot, const char *defval)
{
    if (*slot == NULL)
    {
        *slot = (char *)malloc(OSD_MAX_FILENAME);
        if (*slot == NULL)
            return FALSE;
        (*slot)[0] = '\0';
        if (defval && strlen(defval) < OSD_MAX_FILENAME)
            strcpy(*slot, defval);
    }
    return TRUE;
}

const char *osd_dir_gcfg(void)
{
    return (dir_g_cfg && dir_g_cfg[0]) ? dir_g_cfg : NULL;
}

const char *osd_dir_lcfg(void)
{
    return (dir_l_cfg && dir_l_cfg[0]) ? dir_l_cfg : NULL;
}

void osd_file_config_exit(void)
{
    free(dir_cwd);   dir_cwd = NULL;
    free(dir_rom);   dir_rom = NULL;
    free(dir_disk);  dir_disk = NULL;
    free(dir_tape);  dir_tape = NULL;
    free(dir_snap);  dir_snap = NULL;
    free(dir_state); dir_state = NULL;
    free(dir_g_cfg); dir_g_cfg = NULL;
    free(dir_l_cfg); dir_l_cfg = NULL;
}

int osd_file_config_init(void)
{
    char cwd_buf[OSD_MAX_FILENAME];
    const char *cwd = getcwd(cwd_buf, sizeof(cwd_buf));
    if (!cwd || cwd[0] == '\0')
        cwd = ".";

    if (!czx_alloc_dir_slot(&dir_cwd, cwd) ||
        !czx_alloc_dir_slot(&dir_rom, "") ||
        !czx_alloc_dir_slot(&dir_disk, "") ||
        !czx_alloc_dir_slot(&dir_tape, "") ||
        !czx_alloc_dir_slot(&dir_snap, "") ||
        !czx_alloc_dir_slot(&dir_state, "") ||
        !czx_alloc_dir_slot(&dir_g_cfg, "") ||
        !czx_alloc_dir_slot(&dir_l_cfg, ""))
    {
        osd_file_config_exit();
        return FALSE;
    }
    return TRUE;
}

uint8_t get_file_access_mode(const char *mode)
{
    uint8_t retro_mode = 0;
    size_t i;

    if (!mode)
        return 0;

    for (i = 0; mode[i] != '\0'; i++)
    {
        switch (mode[i])
        {
        case 'r':
            retro_mode |= RETRO_VFS_FILE_ACCESS_READ;
            break;
        case 'w':
            retro_mode |= RETRO_VFS_FILE_ACCESS_WRITE;
            break;
        case 'a':
            retro_mode |= RETRO_VFS_FILE_ACCESS_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
            break;
        case '+':
            retro_mode |= RETRO_VFS_FILE_ACCESS_READ | RETRO_VFS_FILE_ACCESS_WRITE;
            if (strchr(mode, 'r') != NULL)
                retro_mode |= RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
            break;
        default:
            break;
        }
    }
    return retro_mode;
}

OSD_FILE *osd_fopen(int type, const char *path, const char *mode)
{
    uint8_t i;
    uint8_t stat;
    uint8_t retro_mode;
    bool is_append;
    bool is_shared_type;
    OSD_FILE *current_stream = NULL;

    if (!path || path[0] == '\0' || !mode)
        return NULL;

    retro_mode = get_file_access_mode(mode);
    if (!retro_mode)
        return NULL;

    stat = osd_file_stat(path);
    if (stat == FILE_STAT_DIR)
        return NULL;

    is_append = (strchr(mode, 'a') != NULL);
    if (stat != FILE_STAT_FILE)
    {
        if (retro_mode == RETRO_VFS_FILE_ACCESS_READ)
            return NULL;
        /* Only 'w' or 'a' may create a missing file; 'r+' requires existing file. */
        if ((retro_mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) && !is_append)
            return NULL;
        if (is_append)
            retro_mode &= (uint8_t)~RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
    }

    is_shared_type = (type == FTYPE_DISK ||
                      type == FTYPE_TAPE_LOAD ||
                      type == FTYPE_TAPE_SAVE ||
                      type == FTYPE_PRN ||
                      type == FTYPE_COM_LOAD ||
                      type == FTYPE_COM_SAVE);

    if (stat == FILE_STAT_FILE && is_shared_type)
    {
        for (i = 0; i < MAX_STREAM; i++)
        {
            if (osd_stream[i].fp &&
                osd_stream[i].type == (uint8_t)type &&
                string_is_equal(osd_stream[i].path, path) &&
                (osd_stream[i].mode & retro_mode) == retro_mode)
            {
                osd_fseek(&osd_stream[i], 0, is_append ? SEEK_END : SEEK_SET);
                return &osd_stream[i];
            }
        }
    }

    for (i = 0; i < MAX_STREAM; i++)
    {
        if (osd_stream[i].fp == NULL && osd_stream[i].mem_file == NULL)
        {
            current_stream = &osd_stream[i];
            break;
        }
    }
    if (current_stream == NULL)
        return NULL;

    memset(current_stream, 0, sizeof(*current_stream));
    current_stream->fp = filestream_open(path, retro_mode, 0);
    if (!current_stream->fp)
        return NULL;

    if (stat == FILE_STAT_NOEXIST)
    {
        filestream_flush(current_stream->fp);
        if (osd_file_stat(path) != FILE_STAT_FILE)
        {
            filestream_close(current_stream->fp);
            memset(current_stream, 0, sizeof(*current_stream));
            return NULL;
        }
    }

    current_stream->mode = retro_mode;
    snprintf(current_stream->path, OSD_MAX_FILENAME, "%s", path);
    current_stream->type = (uint8_t)type;

    if (type == FTYPE_DISK && !save_to_disk_image && save_path && save_path[0] != '\0')
    {
        char save_name[OSD_MAX_FILENAME];
        char temp_name[OSD_MAX_FILENAME];
        int written_len;
        uint8_t srm_stat = FILE_STAT_DIR;

        strlcpy(temp_name, path_basename(path), sizeof(temp_name));
        path_remove_extension(temp_name);
        written_len = snprintf(save_name, sizeof(save_name), "%.*s%c%.*s.srm",
                               512, save_path, SLASH, 480, temp_name);
        if (written_len > 0 && (size_t)written_len < sizeof(save_name))
            srm_stat = osd_file_stat(save_name);
        if (srm_stat == FILE_STAT_NOEXIST && (retro_mode & RETRO_VFS_FILE_ACCESS_WRITE))
        {
            if (filestream_write_file(save_name, "", 0))
                srm_stat = FILE_STAT_FILE;
        }
        if (srm_stat == FILE_STAT_FILE)
        {
            uint8_t srm_mode = (retro_mode & RETRO_VFS_FILE_ACCESS_WRITE)
                                   ? (RETRO_VFS_FILE_ACCESS_READ |
                                      RETRO_VFS_FILE_ACCESS_WRITE |
                                      RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING)
                                   : RETRO_VFS_FILE_ACCESS_READ;
            current_stream->sfp = filestream_open(save_name, srm_mode, 0);
        }
    }

    if (is_append)
        osd_fseek(current_stream, 0, SEEK_END);

    return current_stream;
}

int osd_fclose(OSD_FILE *stream)
{
    if (!stream)
        return 0;

    if (stream->type == FTYPE_DISK)
    {
        if (stream->sfp)
            filestream_flush(stream->sfp);
        if (stream->fp)
            filestream_flush(stream->fp);
        return TRUE;
    }

    {
        RFILE *fp = stream->fp;
        RFILE *sfp = stream->sfp;
        int res = 0;

        stream->fp = NULL;
        stream->sfp = NULL;
        stream->mem_file = NULL;
        if (sfp)
            filestream_close(sfp);
        if (fp)
            res = filestream_close(fp);
        return res;
    }
}

void osd_fcloseall(void)
{
    int i;

    for (i = 0; i < MAX_STREAM; i++)
    {
        if (osd_stream[i].fp)
            filestream_close(osd_stream[i].fp);
        if (osd_stream[i].sfp)
            filestream_close(osd_stream[i].sfp);
    }
    memset(osd_stream, 0, sizeof(osd_stream));
}

int osd_fflush(OSD_FILE *stream)
{
    int res = 0;

    if (stream == NULL || stream->fp == NULL)
        return TRUE;
    res = filestream_flush(stream->fp);
    if (stream->sfp)
        filestream_flush(stream->sfp);
    return res;
}

void osd_rewind(OSD_FILE *stream)
{
    (void)osd_fseek(stream, 0L, SEEK_SET);
    osd_fflush(stream);
}

size_t osd_fread_diff(void *ptr, size_t size, OSD_FILE *stream)
{
    int64_t pos;
    int64_t read_bytes;

    if (!stream || (!stream->fp && !stream->mem_file) || size == 0)
        return 0;

    pos = filestream_tell(stream->fp);
    read_bytes = filestream_read(stream->fp, ptr, (int64_t)size);
    if (read_bytes <= 0)
        return 0;

    if (stream->sfp && pos >= 0)
    {
        uint8_t *diff = (uint8_t *)malloc((size_t)read_bytes);
        if (diff)
        {
            int64_t diff_bytes;
            int64_t i;

            filestream_seek(stream->sfp, pos, RETRO_VFS_SEEK_POSITION_START);
            diff_bytes = filestream_read(stream->sfp, diff, read_bytes);
            if (diff_bytes > 0)
            {
                uint8_t *dst = (uint8_t *)ptr;
                for (i = 0; i < diff_bytes; i++)
                    dst[i] = (uint8_t)(dst[i] + diff[i]);
            }
            free(diff);
        }
    }

    return (size_t)read_bytes;
}

size_t osd_fwrite_diff(const void *ptr, uint32_t size, OSD_FILE *stream)
{
    int64_t pos;
    uint8_t *buf;
    int64_t written;
    uint32_t i;
    const uint8_t *src = (const uint8_t *)ptr;

    if (!stream || !stream->fp || !stream->sfp || size == 0)
        return 0;

    pos = filestream_tell(stream->fp);
    if (pos < 0)
        return 0;

    buf = (uint8_t *)calloc(size, 1);
    if (!buf)
        return 0;

    /* Reading advances stream->fp just like a normal write does; any bytes past EOF remain 0. */
    (void)filestream_read(stream->fp, buf, (int64_t)size);
    for (i = 0; i < size; i++)
        buf[i] = (uint8_t)(src[i] - buf[i]);

    filestream_seek(stream->sfp, pos, RETRO_VFS_SEEK_POSITION_START);
    written = filestream_write(stream->sfp, buf, (int64_t)size);
    free(buf);

    return (written > 0) ? (size_t)written : 0;
}

size_t osd_fread(void *ptr, size_t size, size_t nobj, OSD_FILE *stream)
{
    size_t total;
    size_t got = 0;

    if (!stream || size == 0 || nobj == 0)
        return 0;

    total = size * nobj;
    if (SAVE_DIFF)
    {
        got = osd_fread_diff(ptr, total, stream);
    }
    else if (stream->mem_file)
    {
        size_t res = osd_fread_real(stream, ptr, total);
        if (res != (size_t)-1)
            got = res;
    }
    else if (stream->fp)
    {
        int64_t res = filestream_read(stream->fp, ptr, (int64_t)total);
        if (res > 0)
            got = (size_t)res;
    }

    return got / size;
}

size_t osd_fwrite(const void *ptr, size_t size, size_t nobj, OSD_FILE *stream)
{
    size_t total;
    size_t put = 0;

    if (!stream || size == 0 || nobj == 0)
        return 0;

    total = size * nobj;
    if (SAVE_DIFF)
    {
        put = osd_fwrite_diff(ptr, (uint32_t)total, stream);
    }
    else if (stream->mem_file)
    {
        size_t res = osd_fwrite_real(stream, ptr, total);
        if (res != (size_t)-1)
            put = res;
    }
    else if (stream->fp)
    {
        int64_t res = filestream_write(stream->fp, ptr, (int64_t)total);
        if (res > 0)
            put = (size_t)res;
    }

    return put / size;
}

static int czx_dirent_cmp(const void *p1, const void *p2)
{
    const T_DIR_ENTRY *a = (const T_DIR_ENTRY *)p1;
    const T_DIR_ENTRY *b = (const T_DIR_ENTRY *)p2;
    int a_dir = (a->type == FILE_STAT_DIR);
    int b_dir = (b->type == FILE_STAT_DIR);

    if (a_dir != b_dir)
        return a_dir ? -1 : 1;
    if (a_dir)
    {
        if (strcmp(a->name, "..") == 0)
            return -1;
        if (strcmp(b->name, "..") == 0)
            return 1;
    }
    return strcmp(a->name, b->name);
}

T_DIR_INFO *osd_opendir(const char *filename)
{
    DIR *dp;
    struct dirent *de;
    T_DIR_INFO *info;
    int cap = 32;
    size_t dir_len;

    if (!filename || filename[0] == '\0')
        return NULL;

    dp = opendir(filename);
    if (!dp)
        return NULL;

    info = (T_DIR_INFO *)calloc(1, sizeof(*info));
    if (!info)
    {
        closedir(dp);
        return NULL;
    }

    info->entry = (T_DIR_ENTRY *)calloc((size_t)cap, sizeof(T_DIR_ENTRY));
    if (!info->entry)
    {
        free(info);
        closedir(dp);
        return NULL;
    }

    dir_len = strlen(filename);
    while ((de = readdir(dp)) != NULL)
    {
        const char *dname = de->d_name;
        size_t name_len;
        char *full;
        struct stat st;
        int type;
        char *name_copy;
        char *str_copy;

        if (strcmp(dname, ".") == 0)
            continue;

        name_len = strlen(dname);
        full = (char *)malloc(dir_len + name_len + 2);
        if (!full)
            break;
        memcpy(full, filename, dir_len);
        full[dir_len] = '/';
        memcpy(full + dir_len + 1, dname, name_len + 1);

        if (stat(full, &st) != 0)
        {
            free(full);
            continue;
        }
        free(full);

        type = S_ISDIR(st.st_mode) ? FILE_STAT_DIR : FILE_STAT_FILE;
        if (info->nr_entry >= cap)
        {
            int next_cap = cap * 2;
            T_DIR_ENTRY *grown = (T_DIR_ENTRY *)realloc(
                info->entry, (size_t)next_cap * sizeof(T_DIR_ENTRY));
            if (!grown)
                break;
            info->entry = grown;
            cap = next_cap;
        }

        name_copy = (char *)malloc(name_len + 1);
        if (!name_copy)
            break;
        memcpy(name_copy, dname, name_len + 1);

        if (type == FILE_STAT_DIR)
        {
            str_copy = (char *)malloc(name_len + 3);
            if (!str_copy)
            {
                free(name_copy);
                break;
            }
            str_copy[0] = '<';
            memcpy(str_copy + 1, dname, name_len);
            str_copy[name_len + 1] = '>';
            str_copy[name_len + 2] = '\0';
        }
        else
        {
            str_copy = (char *)malloc(name_len + 1);
            if (!str_copy)
            {
                free(name_copy);
                break;
            }
            memcpy(str_copy, dname, name_len + 1);
        }

        info->entry[info->nr_entry].type = type;
        info->entry[info->nr_entry].name = name_copy;
        info->entry[info->nr_entry].str = str_copy;
        info->nr_entry++;
    }

    closedir(dp);

    if (info->nr_entry > 1)
        qsort(info->entry, (size_t)info->nr_entry, sizeof(T_DIR_ENTRY), czx_dirent_cmp);

    return info;
}

T_DIR_ENTRY *osd_readdir(T_DIR_INFO *dirp)
{
    if (!dirp || dirp->cur_entry >= dirp->nr_entry)
        return NULL;
    return &dirp->entry[dirp->cur_entry++];
}

void osd_closedir(T_DIR_INFO *dirp)
{
    int i;

    if (!dirp)
        return;

    for (i = 0; i < dirp->nr_entry; i++)
    {
        free(dirp->entry[i].name);
        free(dirp->entry[i].str);
    }
    free(dirp->entry);
    free(dirp);
}

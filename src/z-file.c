/*
 * File: z-file.c
 * Purpose: Low-level file (and directory) handling
 *
 * Copyright (c) 1997-2007 Ben Harrison, pelpel, Andrew Sidwell, Matthew Jones
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include "z-file.h"
#include "z-virt.h"
#include "z-util.h"
#include "z-form.h"

#ifndef RISCOS
# include <sys/types.h>
#endif

#ifdef WINDOWS
# include <windows.h>
# include <io.h>
# include <direct.h>
#endif

#ifdef MACH_O_CARBON
# include <Carbon/Carbon.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef HAVE_DIRENT_H
# include <sys/types.h>
# include <dirent.h>
#endif

#ifdef HAVE_STAT
# include <sys/stat.h>
# include <sys/types.h>
#endif

#ifdef WINDOWS
# define my_mkdir(path, perms) mkdir(path)
#elif HAVE_MKDIR || MACH_O_CARBON
# define my_mkdir(path, perms) mkdir(path, perms)
#else
# define my_mkdir(path, perms) FALSE
#endif

/*
 * Player info
 */
int player_uid;
int player_egid;




/*
 * Drop permissions
 */
void safe_setuid_drop(void)
{
#ifdef SET_UID
# if defined(HAVE_SETRESGID)

	if (setresgid(-1, getgid(), -1) != 0)
		quit("setegid(): cannot drop permissions correctly!");

# else

	if (setegid(getgid()) != 0)
		quit("setegid(): cannot drop permissions correctly!");

# endif
#endif /* SET_UID */
}


/*
 * Grab permissions
 */
void safe_setuid_grab(void)
{
#ifdef SET_UID
# if defined(HAVE_SETRESGID)

	if (setresgid(-1, player_egid, -1) != 0)
		quit("setegid(): cannot grab permissions correctly!");

# elif defined(HAVE_SETEGID)

	if (setegid(player_egid) != 0)
		quit("setegid(): cannot grab permissions correctly!");

# endif
#endif /* SET_UID */
}




/*
 * Apply special system-specific processing before dealing with a filename.
 */
static void path_parse(char *buf, size_t max, cptr file)
{
#ifndef RISCOS

	/* Accept the filename */
	my_strcpy(buf, file, max);

#else /* RISCOS */

	/* Defined in main-ros.c */
	char *riscosify_name(const char *path);
	my_strcpy(buf, riscosify_name(path), max);

#endif /* !RISCOS */
}



static void path_process(char *buf, size_t len, size_t *cur_len, const char *path)
{
#if defined(SET_UID) || defined(USE_PRIVATE_PATHS)

	/* Home directory on Unixes */
	if (path[0] == '~')
	{
		const char *s;
		const char *username = path + 1;

		struct passwd *pw;
		char user[128];

		/* Look for non-user portion of the file */
		s = strstr(username, PATH_SEP);
		if (s)
		{
			int i;

			/* Keep username a decent length */
			if (s >= username + sizeof(user)) return;

			for (i = 0; username < s; ++i) user[i] = *username++;
			user[i] = '\0';
			username = user;
		}

#ifndef MACH_O_CARBON

		/* Look up a user (or "current" user) */
		pw = username[0] ? getpwnam(username) : getpwuid(getuid());

#else /* MACH_O_CARBON */

		/* On Macs getlogin() can incorrectly return root, so get the username via system frameworks */
		CFStringRef cfusername = CSCopyUserName(TRUE);
		CFIndex cfbufferlength = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfusername), kCFStringEncodingUTF8) + 1;
		char *macusername = mem_alloc(cfbufferlength);
		CFStringGetCString(cfusername, macusername, cfbufferlength, kCFStringEncodingUTF8);
		CFRelease(cfusername);

		/* Look up the user */
		pw = getpwnam(macusername);
		mem_free(macusername);
#endif /* !MACH_O_CARBON */

		if (!pw) return;

		/* Copy across */
		strnfcat(buf, len, cur_len, "%s%s", pw->pw_dir, PATH_SEP);
		if (s) strnfcat(buf, len, cur_len, "%s", s);
	}
	else

#endif

	{
		strnfcat(buf, len, cur_len, "%s", path);
	}
}



/*
 * Create a new path string by appending a 'leaf' to 'base'.
 *
 * On Unixes, we convert a tidle at the beginning of a basename to mean the
 * directory, complicating things a little, but better now than later.
 *
 * Remember to free the return value.
 */
size_t path_build(char *buf, size_t len, const char *base, const char *leaf)
{
	size_t cur_len = 0;
	buf[0] = '\0';

	if (!leaf || !leaf[0])
	{
		if (base && base[0])
			path_process(buf, len, &cur_len, base);

		return cur_len;
	}


	/*
	 * If the leafname starts with the seperator,
	 *   or with the tilde (on Unix),
	 *   or there's no base path,
	 * We use the leafname only.
	 */
#if defined(SET_UID) || defined(USE_PRIVATE_PATHS)
	if ((!base || !base[0]) || prefix(leaf, PATH_SEP) || leaf[0] == '~')
#else
	if ((!base || !base[0]) || prefix(leaf, PATH_SEP))
#endif
	{
		path_process(buf, len, &cur_len, leaf);
		return cur_len;
	}


	/* There is both a relative leafname and a base path from which it is relative */
	path_process(buf, len, &cur_len, base);
	strnfcat(buf, len, &cur_len, "%s", PATH_SEP);
	path_process(buf, len, &cur_len, leaf);

	return cur_len;
}



/*** File-handling API ***/

/* On Windows, fwrite() and fread() are broken. */
#if defined(WINDOWS) || defined(SET_UID)
# define HAVE_WRITE
# define HAVE_READ
#endif


/* Private structure to hold file pointers and useful info. */
struct ang_file
{
	FILE *fh;
	char *fname;
	file_mode mode;
};



/** Utility functions **/

/*
 * Delete file 'fname'.
 */
bool file_delete(const char *fname) 
{
	char buf[1024];

	/* Get the system-specific paths */
	path_parse(buf, sizeof(buf), fname);

	return (remove(buf) == 0);
}

/*
 * Delete file 'fname' to 'newname'.
 */
bool file_move(const char *fname, const char *newname)
{
	char buf[1024];
	char aux[1024];

	/* Get the system-specific paths */
	path_parse(buf, sizeof(buf), fname);
	path_parse(aux, sizeof(aux), newname);

	return (rename(buf, aux) == 0);
}


/*
 * Decide whether a file exists or not.
 */

#if defined(HAVE_STAT)

bool file_exists(const char *fname)
{
	struct stat st;
	return (stat(fname, &st) == 0);
}

#elif defined(WINDOWS)

bool file_exists(const char *fname)
{
	char path[MAX_PATH];
	DWORD attrib;

	/* API says we mustn't pass anything larger than MAX_PATH */
	my_strcpy(path, s, sizeof(path));

	attrib = GetFileAttributes(path);
	if (attrib == INVALID_FILE_NAME) return FALSE;
	if (attrib & FILE_ATTRIBUTE_DIRECTORY) return FALSE;

	return TRUE;
}

#else

bool file_exists(const char *fname)
{
	ang_file *f = file_open(fname, MODE_READ, 0);

	if (f) file_close(f);
	return (f ? TRUE : FALSE);
}

#endif

#ifndef RISCOS

/*
 * Return TRUE if first is newer than second, FALSE otherwise.
 */
bool file_newer(const char *first, const char *second)
{
#ifdef HAVE_STAT
	struct stat stat1, stat2;

	/* If the first doesn't exist, the first is not newer. */
	if (stat(first, &stat1) != 0) return FALSE;

	/* If the second doesn't exist, the first is always newer. */
	if (stat(second, &stat2) != 0) return TRUE;

	/* Compare modification times. */
	return stat1.st_mtime > stat2.st_mtime ? TRUE : FALSE;
#else /* HAVE_STAT */
	return FALSE;
#endif /* !HAVE_STAT */
}

#endif /* RISCOS */




/** File-handle functions **/

/*
 * Open file 'fname', in mode 'mode', with filetype 'ftype'.
 * Returns file handle or NULL.
 */
ang_file *file_open(const char *fname, file_mode mode, file_type ftype)
{
	ang_file *f = ZNEW(ang_file);
	char buf[1024];

	(void)ftype;

	/* Get the system-specific path */
	path_parse(buf, sizeof(buf), fname);

	switch (mode)
	{
		case MODE_WRITE:  f->fh = fopen(buf, "wb"); break;
		case MODE_READ:   f->fh = fopen(buf, "rb"); break;
		case MODE_APPEND: f->fh = fopen(buf, "a+"); break;
		default:          f->fh = fopen(buf, "__");
	}

	if (f->fh == NULL)
	{
		FREE(f);
		return NULL;
	}

	f->fname = string_make(buf);
	f->mode = mode;

#ifdef MACH_O_CARBON
	extern void fsetfileinfo(cptr path, u32b fcreator, u32b ftype);

	/* OS X uses its own kind of filetypes */
	if (mode != MODE_READ)
	{
		u32b mac_type = 'TEXT';

		if (ftype == FTYPE_RAW) mac_type = 'DATA';
		else if (ftype == FTYPE_SAVE) mac_type = 'SAVE';

		fsetfileinfo(buf, 'A271', mac_type);
	}
#endif /* MACH_O_CARBON */

#if defined(RISCOS) && 0
	/* do something for RISC OS here? */
	if (mode != MODE_READ)
		File_SetType(n, ftype);
#endif

	return f;
}


/*
 * Close file handle 'f'.
 */
bool file_close(ang_file *f)
{
	if (fclose(f->fh) != 0)
		return FALSE;

	FREE(f->fname);
	FREE(f);

	return TRUE;
}



/** Locking functions **/

/*
 * Lock a file using POSIX locks, on platforms where this is supported.
 */
void file_lock(ang_file *f)
{
#if defined(HAVE_FCNTL_H) && defined(SET_UID)
	struct flock lock;
	lock.l_type = (f->mode == MODE_READ ? F_RDLCK : F_WRLCK);
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = 0;
	fcntl(fileno(f->fh), F_SETLKW, &lock);
#endif /* HAVE_FCNTL_H && SET_UID */
}

/*
 * Unlock a file locked using file_lock().
 */
void file_unlock(ang_file *f)
{
#if defined(HAVE_FCNTL_H) && defined(SET_UID)
	struct flock lock;
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = 0;
	fcntl(fileno(f->fh), F_SETLK, &lock);
#endif /* HAVE_FCNTL_H && SET_UID */
}


/** Byte-based IO and functions **/

/*
 * Seek to location 'pos' in file 'f'.
 */
bool file_seek(ang_file *f, u32b pos)
{
	return (fseek(f->fh, pos, SEEK_SET) == 0);
}

/*
 * Read a single, 8-bit character from file 'f'.
 */
bool file_readc(ang_file *f, byte *b)
{
	int i = fgetc(f->fh);

	if (i == EOF)
		return FALSE;

	*b = (byte)i;
	return TRUE;
}

/*
 * Write a single, 8-bit character 'b' to file 'f'.
 */
bool file_writec(ang_file *f, byte b)
{
	return file_write(f, (const char *)&b, 1);
}

/*
 * Read 'n' bytes from file 'f' into array 'buf'.
 */

#ifdef HAVE_READ

#ifndef SET_UID
# define READ_BUF_SIZE 16384
#endif

int file_read(ang_file *f, char *buf, size_t n)
{
	int fd = fileno(f->fh);
	int ret;
	int n_read = 0;

#ifndef SET_UID

	while (n >= READ_BUF_SIZE)
	{
		ret = read(fd, buf, READ_BUF_SIZE);
		n_read += ret;

		if (ret == -1)
			return -1;
		else if (ret != READ_BUF_SIZE)
			return n_read;

		buf += READ_BUF_SIZE;
		n -= READ_BUF_SIZE;
	}

#endif /* !SET_UID */

	ret = read(fd, buf, n);
	n_read += ret;

	if (ret == -1)
		return -1;
	else
		return n_read;
}

#else

int file_read(ang_file *f, char *buf, size_t n)
{
	size_t read = fread(buf, 1, n, f->fh);

	if (read == 0 && ferror(f->fh))
		return -1;
	else
		return read;
}

#endif


/*
 * Append 'n' bytes of array 'buf' to file 'f'.
 */

#ifdef HAVE_WRITE

#ifndef SET_UID
# define WRITE_BUF_SIZE 16384
#endif

bool file_write(ang_file *f, const char *buf, size_t n)
{
	int fd = fileno(f->fh);

#ifndef SET_UID

	while (n >= WRITE_BUF_SIZE)
	{
		if (write(fd, buf, WRITE_BUF_SIZE) != WRITE_BUF_SIZE)
			return FALSE;

		buf += WRITE_BUF_SIZE;
		n -= WRITE_BUF_SIZE;
	}

#endif /* !SET_UID */

	if (write(fd, buf, n) != (int)n)
		return FALSE;

	return TRUE;
}

#else

bool file_write(ang_file *f, const char *buf, size_t n)
{
	return (fwrite(buf, 1, n, f->fh) == n);
}

#endif 

/** Line-based IO **/

/*
 * Read a line of text from file 'f' into buffer 'buf' of size 'n' bytes.
 *
 * Support both \r\n and \n as line endings, but not the outdated \r that used
 * to be used on Macs.  Replace non-printables with '?', and \ts with ' '.
 */
#define TAB_COLUMNS 4

bool file_getl(ang_file *f, char *buf, size_t len)
{
	bool seen_cr = FALSE;
	byte b;
	size_t i = 0;

	/* Leave a byte for the terminating 0 */
	size_t max_len = len - 1;

	while (i < max_len)
	{
		char c;

		if (!file_readc(f, &b))
		{
			buf[i] = '\0';
			return (i == 0) ? FALSE : TRUE;
		}

		c = (char) b;

		if (c == '\r')
		{
			seen_cr = TRUE;
			continue;
		}

		if (seen_cr && c != '\n')
		{
			fseek(f->fh, -1, SEEK_CUR);
			buf[i] = '\0';
			return TRUE;
		}

		if (c == '\n')
		{
			buf[i] = '\0';
			return TRUE;
		}

		/* Expand tabs */
		if (c == '\t')
		{
			/* Next tab stop */
			size_t tabstop = ((i + TAB_COLUMNS) / TAB_COLUMNS) * TAB_COLUMNS;
			if (tabstop >= len) break;

			/* Convert to spaces */
			while (i < tabstop)
				buf[i++] = ' ';

			continue;
		}

		/* Ignore non-printables */
		if (!isprint((unsigned char) c))
		{
			buf[i++] = '?';
			continue;
		}

		buf[i++] = c;
	}

	return TRUE;
}

/*
 * Append a line of text 'buf' to the end of file 'f', using system-dependent
 * line ending.
 */
bool file_put(ang_file *f, const char *buf)
{
	return file_write(f, buf, strlen(buf));
}

/*
 * Append a formatted line of text to the end of file 'f'.
 */
bool file_putf(ang_file *f, const char *fmt, ...)
{
	char buf[1024];
	va_list vp;

	va_start(vp, fmt);
	(void)vstrnfmt(buf, sizeof(buf), fmt, vp);
	va_end(vp);

	return file_put(f, buf);
}


bool dir_exists(const char *path)
{
	#ifdef HAVE_STAT
	struct stat buf;
	if (stat(path, &buf) != 0)
		return FALSE;
	else if (buf.st_mode & S_IFDIR)
		return TRUE;
	else
		return FALSE;
	#else
	return TRUE;
	#endif
}

#ifdef HAVE_STAT
bool dir_create(const char *path)
{
	const char *ptr;
	char buf[512];

	/* If the directory already exists then we're done */
	if (dir_exists(path)) return TRUE;

	#ifdef WINDOWS
	/* If we're on windows, we need to skip past the "C:" part. */
	if (isalpha(path[0]) && path[1] == ':') path += 2;
	#endif

	/* Iterate through the path looking for path segements. At each step,
	 * create the path segment if it doesn't already exist. */
	for (ptr = path; *ptr; ptr++)
	{
		if (*ptr == PATH_SEPC)
		{
			/* Find the length of the parent path string */
			size_t len = (size_t)(ptr - path);

			/* Skip the initial slash */
			if (len == 0) continue;

			/* If this is a duplicate path separator, continue */
			if (*(ptr - 1) == PATH_SEPC) continue;

			/* We can't handle really big filenames */
			if (len - 1 > 512) return FALSE;

			/* Create the parent path string, plus null-padding */
			my_strcpy(buf, path, len + 1);

			/* Skip if the parent exists */
			if (dir_exists(buf)) continue;

			/* The parent doesn't exist, so create it or fail */
			if (my_mkdir(buf, 0755) != 0) return FALSE;
		}
	}
	return my_mkdir(path, 0755) == 0 ? TRUE : FALSE;
}

#else /* HAVE_STAT */
bool dir_create(const char *path) { return FALSE; }
#endif /* !HAVE_STAT */

/*** Directory scanning API ***/

/*
 * For information on what these are meant to do, please read the header file.
 */

#ifdef WINDOWS


/* System-specific struct */
struct ang_dir
{
	HANDLE h;
	char *first_file;
};

ang_dir *my_dopen(const char *dirname)
{
	WIN32_FIND_DATA fd;
	HANDLE h;
   	ang_dir *dir;
	
	/* Try to open it */
	h = FindFirstFile(format("%s\\*", dirname), &fd);

	/* Abort */
	if (h == INVALID_HANDLE_VALUE)
		return NULL;

	/* Set up the handle */
	dir = ZNEW(ang_dir);
	dir->h = h;
	dir->first_file = string_make(fd.cFileName);

	/* Success */
	return dir;
}

bool my_dread(ang_dir *dir, char *fname, size_t len)
{
	WIN32_FIND_DATA fd;
	BOOL ok;

	/* Try the first file */
	if (dir->first_file)
	{
		/* Copy the string across, then free it */
		my_strcpy(fname, dir->first_file, len);
		FREE(dir->first_file);

		/* Wild success */
		return TRUE;
	}

	/* Try the next file */
	while (1)
	{
		ok = FindNextFile(dir->h, &fd);
		if (!ok) return FALSE;

		/* Skip directories */
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ||
		    strcmp(fd.cFileName, ".") == 0 ||
		    strcmp(fd.cFileName, "..") == 0)
			continue;

		/* Take this one */
		break;
	}

	/* Copy name */
	my_strcpy(fname, fd.cFileName, len);

	return TRUE;
}

void my_dclose(ang_dir *dir)
{
	/* Close directory */
	if (dir->h)
		FindClose(dir->h);

	/* Free memory */
	FREE(dir->first_file);
	FREE(dir);
}

#endif /* WINDOWS */


#ifdef HAVE_DIRENT_H

/* Define our ang_dir type */
struct ang_dir
{
	DIR *d;
	char *dirname;
};

ang_dir *my_dopen(const char *dirname)
{
	ang_dir *dir;
	DIR *d;

	/* Try to open the directory */
	d = opendir(dirname);
	if (!d) return NULL;

	/* Allocate memory for the handle */
	dir = ZNEW(ang_dir);
	if (!dir) 
	{
		closedir(d);
		return NULL;
	}

	/* Set up the handle */
	dir->d = d;
	dir->dirname = string_make(dirname);

	/* Success */
	return dir;
}

bool my_dread(ang_dir *dir, char *fname, size_t len)
{
	struct dirent *entry;
	struct stat filedata;
	char path[1024];

	assert(dir != NULL);

	/* Try reading another entry */
	while (1)
	{
		entry = readdir(dir->d);
		if (!entry) return FALSE;

		path_build(path, sizeof path, dir->dirname, entry->d_name);
            
		/* Check to see if it exists */
		if (stat(path, &filedata) != 0)
			continue;

		/* Check to see if it's a directory */
		if (S_ISDIR(filedata.st_mode))
			continue;

		/* We've found something worth returning */
		break;
	}

	/* Copy the filename */
	my_strcpy(fname, entry->d_name, len);

	return TRUE;
}

void my_dclose(ang_dir *dir)
{
	/* Close directory */
	if (dir->d)
		closedir(dir->d);

	/* Free memory */
	FREE(dir->dirname);
	FREE(dir);
}

#endif /* HAVE_DIRENT_H */


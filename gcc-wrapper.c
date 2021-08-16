#include	"common.h"

const char *prog_basename = "";

static struct {
	int argc;
	const char *const *argv;
} globals;

typedef struct {
	char **files;
	unsigned long n_files;
	char **misc_args;
	unsigned long n_misc_args;
} argv_data_t;

static argv_data_t analyze_argv(int argc, const char *const argv[])
{
	const char *const *cur = argv + 1, *const *const end = argv + argc;
	argv_data_t data_mem;
	memset(&data_mem, 0, sizeof(data_mem));

	for (; cur < end; cur++) {
		if ((*cur)[0] == '-') {
			if ((*cur)[1] == 'o') {
				/* Does argument's parameter comes in a separate string? */
				if ((*cur)[2] == '\0')
					cur++;
				continue;
			}

			if (((*cur)[1] == 'c' || (*cur)[1] == 'S' || (*cur)[1] == 'E') &&
			    (*cur)[2] == '\0')
				continue;

			if ((*cur)[1] == 'M') {
				switch ((*cur)[2]) {
				case '\0':
					continue;
				case 'M':
					if ((*cur)[3] == '\0' ||
					    ((*cur)[3] == 'D' && (*cur)[4] == '\0'))
						continue;
					break; /* Some spurious arg, keep it. */
				case 'G':
				case 'P':
				case 'D':
					if ((*cur)[3] == '\0')
						continue;
					break; /* Some spurious arg, keep it. */
				case 'F':
				case 'T':
				case 'Q':
					/* Does argument's parameter comes in separate string? */
					if ((*cur)[3] == '\0')
						cur++;
					continue;
				default:
					break;
				}
			}

			if (strcmp(*cur + 1, "fdirectives-only") == 0)
				continue;
		} else {
			unsigned long alen = strlen(*cur);
			const unsigned long sfxlen = sizeof(".c") - 1UL;

			if (alen >= sfxlen &&
			    strcmp(*cur + (alen - sfxlen), ".c") == 0 &&
			    access(*cur, R_OK) == 0) {
				data_mem.files = xrealloc(data_mem.files,
				                          sizeof(*data_mem.files) * ++data_mem.n_files);
				data_mem.files[data_mem.n_files - 1UL] = xstrdup(*cur);
				continue;
			}
		}

		data_mem.misc_args = xrealloc(data_mem.misc_args,
		                              sizeof(*data_mem.misc_args) * ++data_mem.n_misc_args);
		data_mem.misc_args[data_mem.n_misc_args - 1UL] = xstrdup(*cur);
	}

	return data_mem;
}

static void run_cmd(char *argv[])
{
	extern char **environ;
	long pid;
	int rc, status;

	if ((pid = fork()) < 0L) {
		fprintf(stderr,
		        "%s: failed to fork\n",
		        prog_basename);
		_exit(E_PROC);
	}

	if (!pid) {
		/* Inside child */
		execve(argv[0], argv, environ);
		_exit(E_PROC);
	}

	if (waitpid(pid, &status, 0) != pid) {
		kill(pid, SIGTERM);
		fprintf(stderr,
		        "%s: failed to wait for child %ld\n",
		        prog_basename,
		        pid);
		_exit(E_PROC);
	}

	/* Something has gone wrong with a child. */
	if (WIFSIGNALED(status)) {
		fprintf(stderr,
		        "%s: child %ld has exited abnormally (killed by a signal)\n",
		        prog_basename,
		        pid);
		_exit(E_PROC);
	}

	/* Spurious data from the OS kernel:
	   we've requested to wait for child's termination. */
	if (!WIFEXITED(status)) {
		kill(pid, SIGTERM);
		fprintf(stderr,
		        "%s: can't determine exit status of child %ld\n",
		        prog_basename,
		        pid);
		_exit(E_PROC);
	}

	if ((rc = WEXITSTATUS(status)) != 0) {
		fprintf(stderr,
		        "%s: child %ld has returned %d\n",
		        prog_basename,
		        pid,
		        rc);
		_exit(rc);
	}

	; /* Ok, job is done */
}

static void run_gcc(const char *const cc, int argc, const char *const argv[])
{
	const char *const *cur = argv + 1, *const *const end = argv + argc;
	char **new_argv, **p;

	p = new_argv = xmalloc(sizeof(*new_argv) * ((unsigned long) argc + 1UL));
	*p++ = xstrdup(cc);
	for (; cur < end; cur++)
		*p++ = xstrdup(*cur);
	*p = 0;

	run_cmd(new_argv);

	while (--p >= new_argv)
		xfree(*p);
	xfree(new_argv);
}

static dyn_buf_t *finalize_i_files_internal(char *start1,
                                            char *end1,
                                            char *start2,
                                            char *end2)
{
	dyn_buf_t *buf;
	char *p1, *p2, *s1, *s2, *next1, *next2;
	int is_identical, is_init_ws;

	buf = xmalloc(sizeof(*buf));
	dyn_buf_init(buf);

	for (p1 = start1, p2 = start2;
	     p1 < end1 && p2 < end2;
	     p1 = next1, p2 = next2) {
		next1 = s1 = p1, next2 = s2 = p2;
		is_init_ws = 1 << 1 | 1;
		is_identical = 1;
		do {
			if (is_init_ws & 1) {
				while (!is_end_of_line(next1, end1) &&
				       is_whitespace(*next1)) {
					next1++;
					s1++;
				}
				is_init_ws &= ~1;
			} else {
				while (!is_end_of_line(next1, end1) &&
				       is_whitespace(*next1))
					next1++;
			}

			if (is_init_ws & (1 << 1)) {
				while (!is_end_of_line(next2, end2) &&
				       is_whitespace(*next2)) {
					next2++;
					s2++;
				}
				is_init_ws &= ~(1 << 1);
			} else {
				while (!is_end_of_line(next2, end2) &&
				       is_whitespace(*next2))
					next2++;
			}

			is_identical = is_identical &&
			               ((is_end_of_line(next1, end1) &&
			                 is_end_of_line(next2, end2)) ||
			                (!is_end_of_line(next1, end1) &&
			                 !is_end_of_line(next2, end2) &&
			                 *next1 == *next2));

			if (!is_end_of_line(next1, end1))
				next1++;
			if (!is_end_of_line(next2, end2))
				next2++;
		} while (!is_end_of_line(next1, end1) ||
		         !is_end_of_line(next2, end2));

		dyn_buf_printf(buf, "%.*s\n", (int) (next2 - p2), p2);
		if (!is_identical) {
			dyn_buf_printf(buf,
			               "%.*s/* %.*s */\n",
			               (int) (s2 - p2),
			               p2,
			               (int) (next1 - s1),
			               s1);
		}

		if (next1 < end1)
			next1++;
		if (next2 < end2)
			next2++;
	}

	/* Second file contains unexpanded macros. So copy everything remained */
	if (p2 < end2)
		dyn_buf_printf(buf, "%.*s", (int) (end2 - p2), p2);

	return buf;
}

static int finalize_i_files(const char *ifile,
                            const char *ifile_1,
                            const char *ifile_2)
{
	void *base_1, *base_2;
	unsigned long size_1, size_2, new_size_1, new_size_2;
	dyn_buf_t *buf;
	int fd, rc;
	const char *const *p;

	if (load_file(ifile_1, &base_1, &size_1) < 0)
		return -E_IO;

	if (load_file(ifile_2, &base_2, &size_2) < 0) {
		unload_file(base_1, size_1);
		return -E_IO;
	}

	new_size_1 = trim_whitespaces(base_1, size_1);
	new_size_2 = trim_whitespaces(base_2, size_2);

	buf = finalize_i_files_internal(base_1, (char *) base_1 + new_size_1,
	                                base_2, (char *) base_2 + new_size_2);

	unload_file(base_1, size_1);
	unload_file(base_2, size_2);

	if (!buf)
		return -E_MAL_FILE;

	/* Append original ARGV to the end of file in C comment */
	dyn_buf_printf(buf, "\n/*");
	for (p = globals.argv; *p; p++)
		dyn_buf_printf(buf, " %s", *p);
	dyn_buf_printf(buf, " */\n");

	rc = -E_IO;
	if ((fd = open(ifile, O_WRONLY, 0644)) >= 0) {
		long rv;
		unsigned long size;

		size = buf->pos - buf->base;
		size = shrink_lines(buf->base, size);

		if ((rv = write(fd, buf->base, size)) >= 0L &&
		    (unsigned long) rv == size) {
			rc = E_SUCCESS;
		} else {
			unlink(ifile);
		}
		close(fd);
	}

	dyn_buf_free(buf);
	xfree(buf);

	return rc;
}

static int produce_i_files_internal(const char *file, const char *dump_file)
{
	void *base;
	unsigned long size;
	dyn_buf_t *buf;
	int dump_fd;
	int rc;

	if (load_file(file, &base, &size) < 0)
		return -E_IO;

	buf = process_linemarkers(base, size, dump_file);
	unload_file(base, size);
	if (!buf)
		return -E_MAL_FILE;

	rc = -E_IO;
	if ((dump_fd = open(file, O_WRONLY | O_TRUNC)) >= 0) {
		long dump_len = buf->pos - buf->base;

		if (write(dump_fd, buf->base, (unsigned long) dump_len) == dump_len)
			rc = E_SUCCESS;
		else
			unlink(file);
		close(dump_fd);
	}

	dyn_buf_free(buf);
	xfree(buf);

	return rc;
}

static void produce_i_files(const char *const cc,
                            const argv_data_t *const data)
{
	unsigned long i;

	for (i = 0UL; i < data->n_files; i++) {
		const char *file = data->files[i];
		char **argv, **argp;
		char *ifile, *ifile_1, *ifile_2, *dfile;
		char *extra_arg = 0;
		unsigned long sfx_off, j;
		int rc;

		ifile = get_basename(file);
		sfx_off = strlen(ifile);
		ifile = xrealloc(ifile, sfx_off + sizeof("._i_.c")); /* Includes space for '\0' */
		memcpy(ifile + sfx_off, "._i_.c", sizeof("._i_.c"));

		/* Does file already exist? */
		if ((rc = open(ifile, O_CREAT | O_RDWR | O_EXCL, 0644)) >= 0)
			close(rc);
		else
			goto free_ifile;

		argp = argv = xmalloc(sizeof(*argv) * (data->n_misc_args + 10UL));
		*argp++ = xstrdup(cc);
		for (j = 0UL; j < data->n_misc_args; j++)
			*argp++ = xstrdup(data->misc_args[j]);
		*argp++ = xstrdup(file);
		*argp++ = xstrdup("-E");
		*argp++ = xstrdup("-C");
		*argp++ = xstrdup("-dD");
		*argp++ = xstrdup("-dI");
		*argp++ = xstrdup("-o");
		argp[0] = 0; /* For output file */
		argp[1] = 0; /* For possible -fdirectives-only arg */
		argp[2] = 0; /* Trailing ARGV NULL */
		extra_arg = xstrdup("-fdirectives-only");

		ifile_1 = xstrdup(ifile);
		memcpy(ifile_1 + sfx_off, "._i_.1", sizeof("._i_.1"));
		ifile_2 = xstrdup(ifile);
		memcpy(ifile_2 + sfx_off, "._i_.2", sizeof("._i_.2"));
		dfile = xstrdup(ifile);
		memcpy(dfile + sfx_off, "._d_.c", sizeof("._d_.c"));

		argp[0] = ifile_1;
		run_cmd(argv);
		if ((rc = produce_i_files_internal(ifile_1, dfile)) < 0) {
			fprintf(stderr,
			        "%s: error while handling file %s. Code = %d. Skipped\n",
			        prog_basename,
			        ifile_1,
			        -rc);
			goto free_all;
		}

		argp[0] = ifile_2;
		argp[1] = extra_arg;
		run_cmd(argv);
		if ((rc = produce_i_files_internal(ifile_2, 0)) < 0) {
			fprintf(stderr,
			        "%s: error while handling file %s. Code = %d. Skipped\n",
			        prog_basename,
			        ifile_2,
			        -rc);
			unlink(dfile);
			goto rm_ifile_1;
		}

		if ((rc = finalize_i_files(ifile, ifile_1, ifile_2)) < 0) {
			fprintf(stderr,
			        "%s: failed to produce resulting file for %s. Code = %d\n",
			        prog_basename,
			        file,
			        -rc);
			unlink(dfile);
			goto rm_ifile_2;
		}

		;

	rm_ifile_2:
		unlink(ifile_2);
	rm_ifile_1:
		unlink(ifile_1);
	free_all:
		xfree(dfile);
		xfree(ifile_2);
		xfree(ifile_1);
		xfree(extra_arg);
		while (--argp >= argv)
			xfree(*argp);
		xfree(argv);
	free_ifile:
		xfree(ifile);
	}
}

int main(int argc, char *argv[])
{
	const char *cc = getenv("REAL_CC");
	char *resolved_cc;
	argv_data_t data_mem;

	prog_basename = get_basename(argv[0]);
	globals.argc = argc;
	globals.argv = (const char *const *) argv;

	if (!cc)
		cc = "gcc";
	if (!(resolved_cc = locate_bin_file(cc))) {
		fprintf(stderr, "%s: can't locate %s\n", prog_basename, cc);
		xfree((void *) prog_basename);
		prog_basename = "";
		return E_SRCH;
	}

	data_mem = analyze_argv(globals.argc, globals.argv);

	run_gcc(resolved_cc, globals.argc, globals.argv);

	if (!getenv("X_NO_I_FILES"))
		produce_i_files(resolved_cc, &data_mem);

	if (data_mem.files) {
		char **p = data_mem.files + data_mem.n_files;

		while (--p >= data_mem.files)
			xfree(*p);
		xfree(data_mem.files);
	}

	if (data_mem.misc_args) {
		char **p = data_mem.misc_args + data_mem.n_misc_args;

		while (--p >= data_mem.misc_args)
			xfree(*p);
		xfree(data_mem.misc_args);
	}

	memset(&data_mem, 0, sizeof(data_mem));

	xfree(resolved_cc);
	xfree((void *) prog_basename);
	prog_basename = "";

	return E_SUCCESS;
}

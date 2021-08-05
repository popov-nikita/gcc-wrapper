#include	"common.h"

const char *prog_basename = "";

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

static int finalize_i_files(char *file)
{
	char *sfx = file + (strlen(file) - 1UL);
	void *base_1, *base_2;
	unsigned long size_1, size_2, new_size_1, new_size_2;
	long buf_size;
	dyn_buf_t *buf;
	int fd, rc;

	*sfx = '1';
	rc = load_file(file, &base_1, &size_1);
	*sfx = '2';
	if (rc < 0)
		return -E_IO;

	if (load_file(file, &base_2, &size_2) < 0) {
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

	rc = E_SUCCESS;
	buf_size = buf->pos - buf->base;
	*--sfx = '\0';
	if ((fd = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0 ||
	    write(fd, buf->base, (unsigned long) buf_size) != buf_size)
		rc = -E_IO;
	*sfx = '.';

	if (fd >= 0) {
		if (rc != E_SUCCESS) {
			*sfx = '\0';
			unlink(file);
			*sfx = '.';
		}
		close(fd);
	}
	dyn_buf_free(buf);
	xfree(buf);

	return rc;
}

static int produce_i_files_internal(const char *file)
{
	void *base;
	unsigned long size;
	long buf_size;
	dyn_buf_t *buf;
	int fd, rc;

	if (load_file(file, &base, &size) < 0)
		return -E_IO;

	buf = process_linemarkers(base, size);
	unload_file(base, size);
	if (!buf)
		return -E_MAL_FILE;

	rc = E_SUCCESS;
	buf_size = buf->pos - buf->base;
	if ((fd = open(file, O_WRONLY | O_TRUNC)) < 0 ||
	    write(fd, buf->base, (unsigned long) buf_size) != buf_size)
		rc = -E_IO;

	if (fd >= 0)
		close(fd);
	dyn_buf_free(buf);
	xfree(buf);

	return rc;
}

static void produce_i_files(const char *const cc,
                            const argv_data_t *const data)
{
	unsigned long i;

	for (i = 0UL; i < data->n_files; i++) {
		const char *ifile = data->files[i];
		char *ofile, *t;
		char **argv, **p;
		unsigned long ilen = strlen(ifile);
		unsigned long olen = ilen + (sizeof(".i.X") - sizeof(".c"));
		unsigned long j;
		int rc;

		t = ofile = xmalloc(olen + 1UL);
		t += ilen - (sizeof(".c") - 1UL);
		memcpy(ofile, ifile, (unsigned long) (t - ofile));
		*t++ = '.';
		*t++ = 'i';
		*t++ = '.';
		t[0] = '1';
		t[1] = '\0';

		p = argv = xmalloc(sizeof(*argv) * (data->n_misc_args + 10UL));
		*p++ = xstrdup(cc);
		for (j = 0UL; j < data->n_misc_args; j++)
			*p++ = xstrdup(data->misc_args[j]);
		*p++ = xstrdup("-o");
		*p++ = ofile;
		*p++ = xstrdup(ifile);
		*p++ = xstrdup("-E");
		*p++ = xstrdup("-C");
		*p++ = xstrdup("-dD");
		*p++ = xstrdup("-dI");
		*p = 0;

		run_cmd(argv);

		if ((rc = produce_i_files_internal(ofile)) < 0) {
			fprintf(stderr,
			        "%s: error while handling file %s. Code = %d. Skipped\n",
			        prog_basename,
			        ofile,
			        -rc);
			goto next;
		}

		t[0] = '2';
		*p++ = xstrdup("-fdirectives-only");
		*p = 0;

		run_cmd(argv);

		if ((rc = produce_i_files_internal(ofile)) < 0) {
			fprintf(stderr,
			        "%s: error while handling file %s. Code = %d. Skipped\n",
			        prog_basename,
			        ofile,
			        -rc);
			goto next;
		}

		if ((rc = finalize_i_files(ofile)) < 0) {
			fprintf(stderr,
			        "%s: failed to produce resulting file for %s. Code = %d\n",
			        prog_basename,
			        ifile,
			        -rc);
			goto next;
		}

	next:
		if (*t == '2') {
			unlink(ofile);
			*t = '1';
		}
		unlink(ofile);

		while (--p >= argv)
			xfree(*p);
		xfree(argv);
	}
}

int main(int argc, char *argv[])
{
	const char *cc = getenv("REAL_CC");
	char *resolved_cc;
	argv_data_t data_mem;

	prog_basename = get_basename(argv[0]);

	if (!cc)
		cc = "gcc";
	if (!(resolved_cc = locate_bin_file(cc))) {
		fprintf(stderr, "%s: can't locate %s\n", prog_basename, cc);
		xfree((void *) prog_basename);
		prog_basename = "";
		return E_SRCH;
	}

	data_mem = analyze_argv(argc, (const char *const *) argv);

	run_gcc(resolved_cc, argc, (const char *const *) argv);

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

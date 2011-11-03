/*! -lcrypto */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <regex.h>
#include <openssl/sha.h>

static const char *DEFAULT_COMPILER = "clang -Wall -std=c99";

void print_usage(void)
{
	printf("runc: usage: runc <filename>\n");
}

void to_hex(const unsigned char *data, int n, char *str)
{
	for (int i = 0; i < n; i++) {
		sprintf(str + i*2, "%02x", (unsigned int)data[i]);
	}
}

/** returns `true` if the file is readable */
bool file_exists(const char *filename)
{
	FILE *file = fopen(filename, "rb");
	bool exists = file != NULL;
	fclose(file);
	return exists;
}

/** recursively creates directory `dir` 
 * @param mode_t mode passed on to mkdir */
int mkdir_p(const char *dir, mode_t mode)
{
	char *buf = malloc(strlen(dir) + 1);
	if (!buf) return 1;

	for (int i = 0; dir[i-0]; i++) {
		buf[i] = dir[i];
		if (dir[i] != '/') continue;
		buf[i+1] = 0;
		int result = mkdir(buf, mode);

		if (result != 0 && errno != EEXIST && errno != EISDIR) {
			free(buf);
			return result;
		}
	}

	return 0;
}

/** reads a full file into a string 
 * @param len optional output param, length of returned string
 * @return NULL-terminated string with file contents that must be freed by the caller, or NULL on failure */
char *readfilestr(const char *filename, int *len)
{
	FILE *file = fopen(filename, "rb");
	if (!file) return NULL;

	fseek(file, 0, SEEK_END);
	long filesize = ftell(file);
	if (len) *len = filesize;
	rewind(file);

	char *filedata = malloc(filesize + 1);
	filedata[filesize] = 0;
	if (!filedata) {
		fclose(file);
		return NULL;
	}

	size_t result = fread(filedata, 1, filesize, file);
	fclose(file);
	
	if (result != filesize) {
		free(filedata);
		return NULL;
	}
	
	return filedata;	
}

/** hashes the source code
 * @param size size of `data`
 * @return a non-terminated hash of size `SHA_DIGEST_LENGTH` that must be freed by the caller */
unsigned char *source_hash(const char *data, int size)
{
	unsigned char *hash = malloc(SHA_DIGEST_LENGTH);
	if (!hash) return NULL;
	SHA1((unsigned char *)data, size, hash);
	return hash;
}

/** returns the full path to the cache directory that must be freed by the caller */
char *get_cache_path(void)
{
	static const char *path_part = "/.runc/cache/";

	const char *homedir = getenv("HOME");
	if (!homedir) {
		struct passwd *pwd = getpwuid(getuid());
		if (pwd) homedir = pwd->pw_dir;
		if (!homedir) return NULL;
	}

	char *path = malloc(strlen(path_part) + strlen(homedir) + 1);
	strcpy(path, homedir);
	strcat(path, path_part);
	return path;
}

/** returns a path for the output file of the source code with the given hash by combining `cache_path` and an appropriate encoding of `hash`
 * @return a string that must be freed by the caller */
char *get_hash_path(const char *cache_path, const unsigned char *hash)
{
	char *hash_hex = malloc(SHA_DIGEST_LENGTH * 2 + 1);
	if (!hash_hex) return NULL;
	to_hex(hash, SHA_DIGEST_LENGTH, hash_hex);

	char *path = malloc(strlen(cache_path) + strlen(hash_hex) + 1);
	if (!path) {
		free(hash_hex);
		return NULL;
	}

	strcpy(path, cache_path);
	strcat(path, hash_hex);

	free(hash_hex);
	return path;
}

/** extract a hint comment from the source code
 * @return the hint without the surrounding comment marks, to be freed by the caller */
char *extract_hint(const char *sourcecode)
{
	static const char *pattern = "^[:blank:]*/\\*!([^\\*]+)\\*/[:blank:]*$";

	regex_t regex;
	int comp_err = regcomp(&regex, pattern, REG_EXTENDED | REG_NEWLINE);
	if (comp_err != 0) {
		size_t errsize = regerror(comp_err, &regex, NULL, 0);
		char *errstr = malloc(errsize);
		if (!errstr) {
			regfree(&regex);
			return NULL;
		}
		regerror(comp_err, &regex, errstr, errsize);
		printf("runc: error compiling options regex: %s\n", errstr);
		free(errstr);
		return NULL;
	}

	regmatch_t matches[2];
	if (regexec(&regex, sourcecode, 2, matches, 0) != 0) {
		regfree(&regex);
		return NULL;
	}

	char *hint = NULL;
	regmatch_t *hintmatch = &matches[1];
	if (hintmatch->rm_so >= 0) {
		int hintlen = hintmatch->rm_eo - hintmatch->rm_so;
		hint = malloc(hintlen);
		if (!hint) {
			regfree(&regex);
			return NULL;
		}

		strncpy(hint, sourcecode + hintmatch->rm_so, hintlen);
		hint[hintlen] = 0;
	}

	regfree(&regex);
	return hint;
}

/** returns `true` if the hint contains only flags (starting with -) and not a full command line */
bool hint_only_flags(const char *options)
{
	for (const char *c = options; c; c++)	{
		if (*c == '-') return true;
		if (!isspace(*c)) return false;
	}
	return true;
}

bool compile(const char *filename, const char *output, const char *hint, int *result)
{
	static const char *cmdline_fmt = "%s %s \"%s\" -o \"%s\"";
	
	const char *compiler = DEFAULT_COMPILER;
	const char *extra_flags = "";
	if (hint) {
		if (hint_only_flags(hint)) {
			extra_flags = hint;
		} else {
			compiler = hint;
		}
	}
	
	char *cmdline = malloc(strlen(cmdline_fmt) - 8 + strlen(compiler) + strlen(extra_flags) + strlen(filename) + strlen(output) + 1);
	if (!cmdline) return NULL;
	
	sprintf(cmdline, cmdline_fmt, compiler, extra_flags, filename, output);
	printf("%s\n", cmdline);
	*result = system(cmdline);
	free(cmdline);

	return true;
}

bool launch(const char *filename, const int argc, char **argv, int *result)
{
	int cmdline_len = strlen(filename);
	for (int i = 0; i < argc; i++)
		cmdline_len += 3 + strlen(argv[i]);

	char *cmdline = malloc(cmdline_len);
	if (!cmdline) return NULL;

	strcpy(cmdline, filename);
	for (int i = 0; i < argc; i++) {
		strcat(cmdline, " \"");
		strcat(cmdline, argv[i]);
		strcat(cmdline, " \"");
	}
	
	*result = system(cmdline);
	free(cmdline);
	return true;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage();
		return 1;
	}

	char *filename = argv[1];
	int sourcelen;
	char *sourcecode = readfilestr(filename, &sourcelen);
	if (!sourcecode) {
		printf("runc: could not read %s\n", filename);
		return 1;
	}

	unsigned char *hash = source_hash(sourcecode, sourcelen);
	if (!hash) {
		printf("runc: could not compute hash of %s\n", filename);
		free(sourcecode);
		return 1;
	}

	char *cache_path = get_cache_path();
	if (!cache_path) {
		printf("runc: could not get cache path\n");
		free(sourcecode);
		return 1;
	}

	char *out_path = get_hash_path(cache_path, hash);
	free(hash);
	if (!out_path) {
		printf("runc: could not generate output file path for %s\n", filename);
		free(sourcecode);
		free(cache_path);
		return 1;
	}

	if (!file_exists(out_path)) {
		mode_t dir_mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
		if (mkdir_p(cache_path, dir_mode) != 0) {
			printf("runc: could not create cache directory at %s\n", cache_path);
			free(sourcecode);
			free(cache_path);
			free(out_path);
			return 1;
		}

		char *hint = extract_hint(sourcecode);
		int compile_result;
		int compile_ok = compile(filename, out_path, hint, &compile_result);
		free(hint);

		if (!compile_ok) {
			printf("runc: error compiling %s to %s\n", filename, out_path);
			free(sourcecode);
			free(cache_path);
			free(out_path);
		}

		if (compile_result != 0) {
			free(sourcecode);
			free(cache_path);
			free(out_path);
			return compile_result;
		}
	}
	
	free(sourcecode);
	free(cache_path);
	
	int launch_result;
	int launch_ok = launch(out_path, argc - 2, argv + 2, &launch_result);
	free(out_path);
	if (!launch_ok) {
		printf("runc: failed to launch %s\n", out_path);
		return 1;
	}

	return launch_result;
}
/*
 * This file is part of John the Ripper password cracker,
 * Copyright (c) 1996-2000,2003,2005,2010-2012 by Solar Designer
 *
 * ...with heavy changes in the jumbo patch, by magnum and various authors
 */
#if AC_BUILT
#include "autoconfig.h"
#endif

#define LDR_WARN_AMBIGUOUS

#include <stdio.h>
// needs to be above sys/stat.h for mingw, if -std=c99 used.
#include "jumbo.h"
#include <sys/stat.h>
#include "os.h"
#if (!AC_BUILT || HAVE_UNISTD_H) && !_MSC_VER
#include <unistd.h>
#endif
#ifdef _MSC_VER
#define S_ISDIR(a) ((a) & _S_IFDIR)
#endif
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include "arch.h"
#include "misc.h"
#include "params.h"
#include "path.h"
#include "memory.h"
#include "list.h"
#include "signals.h"
#include "formats.h"
#include "dyna_salt.h"
#include "loader.h"
#include "options.h"
#include "config.h"
#include "unicode.h"
#include "dynamic.h"
#include "fake_salts.h"
#include "john.h"
#include "cracker.h"
#include "config.h"
#include "logger.h" /* Beware: log_init() happens after most functions here */
#include "memdbg.h"

#ifdef HAVE_CRYPT
extern struct fmt_main fmt_crypt;
#endif

/*
 * If this is set, we are loading john.pot so we should
 * probably not emit warnings from valid().
 */
int ldr_in_pot = 0;

/*
 * Flags for read_file().
 */
#define RF_ALLOW_MISSING		1
#define RF_ALLOW_DIR			2

/*
 * Fast "Strlen" for fields[f]
 */
#define SPLFLEN(f)	(fields[f][0] ? fields[f+1] - fields[f] - 1 : 0)

static char *no_username = "?";
static int pristine_gecos;

/* There should be legislation against adding a BOM to UTF-8 */
static char *skip_bom(char *string)
{
	if (!memcmp(string, "\xEF\xBB\xBF", 3))
		string += 3;
	return string;
}

static void read_file(struct db_main *db, char *name, int flags,
	void (*process_line)(struct db_main *db, char *line))
{
	struct stat file_stat;
	FILE *file;
	char line_buf[LINE_BUFFER_SIZE], *line;
	int warn = cfg_get_bool(SECTION_OPTIONS, NULL, "WarnEncoding", 0);

	if (!john_main_process)
		warn = 0;

	if (flags & RF_ALLOW_DIR) {
		if (stat(name, &file_stat)) {
			if (flags & RF_ALLOW_MISSING)
				if (errno == ENOENT) return;
			pexit("stat: %s", path_expand(name));
		} else
			if (S_ISDIR(file_stat.st_mode)) return;
	}

	if (!(file = fopen(path_expand(name), "r"))) {
		if ((flags & RF_ALLOW_MISSING) && errno == ENOENT) return;
		pexit("fopen: %s", path_expand(name));
	}

	dyna_salt_init(db->format);
	while (fgets(line_buf, sizeof(line_buf), file)) {
		line = skip_bom(line_buf);

		if (warn) {
			char *u8check;

			if (!(flags & RF_ALLOW_MISSING) ||
			    !(u8check =
			      strchr(line, options.loader.field_sep_char)))
				u8check = line;

			if (((flags & RF_ALLOW_MISSING) &&
			     pers_opts.store_utf8) ||
			    ((flags & RF_ALLOW_DIR) &&
			     pers_opts.input_enc == UTF_8)) {
				if (!valid_utf8((UTF8*)u8check)) {
					warn = 0;
					fprintf(stderr, "Warning: invalid UTF-8"
					        " seen reading %s\n", name);
				}
			} else if (pers_opts.input_enc != UTF_8 &&
			           (line != line_buf ||
			            valid_utf8((UTF8*)u8check) > 1)) {
				warn = 0;
				fprintf(stderr, "Warning: UTF-8 seen reading "
				        "%s\n", name);
			}
		}
		process_line(db, line);
		check_abort(0);
	}
	if (name == pers_opts.activepot)
		crk_pot_pos = jtr_ftell64(file);

	if (ferror(file)) pexit("fgets");

	if (fclose(file)) pexit("fclose");
}

void ldr_init_database(struct db_main *db, struct db_options *options)
{
	db->loaded = 0;

	db->options = mem_alloc_copy(options,
	    sizeof(struct db_options), MEM_ALIGN_WORD);

	db->salts = NULL;

	db->password_hash = NULL;
	db->password_hash_func = NULL;

	if (options->flags & DB_CRACKED) {
		db->salt_hash = NULL;

		db->cracked_hash = mem_alloc(
			CRACKED_HASH_SIZE * sizeof(struct db_cracked *));
		memset(db->cracked_hash, 0,
			CRACKED_HASH_SIZE * sizeof(struct db_cracked *));
	} else {
		db->salt_hash = mem_alloc(
			SALT_HASH_SIZE * sizeof(struct db_salt *));
		memset(db->salt_hash, 0,
			SALT_HASH_SIZE * sizeof(struct db_salt *));

		db->cracked_hash = NULL;

		if (options->flags & DB_WORDS)
			options->flags |= DB_LOGIN;

	}

	list_init(&db->plaintexts);

	db->salt_count = db->password_count = db->guess_count = 0;

	db->format = NULL;
}

/*
 * Allocate a hash table for use by the loader itself.  We use this for two
 * purposes: to detect and avoid loading of duplicate hashes when DB_WORDS is
 * not set, and to remove previously-cracked hashes (found in john.pot).  We
 * allocate, use, and free this hash table prior to deciding on the sizes of
 * and allocating the per-salt hash tables to be used while cracking.
 */
static void ldr_init_password_hash(struct db_main *db)
{
	int (*func)(void *binary);
	int size = PASSWORD_HASH_SIZE_FOR_LDR;

	if (size > 0 && mem_saving_level >= 2)
		size--;

	do {
		func = db->format->methods.binary_hash[size];
		if (func && func != fmt_default_binary_hash)
			break;
	} while (--size >= 0);
	if (size < 0)
		size = 0;
	db->password_hash_func = func;
	size = password_hash_sizes[size] * sizeof(struct db_password *);
	db->password_hash = mem_alloc(size);
	memset(db->password_hash, 0, size);
}

static char *ldr_get_field(char **ptr, char field_sep_char)
{
	static char *last;
	char *res, *pos;

	if (!*ptr) return last;

	if ((pos = strchr(res = *ptr, field_sep_char))) {
		*pos++ = 0; *ptr = pos;
	} else {
		pos = res;
		do {
			if (*pos == '\r' || *pos == '\n') *pos = 0;
		} while (*pos++);
		last = pos - 1;
		*ptr = NULL;
	}

	return res;
}

static int ldr_check_list(struct list_main *list, char *s1, char *s2)
{
	struct list_entry *current;
	char *data;

	if (!(current = list->head)) return 0;

	if (*current->data == '-') {
		data = current->data + 1;
		do {
			if (!strcmp(s1, data) || !strcmp(s2, data)) return 1;
			if ((current = current->next)) data = current->data;
		} while (current);
	} else {
		do {
			data = current->data;
			if (!strcmp(s1, data) || !strcmp(s2, data)) return 0;
		} while ((current = current->next));
		return 1;
	}

	return 0;
}

static int ldr_check_shells(struct list_main *list, char *shell)
{
	char *name;

	if (list->head) {
		if ((name = strrchr(shell, '/'))) name++; else name = shell;
		return ldr_check_list(list, shell, name);
	}

	return 0;
}

static void ldr_set_encoding(struct fmt_main *format)
{
	if ((!pers_opts.target_enc || pers_opts.default_target_enc) &&
	    !pers_opts.internal_enc) {
		if (!strcasecmp(format->params.label, "LM") ||
		    !strcasecmp(format->params.label, "netlm") ||
		    !strcasecmp(format->params.label, "nethalflm")) {
			pers_opts.target_enc =
				cp_name2id(cfg_get_param(SECTION_OPTIONS,
				                         NULL,
				                         "DefaultMSCodepage"));
			if (pers_opts.target_enc)
				pers_opts.default_target_enc = 1;
			else
				pers_opts.target_enc = pers_opts.input_enc;
		} else if (pers_opts.internal_enc &&
		           (format->params.flags & FMT_UNICODE) &&
		           (format->params.flags & FMT_UTF8)) {
			pers_opts.target_enc = pers_opts.internal_enc;
		}
	}

	/* For FMT_NOT_EXACT, --show=left should only list hashes we
	   did not find any candidate for */
	if (options.loader.showuncracked)
		format->params.flags &= ~FMT_NOT_EXACT;

	if ((options.flags & FLG_SHOW_CHK) || options.loader.showuncracked) {
		initUnicode(UNICODE_UNICODE);
		return;
	}

	/* john.conf alternative for --internal-encoding */
	if (options.flags &
	    (FLG_RULES | FLG_SINGLE_CHK | FLG_BATCH_CHK | FLG_MASK_CHK))
	if ((!pers_opts.target_enc || pers_opts.target_enc == UTF_8) &&
	    !pers_opts.internal_enc) {
		if (!(pers_opts.internal_enc =
		      cp_name2id(cfg_get_param(SECTION_OPTIONS, NULL,
		                               "DefaultInternalEncoding"))))
			/* Deprecated alternative */
			pers_opts.internal_enc =
				cp_name2id(cfg_get_param(SECTION_OPTIONS, NULL,
				               "DefaultIntermediateEncoding"));
	}

	/* Performance opportunity - avoid unneccessary conversions */
	if (pers_opts.internal_enc && pers_opts.internal_enc != UTF_8 &&
	    (!pers_opts.target_enc || pers_opts.target_enc == UTF_8)) {
		if ((format->params.flags & FMT_UNICODE) &&
		    (format->params.flags & FMT_UTF8))
			pers_opts.target_enc = pers_opts.internal_enc;
	}

	initUnicode(UNICODE_UNICODE);
}

static int ldr_split_line(char **login, char **ciphertext,
	char **gecos, char **home, char **uid,
	char *source, struct fmt_main **format,
	struct db_options *db_opts, char *line)
{
	struct fmt_main *alt;
	char *fields[10], *gid, *shell;
	int i, retval;

	fields[0] = *login = ldr_get_field(&line, db_opts->field_sep_char);
	fields[1] = *ciphertext = ldr_get_field(&line, db_opts->field_sep_char);

/* Check for NIS stuff */
	if ((!strcmp(*login, "+") || !strncmp(*login, "+@", 2)) &&
	    strlen(*ciphertext) < 10 && strncmp(*ciphertext, "$dummy$", 7)
	    && strncmp(*ciphertext, "$0$", 3))
		return 0;

	if (!**ciphertext && !line) {
/* Possible hash on a line on its own (no colons) */
		char *p = *login;
/* Skip leading and trailing whitespace */
		while (*p == ' ' || *p == '\t') p++;
		*ciphertext = p;
		p += strlen(p) - 1;
		while (p > *ciphertext && (*p == ' ' || *p == '\t')) p--;
		p++;
/* Some valid dummy or plaintext hashes may be shorter than 10 characters,
 * so don't subject them to the length checks. */
		if (strncmp(*ciphertext, "$dummy$", 7) &&
		    strncmp(*ciphertext, "$0$", 3) &&
		    p - *ciphertext != 10 /* not tripcode */) {
/* Check for a special case: possibly a traditional crypt(3) hash with
 * whitespace in its invalid salt.  Only support such hashes at the very start
 * of a line (no leading whitespace other than the invalid salt). */
			if (p - *ciphertext == 11 && *ciphertext - *login == 2)
				(*ciphertext)--;
			if (p - *ciphertext == 12 && *ciphertext - *login == 1)
				(*ciphertext)--;
			if (p - *ciphertext < 13)
				return 0;
		}
		*p = 0;
		fields[0] = *login = no_username;
		fields[1] = *ciphertext;
	}

	if (source)
		strcpy(source, line ? line : "");

/*
 * This check is just a loader performance optimization, so that we can parse
 * fewer fields when we know we won't need the rest.  It should be revised or
 * removed when there are formats that use higher-numbered fields in prepare().
 */
	if ((db_opts->flags & DB_WORDS) || db_opts->shells->head) {
		/* Parse all fields */
		for (i = 2; i < 10; i++)
			fields[i] = ldr_get_field(&line,
			                          db_opts->field_sep_char);
	} else {
		/* Parse some fields only */
		for (i = 2; i < 4; i++)
			fields[i] = ldr_get_field(&line,
			                          db_opts->field_sep_char);
		// Next line needed for l0phtcrack (in Jumbo)
		for (; i < 6; i++)
			fields[i] = ldr_get_field(&line,
			                          db_opts->field_sep_char);
		for (; i < 10; i++)
			fields[i] = "/";
	}

	/* /etc/passwd */
	*uid = fields[2];
	gid = fields[3];
	*gecos = fields[4];
	*home = fields[5];
	shell = fields[6];

	if (SPLFLEN(2) == 32 || SPLFLEN(3) == 32) {
		/* PWDUMP */
		/* user:uid:LMhash:NThash:comment:homedir: */
		*uid = fields[1];
		*ciphertext = fields[2];
		if (!strncmp(*ciphertext, "NO PASSWORD", 11))
			*ciphertext = "";
		gid = shell = "";
		*gecos = fields[4];
		*home = fields[5];

		/* Re-introduce the previously removed uid field */
		if (source) {
			int shift = strlen(*uid);
			memmove(source + shift + 1, source, strlen(source) + 1);
			memcpy(source, *uid, shift);
			source[shift] = db_opts->field_sep_char;
		}
	}
	else if (SPLFLEN(1) == 0 && SPLFLEN(3) >= 16 && SPLFLEN(4) >= 32 &&
	         SPLFLEN(5) >= 16) {
		/* l0phtcrack-style input
		   user:::lm response:ntlm response:challenge
		   user::domain:srvr challenge:ntlmv2 response:client challenge
		 */
		*uid = gid = *home = shell = "";
		*gecos = fields[2]; // in case there's a domain name here
	}
	else if (fields[5][0] != '/' &&
	    ((!strcmp(fields[5], "0") && !strcmp(fields[6], "0")) ||
	    fields[8][0] == '/' ||
	    fields[9][0] == '/')) {
		/* /etc/master.passwd */
		*gecos = fields[7];
		*home = fields[8];
		shell = fields[9];
	}

	if (ldr_check_list(db_opts->users, *login, *uid)) return 0;
	if (ldr_check_list(db_opts->groups, gid, gid)) return 0;
	if (ldr_check_shells(db_opts->shells, shell)) return 0;

	if (*format) {
		char *prepared;
		int valid;

		prepared = (*format)->methods.prepare(fields, *format);
		if (prepared)
			valid = (*format)->methods.valid(prepared, *format);
		else
			valid = 0;

		if (valid) {
			*ciphertext = prepared;
			return valid;
		}

		ldr_set_encoding(*format);

		alt = fmt_list;
		do {
			if (alt == *format)
				continue;
			if (alt->params.flags & FMT_WARNED)
				continue;
			/* Format disabled in john.conf */
			if (cfg_get_bool(SECTION_DISABLED, SUBSECTION_FORMATS,
			                 alt->params.label, 0))
				continue;
#ifdef HAVE_CRYPT
			if (alt == &fmt_crypt &&
#ifdef __sun
			    strncmp(*ciphertext, "$md5$", 5) &&
			    strncmp(*ciphertext, "$md5,", 5) &&
#endif
			    strncmp(*ciphertext, "$5$", 3) &&
			    strncmp(*ciphertext, "$6$", 3))
				continue;
#endif
			prepared = alt->methods.prepare(fields, alt);
			if (alt->methods.valid(prepared, alt)) {
				alt->params.flags |= FMT_WARNED;
				if (john_main_process)
				fprintf(stderr,
				    "Warning: only loading hashes of type "
				    "\"%s\", but also saw type \"%s\"\n"
				    "Use the \"--format=%s\" option to force "
				    "loading hashes of that type instead\n",
				    (*format)->params.label,
				    alt->params.label,
				    alt->params.label);
				break;
			}
		} while ((alt = alt->next));

		return 0;
	}

	retval = -1;
	if ((alt = fmt_list))
	do {
		char *prepared;
		int valid;

		/* Format disabled in john.conf, unless forced */
		if (fmt_list->next &&
		    cfg_get_bool(SECTION_DISABLED, SUBSECTION_FORMATS,
		                 alt->params.label, 0))
			continue;

#ifdef HAVE_CRYPT
/*
 * Only probe for support by the current system's crypt(3) if this is forced
 * from the command-line or/and if the hash encoding string looks like one of
 * those that are only supported in that way.  Avoid the probe in other cases
 * because it may be slow and undesirable (false detection is possible).
 */
		if (alt == &fmt_crypt &&
		    fmt_list != &fmt_crypt /* not forced */ &&
#ifdef __sun
		    strncmp(*ciphertext, "$md5$", 5) &&
		    strncmp(*ciphertext, "$md5,", 5) &&
#endif
		    strncmp(*ciphertext, "$5$", 3) &&
		    strncmp(*ciphertext, "$6$", 3))
			continue;
#endif

		prepared = alt->methods.prepare(fields, alt);
		if (!prepared)
			continue;
		valid = alt->methods.valid(prepared, alt);
		if (!valid)
			continue;

		if (retval < 0) {
			retval = valid;
			*ciphertext = prepared;
			ldr_set_encoding(alt);
#ifdef HAVE_OPENCL
			if (options.gpu_devices->count && options.fork &&
			    strstr(alt->params.label, "-opencl"))
				*format = alt;
			else
#endif
			fmt_init(*format = alt);
#ifdef LDR_WARN_AMBIGUOUS
			if (!source) /* not --show */
				continue;
#endif
			break;
		}
#ifdef LDR_WARN_AMBIGUOUS
		if (john_main_process)
		fprintf(stderr,
		    "Warning: detected hash type \"%s\", but the string is "
		    "also recognized as \"%s\"\n"
		    "Use the \"--format=%s\" option to force loading these "
		    "as that type instead\n",
		    (*format)->params.label, alt->params.label,
		    alt->params.label);
#endif
	} while ((alt = alt->next));

	return retval;
}

static char* ldr_conv(char *word)
{
	if (pers_opts.input_enc == UTF_8 && pers_opts.target_enc != UTF_8) {
		static char u8[PLAINTEXT_BUFFER_SIZE + 1];

		word = utf8_to_cp_r(word, u8, PLAINTEXT_BUFFER_SIZE);
	}
	return word;
}

static void ldr_split_string(struct list_main *dst, char *src)
{
	char *word, *pos;
	char c;

	pos = src;
	do {
		word = pos;
		while (*word && CP_isSeparator[ARCH_INDEX(*word)]) word++;
		if (!*word) break;

		pos = word;
		while (!CP_isSeparator[ARCH_INDEX(*pos)]) pos++;
		c = *pos;
		*pos = 0;
		list_add_unique(dst, word);
		*pos++ = c;
	} while (c && dst->count < LDR_WORDS_MAX);
}

static struct list_main *ldr_init_words(char *login, char *gecos, char *home)
{
	struct list_main *words;
	char *pos;

	list_init(&words);

	if (*login && login != no_username)
		list_add(words, ldr_conv(login));
	ldr_split_string(words, ldr_conv(gecos));
	if (login != no_username)
		ldr_split_string(words, ldr_conv(login));
	if (pristine_gecos && *gecos)
		list_add_unique(words, ldr_conv(gecos));

	if ((pos = strrchr(home, '/')) && pos[1])
		list_add_unique(words, ldr_conv(&pos[1]));

	return words;
}

static void ldr_load_pw_line(struct db_main *db, char *line)
{
	static int skip_dupe_checking = 0;
	struct fmt_main *format;
	int index, count;
	char *login, *ciphertext, *gecos, *home, *uid;
	char *piece;
	void *binary, *salt;
	int salt_hash, pw_hash;
	struct db_salt *current_salt, *last_salt;
	struct db_password *current_pw, *last_pw;
	struct list_main *words;
	size_t pw_size, salt_size;
#if FMT_MAIN_VERSION > 11
	int i;
#endif

	count = ldr_split_line(&login, &ciphertext, &gecos, &home, &uid,
		NULL, &db->format, db->options, line);
	if (count <= 0) return;
	if (count >= 2) db->options->flags |= DB_SPLIT;

	format = db->format;
	dyna_salt_init(format);

	words = NULL;

	if (db->options->flags & DB_WORDS) {
		pw_size = sizeof(struct db_password);
		salt_size = sizeof(struct db_salt);
	} else {
		if (db->options->flags & DB_LOGIN)
			pw_size = sizeof(struct db_password) -
				sizeof(struct list_main *);
		else
			pw_size = sizeof(struct db_password) -
				(sizeof(char *) + sizeof(struct list_main *));
		salt_size = sizeof(struct db_salt) -
			sizeof(struct db_keys *);
	}

	if (!db->password_hash) {
		ldr_init_password_hash(db);
		if (cfg_get_bool(SECTION_OPTIONS, NULL,
		                 "NoLoaderDupeCheck", 0)) {
			skip_dupe_checking = 1;
			if (john_main_process)
				fprintf(stderr, "No dupe-checking performed "
				        "when loading hashes.\n");
		}
	}

	for (index = 0; index < count; index++) {
		piece = format->methods.split(ciphertext, index, format);

		binary = format->methods.binary(piece);
		pw_hash = db->password_hash_func(binary);

		if (options.flags & FLG_REJECT_PRINTABLE) {
			int i = 0;

			while (isprint((int)((unsigned char*)binary)[i]) &&
			       i < format->params.binary_size)
				i++;

			if (i == format->params.binary_size) {
				if (john_main_process)
				fprintf(stderr, "rejecting printable binary"
				        " \"%.*s\" (%s)\n",
				        format->params.binary_size,
				        (char*)binary, piece);
				break;
			}
		}

		if (!(db->options->flags & DB_WORDS) && !skip_dupe_checking) {
			int collisions = 0;
			if ((current_pw = db->password_hash[pw_hash]))
			do {
				if (!memcmp(binary, current_pw->binary,
				    format->params.binary_size) &&
				    !strcmp(piece, format->methods.source(
				    current_pw->source, current_pw->binary))) {
					db->options->flags |= DB_NODUP;
					break;
				}
				if (++collisions <= LDR_HASH_COLLISIONS_MAX)
					continue;

				if (john_main_process) {
					if (format->params.binary_size)
					fprintf(stderr, "Warning: "
					    "excessive partial hash "
					    "collisions detected\n%s",
					    db->password_hash_func !=
					    fmt_default_binary_hash ? "" :
					    "(cause: the \"format\" lacks "
					    "proper binary_hash() function "
					    "definitions)\n");
					else
					fprintf(stderr, "Warning: "
					    "check for duplicates partially "
					    "bypassed to speedup loading\n");
				}
				skip_dupe_checking = 1;
				current_pw = NULL; /* no match */
				break;
			} while ((current_pw = current_pw->next_hash));

			if (current_pw) continue;
		}

		salt = format->methods.salt(piece);
		dyna_salt_create(salt);
		salt_hash = format->methods.salt_hash(salt);

		if ((current_salt = db->salt_hash[salt_hash])) {
			do {
				if (!dyna_salt_cmp(current_salt->salt, salt, format->params.salt_size))
					break;
			}  while ((current_salt = current_salt->next));
		}

		if (!current_salt) {
			last_salt = db->salt_hash[salt_hash];
			current_salt = db->salt_hash[salt_hash] =
				mem_alloc_tiny(salt_size, MEM_ALIGN_WORD);
			current_salt->next = last_salt;

			current_salt->salt = mem_alloc_copy(salt,
				format->params.salt_size,
				format->params.salt_align);

#if FMT_MAIN_VERSION > 11
			for (i = 0; i < FMT_TUNABLE_COSTS && format->methods.tunable_cost_value[i] != NULL; ++i)
				current_salt->cost[i] = format->methods.tunable_cost_value[i](current_salt->salt);
#endif

			current_salt->index = fmt_dummy_hash;
			current_salt->bitmap = NULL;
			current_salt->list = NULL;
			current_salt->hash = &current_salt->list;
			current_salt->hash_size = -1;

			current_salt->count = 0;

			if (db->options->flags & DB_WORDS)
				current_salt->keys = NULL;

			db->salt_count++;
		} else
			dyna_salt_remove(salt);

		current_salt->count++;
		db->password_count++;

		last_pw = current_salt->list;
		current_pw = current_salt->list = mem_alloc_tiny(
			pw_size, MEM_ALIGN_WORD);
		current_pw->next = last_pw;

		last_pw = db->password_hash[pw_hash];
		db->password_hash[pw_hash] = current_pw;
		current_pw->next_hash = last_pw;

/* If we're not going to use the source field for its usual purpose, see if we
 * can pack the binary value in it. */
		if (format->methods.source != fmt_default_source &&
		    sizeof(current_pw->source) >= format->params.binary_size)
			current_pw->binary = memcpy(&current_pw->source,
				binary, format->params.binary_size);
		else
			current_pw->binary = mem_alloc_copy(binary,
				format->params.binary_size,
				format->params.binary_align);

		if (format->methods.source == fmt_default_source)
			current_pw->source = str_alloc_copy(piece);

		if (db->options->flags & DB_WORDS) {
			if (!words)
				words = ldr_init_words(login, gecos, home);
			current_pw->words = words;
		}

		if (db->options->flags & DB_LOGIN) {
			if (login != no_username && index == 0)
				login = ldr_conv(login);

			current_pw->uid = "";
			if (count >= 2 && count <= 9) {
				current_pw->login = mem_alloc_tiny(
					strlen(login) + 3, MEM_ALIGN_NONE);
				sprintf(current_pw->login, "%s:%d",
					login, index + 1);
				current_pw->uid = str_alloc_copy(uid);
			} else
			if (login == no_username)
				current_pw->login = login;
			else
			if (words && *login)
				current_pw->login = words->head->data;
			else {
				current_pw->login = str_alloc_copy(login);
				current_pw->uid = str_alloc_copy(uid);
			}
		}
	}
}

void ldr_load_pw_file(struct db_main *db, char *name)
{
	pristine_gecos = cfg_get_bool(SECTION_OPTIONS, NULL,
	        "PristineGecos", 0);

	read_file(db, name, RF_ALLOW_DIR, ldr_load_pw_line);
}

static void ldr_load_pot_line(struct db_main *db, char *line)
{
	struct fmt_main *format = db->format;
	char *ciphertext;
	void *binary;
	int hash;
	struct db_password *current;

	ciphertext = ldr_get_field(&line, db->options->field_sep_char);
	if (format->methods.valid(ciphertext, format) != 1) return;
	ciphertext = format->methods.split(ciphertext, 0, format);
	binary = format->methods.binary(ciphertext);
	hash = db->password_hash_func(binary);

	if ((current = db->password_hash[hash]))
	do {
		if (options.regen_lost_salts)
			ldr_pot_possible_fixup_salt(current->source,
			                            ciphertext);
		if (!current->binary) /* already marked for removal */
			continue;
		if (memcmp(binary, current->binary, format->params.binary_size))
			continue;
		if (strcmp(ciphertext,
		    format->methods.source(current->source, current->binary)))
			continue;
		current->binary = NULL; /* mark for removal */
	} while ((current = current->next_hash));
}

void ldr_load_pot_file(struct db_main *db, char *name)
{
	if (db->format && !(db->format->params.flags & FMT_NOT_EXACT)) {
#ifdef HAVE_CRYPT
		ldr_in_pot = 1;
#endif
		read_file(db, name, RF_ALLOW_MISSING, ldr_load_pot_line);
#ifdef HAVE_CRYPT
		ldr_in_pot = 0;
#endif
	}
}

/*
 * The following are several functions called by ldr_fix_database().
 * They assume that the per-salt hash tables have not yet been initialized.
 */

/*
 * Glue the salt_hash[] buckets together and into the salts list.  The loader
 * needs the hash table, but then we free it and the cracker uses the list.
 */
static void ldr_init_salts(struct db_main *db)
{
	struct db_salt **tail, *current;
	int hash, ctr = 0;

	for (hash = 0, tail = &db->salts; hash < SALT_HASH_SIZE; hash++)
	if ((current = db->salt_hash[hash])) {
		*tail = current;
		ctr = 0;
		do {
			current -> sequential_id = ctr++;
			tail = &current->next;
		} while ((current = current->next));
#ifdef DEBUG_HASH
		if (ctr)
			printf("salt hash %08x, %d salts\n", hash, ctr);
#endif
	}
}

/* #define DEBUG_SALT_SORT */

/* Default: Most used salts first */
static int salt_compare_num(int a, int b)
{
	if (a > b) return -1;
	if (a < b) return 1;
	return 0;
}

/*
 * This was done as a structure to allow more data to be
 * placed into it, beyond just the simple pointer. The
 * pointer is really all that is needed.  However, when
 * building with the structure containing just a pointer,
 * we get no (or very little) degredation over just an
 * array of pointers.  The compiler treats them the same.
 * so for ease of debugging, I have left this as a simple
 * structure
 */
typedef struct salt_cmp_s
{
	struct db_salt *p;
#ifdef DEBUG_SALT_SORT
	/* used by JimF in debugging.  Left in for now */
	int org_idx;
	char str[36];
#endif
} salt_cmp_t;

/*
 * there is no way to pass this pointer to the sort function, so
 * we set it before calling sort.
 */
static int (*fmt_salt_compare)(const void *x, const void *y);

/*
 * This helper function will stay in loader.  It is what the qsort
 * function calls.  This function is smart enough to know how to
 * parse a salt_cmp_t. It does that, to find the real salt values,
 * and then calls the formats salt_compare passing in just the salts.
 * It is an extra layer of indirection, but keeps the function from
 * having to know about our structure, or the db_salt structure. There
 * is very little additional overhead, in this 2nd layer of indirection
 * since qsort is pretty quick, and does not call compare any more than
 * is needed to partition sort the data.
 */
static int ldr_salt_cmp(const void *x, const void *y) {
	salt_cmp_t *X = (salt_cmp_t *)x;
	salt_cmp_t *Y = (salt_cmp_t *)y;
	int cmp = fmt_salt_compare(X->p->salt, Y->p->salt);
	return cmp;
}

static int ldr_salt_cmp_num(const void *x, const void *y) {
	salt_cmp_t *X = (salt_cmp_t *)x;
	salt_cmp_t *Y = (salt_cmp_t *)y;
	int cmp = salt_compare_num(X->p->count, Y->p->count);
	return cmp;
}

/*
 * If there are more than 1 salt, AND the format exports a salt_compare
 * function, then we reorder the salt array, into the order the format
 * wants them in.  Reasons for this, are usually that the format can
 * gain a lot of speed, if some of the salts are grouped together, so
 * that the group is run one after the other.  This was first done for
 * the WPAPSK format, so that the format could group all ESSID's in
 * the salts, so that the PBKDF2 is computed once (for the first
 * instance of the ESSID), then all of the other salts which are
 * different salts, but  * which have the exact same ESSID will not
 * have to perform the very costly PBKDF2.  The format is designed
 * to work that way, IFF the salts come to it in the right order.
 * This function gets them into that order.
 * A later bug was found in dynamic (hopefully not in other formats
 * also), where some formats, like md5(md5($p).$s) would fail, if
 * there were salt of varying length, within the same input file.
 * the longer salts would leave stale data, which would cause
 * subsquent shorter salt values to produce wrong hash. But if
 * we sort the salts based on salt string length, this issue
 * goes away, and things work properly.  This function now handles
 * the dynamic type also, to correct this performance design choice.
 */
static void ldr_sort_salts(struct db_main *db)
{
	int i;
	struct db_salt *s;
#ifndef DEBUG_SALT_SORT
	salt_cmp_t *ar;
#else
	salt_cmp_t ar[100];  /* array is easier to debug in VC */
#endif
	if (db->salt_count < 2)
		return;

	log_event("Sorting salts, for performance");

	fmt_salt_compare = db->format->methods.salt_compare;
#ifndef DEBUG_SALT_SORT
	ar = (salt_cmp_t *)mem_alloc(sizeof(salt_cmp_t)*db->salt_count);
#endif
	s = db->salts;

	/* load our array of pointers. */
	for (i = 0; i < db->salt_count; ++i) {
		ar[i].p = s;
#ifdef DEBUG_SALT_SORT
		ar[i].org_idx = i;
		strncpy(ar[i].str, (char*)s->salt, 36);
		ar[i].str[35] = 0; /*just in case*/
#endif
		s = s->next;
	}

	if (fmt_salt_compare)
		qsort(ar, db->salt_count, sizeof(ar[0]), ldr_salt_cmp);
	else /* Most used salt first */
		qsort(ar, db->salt_count, sizeof(ar[0]), ldr_salt_cmp_num);

	/* Reset salt hash table, if we still have one */
	if (db->salt_hash) {
		memset(db->salt_hash, 0,
		       SALT_HASH_SIZE * sizeof(struct db_salt *));
	}

	/* finally, we re-build the linked list of salts */
	db->salts = ar[0].p;
	s = db->salts;
	for (i = 1; i < db->salt_count; ++i) {
		/* Rebuild salt hash table, if we still had one */
		if (db->salt_hash) {
			int hash;

			hash = db->format->methods.salt_hash(s->salt);
			if (!db->salt_hash[hash])
				db->salt_hash[hash] = s;
		}
		s->next = ar[i].p;
		s = s->next;
	}
	s->next = 0;

#ifndef DEBUG_SALT_SORT
	MEM_FREE(ar);
#else
	/* setting s here, allows me to debug quick-watch s=s->next
	 * over and over again while watching the char* value of s->salt
	 */
	s = db->salts;
#endif
}

/*
 * Emit the output for --show=left.
 */
static void ldr_show_left(struct db_main *db, struct db_password *pw)
{
	char uid_sep[2] = { 0 };
	char *uid_out = "";
	if (options.show_uid_on_crack && pw->uid && *pw->uid) {
		uid_sep[0] = db->options->field_sep_char;
		uid_out = pw->uid;
	}
	if (pers_opts.target_enc != UTF_8 && pers_opts.report_utf8)
	{
		char utf8login[PLAINTEXT_BUFFER_SIZE + 1];

		cp_to_utf8_r(pw->login, utf8login,
		             PLAINTEXT_BUFFER_SIZE);
		printf("%s%c%s%s%s\n", utf8login, db->options->field_sep_char,
		       db->format->methods.source(pw->source, pw->binary),
			   uid_sep, uid_out);
	} else
		printf("%s%c%s%s%s\n", pw->login, db->options->field_sep_char,
		       db->format->methods.source(pw->source, pw->binary),
			   uid_sep, uid_out);
}

/*
 * Remove the previously-cracked hashes marked with "binary = NULL" by
 * ldr_load_pot_line().
 */
static void ldr_remove_marked(struct db_main *db)
{
	struct db_salt *current_salt, *last_salt;
	struct db_password *current_pw, *last_pw;

	last_salt = NULL;
	if ((current_salt = db->salts))
	do {
		last_pw = NULL;
		if ((current_pw = current_salt->list))
		do {
			if (!current_pw->binary) {
				db->password_count--;
				current_salt->count--;

				if (last_pw)
					last_pw->next = current_pw->next;
				else
					current_salt->list = current_pw->next;
			} else {
				last_pw = current_pw;
				if (options.loader.showuncracked)
					ldr_show_left(db, current_pw);
			}
		} while ((current_pw = current_pw->next));

		if (!current_salt->list) {
			db->salt_count--;
			dyna_salt_remove(current_salt->salt);
			if (last_salt)
				last_salt->next = current_salt->next;
			else
				db->salts = current_salt->next;
		} else
			last_salt = current_salt;
	} while ((current_salt = current_salt->next));
}

/*
 * Remove salts with too few or too many password hashes.
 */
static void ldr_filter_salts(struct db_main *db)
{
	struct db_salt *current, *last;
	int min = db->options->min_pps;
	int max = db->options->max_pps;

	if (!max) {
		if (!min) return;
		max = ~(unsigned int)0 >> 1;
	}

	last = NULL;
	if ((current = db->salts))
	do {
		if (current->count < min || current->count > max) {
			dyna_salt_remove(current->salt);
			if (last)
				last->next = current->next;
			else
				db->salts = current->next;

			db->salt_count--;
			db->password_count -= current->count;
		} else
			last = current;
	} while ((current = current->next));
}

#if FMT_MAIN_VERSION > 11
/*
 * check if cost values for a particular salt match
 * what has been requested with the --costs= option
 */
static int ldr_cost_ok(struct db_salt *salt, unsigned int *min_cost, unsigned int *max_cost)
{
	int i;

	for (i = 0; i < FMT_TUNABLE_COSTS; i++) {
		if (salt->cost[i] < min_cost[i] || salt->cost[i] > max_cost[i])
			return 0;
	}
	return 1;
}


/*
 * Remove salts with too low or too high value for a particular tunable cost
 */
static void ldr_filter_costs(struct db_main *db)
{
	struct db_salt *current, *last;

	last = NULL;
	if ((current = db->salts))
	do {
		if (!ldr_cost_ok(current, db->options->min_cost,
		                          db->options->max_cost)) {
			dyna_salt_remove(current->salt);
			if (last)
				last->next = current->next;
			else
				db->salts = current->next;
			db->salt_count--;
			db->password_count -= current->count;
		} else
			last = current;
	} while ((current = current->next));
}
#endif

/*
 * Allocate memory for and initialize the hash table for this salt if needed.
 * Also initialize salt->count (the number of password hashes for this salt).
 */
static void ldr_init_hash_for_salt(struct db_main *db, struct db_salt *salt)
{
	struct db_password *current;
	int (*hash_func)(void *binary);
	int bitmap_size, hash_size;
	int hash;

	if (salt->hash_size < 0) {
		salt->count = 0;
		if ((current = salt->list))
		do {
			current->next_hash = NULL; /* unused */
			salt->count++;
		} while ((current = current->next));

		return;
	}

	bitmap_size = password_hash_sizes[salt->hash_size];
	{
		size_t size = (bitmap_size +
		    sizeof(*salt->bitmap) * 8 - 1) /
		    (sizeof(*salt->bitmap) * 8) * sizeof(*salt->bitmap);
		salt->bitmap = mem_alloc_tiny(size, sizeof(*salt->bitmap));
		memset(salt->bitmap, 0, size);
	}

	hash_size = bitmap_size >> PASSWORD_HASH_SHR;
	if (hash_size > 1) {
		size_t size = hash_size * sizeof(struct db_password *);
		salt->hash = mem_alloc_tiny(size, MEM_ALIGN_WORD);
		memset(salt->hash, 0, size);
	}

	salt->index = db->format->methods.get_hash[salt->hash_size];

	hash_func = db->format->methods.binary_hash[salt->hash_size];

	salt->count = 0;
	if ((current = salt->list))
	do {
		hash = hash_func(current->binary);
		salt->bitmap[hash / (sizeof(*salt->bitmap) * 8)] |=
		    1U << (hash % (sizeof(*salt->bitmap) * 8));
		if (hash_size > 1) {
			hash >>= PASSWORD_HASH_SHR;
			current->next_hash = salt->hash[hash];
			salt->hash[hash] = current;
		} else
			current->next_hash = current->next;
		salt->count++;
	} while ((current = current->next));
}

/*
 * Decide on whether to use a hash table and on its size for each salt, call
 * ldr_init_hash_for_salt() to allocate and initialize the hash tables.
 */
static void ldr_init_hash(struct db_main *db)
{
	struct db_salt *current;
	int threshold, size;

	threshold = password_hash_thresholds[0];
	if (db->format && (db->format->params.flags & FMT_BS)) {
/*
 * Estimate the complexity of DES_bs_get_hash() for each computed hash (but
 * comparing it against less than 1 loaded hash on average due to the use of a
 * hash table) vs. the complexity of DES_bs_cmp_all() for all computed hashes
 * at once (but calling it for each loaded hash individually).
 */
		threshold = 5 * ARCH_BITS / ARCH_BITS_LOG + 1;
	}

	if ((current = db->salts))
	do {
		size = -1;
		if (current->count >= threshold && mem_saving_level < 3)
			for (size = PASSWORD_HASH_SIZES - 1; size >= 0; size--)
				if (current->count >=
				    password_hash_thresholds[size] &&
				    db->format->methods.binary_hash[size] &&
				    db->format->methods.binary_hash[size] !=
				    fmt_default_binary_hash)
					break;

		if (mem_saving_level >= 2)
			size--;

		current->hash_size = size;
		ldr_init_hash_for_salt(db, current);
#ifdef DEBUG_HASH
		if (current->hash_size > 0)
			printf("salt %08x, binary hash size 0x%x (%d), "
			       "num ciphertexts %d\n",
			       *(unsigned int*)current->salt,
			       password_hash_sizes[current->hash_size],
			       current->hash_size, current->count);
		else
			printf("salt %08x, no binary hash, "
			       "num ciphertexts %d\n",
			       *(unsigned int*)current->salt, current->count);
#endif
	} while ((current = current->next));
}

#if FMT_MAIN_VERSION > 11
/*
 * compute cost ranges after all unneeded salts have been removed
 */
static void ldr_cost_ranges(struct db_main *db)
{
	int i;
	struct db_salt *current;

	for (i = 0; i < FMT_TUNABLE_COSTS; ++i) {
		db->min_cost[i] = UINT_MAX;
		db->max_cost[i] = 0;
	}

	if ((current = db->salts))
	do {
		for (i = 0; i < FMT_TUNABLE_COSTS && db->format->methods.tunable_cost_value[i] != NULL; ++i) {
			if (current->cost[i] < db->min_cost[i])
				db->min_cost[i] = current->cost[i];
			if (current->cost[i] > db->max_cost[i])
				db->max_cost[i] = current->cost[i];
		}
	} while ((current = current->next));
}
#endif

void ldr_fix_database(struct db_main *db)
{
	int total = db->password_count;

	ldr_init_salts(db);
	MEM_FREE(db->password_hash);
	if (!db->format ||
	    db->format->methods.salt_hash == fmt_default_salt_hash ||
	    mem_saving_level >= 2) /* Otherwise kept for faster pot sync */
		MEM_FREE(db->salt_hash);

	ldr_filter_salts(db);
#if FMT_MAIN_VERSION > 11
	ldr_filter_costs(db);
#endif
	ldr_remove_marked(db);
#if FMT_MAIN_VERSION > 11
	ldr_cost_ranges(db);
#endif
	ldr_sort_salts(db);

	ldr_init_hash(db);

	db->loaded = 1;

	if (options.loader.showuncracked) {
		total -= db->password_count;
		if (john_main_process)
			fprintf(stderr, "%s%d password hash%s cracked,"
			        " %d left\n", total ? "\n" : "", total,
			        total != 1 ? "es" : "", db->password_count);
		exit(0);
	}
}

static int ldr_cracked_hash(char *ciphertext)
{
	unsigned int hash = 0;
	char *p = ciphertext;

	while (*p) {
		hash <<= 1;
		hash += (unsigned char)*p++ | 0x20; /* ASCII case insensitive */
		if (hash >> (2 * CRACKED_HASH_LOG - 1)) {
			hash ^= hash >> CRACKED_HASH_LOG;
			hash &= CRACKED_HASH_SIZE - 1;
		}
	}

	hash ^= hash >> CRACKED_HASH_LOG;
	hash &= CRACKED_HASH_SIZE - 1;

	return hash;
}

static void ldr_show_pot_line(struct db_main *db, char *line)
{
	char *ciphertext, *pos;
	int hash;
	struct db_cracked *current, *last;

	ciphertext = ldr_get_field(&line, db->options->field_sep_char);

	if (options.format &&
	    !strcasecmp(options.format, "raw-sha1-linkedin") &&
	    !strncmp(ciphertext, "$dynamic_26$", 12) &&
	    strncmp(ciphertext, "$dynamic_26$00000", 17)) {
		char *new = mem_alloc_tiny(12 + 41, MEM_ALIGN_NONE);
		strnzcpy(new, ciphertext, 12 + 41);
		memset(new + 12, '0', 5);
		ciphertext = new;
	} else
	if (!strncmp(ciphertext, "$dynamic_", 9) && strstr(ciphertext, "$HEX$"))
	{
		char Tmp[512], *cp=Tmp;
		int alloced=0;
		if (strlen(ciphertext)>sizeof(Tmp)) {
			cp = (char*)mem_alloc(strlen(ciphertext)+1);
			alloced = 1;
		}
		RemoveHEX(Tmp, ciphertext);
#if 0
		// I am pretty sure that this removed hex should be used all the time, even if
		// there are ':' or \n chars. I believe this logic was from an older version of
		// dynamic, and simply was not changed here. I am leaving the code (commented out)
		// for now, just in case there are issue, and it can be reverted back. This bug was
		// found digging into https://github.com/magnumripper/JohnTheRipper/issues/930

		// We only remove hex if the end result is 'safe'. IF there are any line feeds, or
		// ':' chars, then it is not safe to remove.  NULL is also dangrous, BUT the
		// RemoveHEX itself bails if there are nulls, putting original ciphertext into Tmp.
		if (strchr(Tmp, ':') || strchr(Tmp, '\n')
#if (AC_BUILT && HAVE_WINDOWS_H) || (!AC_BUILT && (_MSC_VER || __CYGWIN__ || __MINGW__))
			|| strchr(Tmp, '\r') || strchr(Tmp, 0x1A)
#endif
		)
			; // do nothing.
		else
#endif
			// tmp will always be 'shorter' or equal length to ciphertext
			strcpy(ciphertext, Tmp);
		if (alloced)
			MEM_FREE(cp);
	}

	if (line) {
/* If just one format was forced on the command line, insist on it */
		if (!fmt_list->next &&
		    !fmt_list->methods.valid(ciphertext, fmt_list))
			return;

		pos = line;
		do {
			if (*pos == '\r' || *pos == '\n') *pos = 0;
		} while (*pos++);

		if (db->options->flags & DB_PLAINTEXTS) {
			list_add(db->plaintexts, line);
			return;
		}

		hash = ldr_cracked_hash(ciphertext);

		last = db->cracked_hash[hash];
		current = db->cracked_hash[hash] =
			mem_alloc_tiny(sizeof(struct db_cracked),
			MEM_ALIGN_WORD);
		current->next = last;

		current->ciphertext = str_alloc_copy(ciphertext);
		current->plaintext = str_alloc_copy(line);
	}
}

void ldr_show_pot_file(struct db_main *db, char *name)
{
#ifdef HAVE_CRYPT
	ldr_in_pot = 1;
#endif
	read_file(db, name, RF_ALLOW_MISSING, ldr_show_pot_line);
#ifdef HAVE_CRYPT
	ldr_in_pot = 0;
#endif
}

static void ldr_show_pw_line(struct db_main *db, char *line)
{
	int show, loop;
	char source[LINE_BUFFER_SIZE];
	struct fmt_main *format;
	char *(*split)(char *ciphertext, int index, struct fmt_main *self);
	int index, count, unify;
	char *login, *ciphertext, *gecos, *home, *uid;
	char *piece;
	int pass, found, chars;
	int hash;
	struct db_cracked *current;
	char utf8login[LINE_BUFFER_SIZE + 1];
	char utf8source[LINE_BUFFER_SIZE + 1];
	char joined[PLAINTEXT_BUFFER_SIZE + 1] = "";

	format = NULL;
	count = ldr_split_line(&login, &ciphertext, &gecos, &home, &uid,
		source, &format, db->options, line);
	if (!count) return;

/* If just one format was forced on the command line, insist on it */
	if (!fmt_list->next && !format) return;

	show = !(db->options->flags & DB_PLAINTEXTS);

	if ((loop = (options.flags & FLG_LOOPBACK_CHK) ? 1 : 0))
		show = 0;

	if (format) {
		split = format->methods.split;
		unify = format->params.flags & FMT_SPLIT_UNIFIES_CASE;
		if (format->params.flags & FMT_UNICODE)
			pers_opts.store_utf8 = cfg_get_bool(SECTION_OPTIONS,
			    NULL, "UnicodeStoreUTF8", 0);
		else
			pers_opts.store_utf8 = cfg_get_bool(SECTION_OPTIONS,
			    NULL, "CPstoreUTF8", 0);
	} else {
		split = fmt_default_split;
		count = 1;
		unify = 0;
	}

	if (pers_opts.target_enc != UTF_8 &&
	    !pers_opts.store_utf8 && pers_opts.report_utf8) {
		login = cp_to_utf8_r(login, utf8login, LINE_BUFFER_SIZE);
		cp_to_utf8_r(source, utf8source, LINE_BUFFER_SIZE);
		strnzcpy(source, utf8source, sizeof(source));
	}

	if (!*ciphertext) {
		found = 1;
		if (show) printf("%s%cNO PASSWORD",
		                 login, db->options->field_sep_char);

		db->guess_count++;
	} else
	for (found = pass = 0; pass == 0 || (pass == 1 && found); pass++)
	for (index = 0; index < count; index++) {
		piece = split(ciphertext, index, format);
		if (unify)
			piece = strcpy(mem_alloc(strlen(piece) + 1), piece);

		hash = ldr_cracked_hash(piece);

		if ((current = db->cracked_hash[hash]))
		do {
			char *pot = current->ciphertext;
			if (!strcmp(pot, piece))
				break;
/* This extra check, along with ldr_cracked_hash() being case-insensitive,
 * is only needed for matching some pot file records produced by older
 * versions of John and contributed patches where split() didn't unify the
 * case of hex-encoded hashes. */
			if (unify &&
			    format->methods.valid(pot, format) == 1 &&
			    !strcmp(split(pot, 0, format), piece))
				break;
		} while ((current = current->next));

		if (unify)
			MEM_FREE(piece);

		if (pass) {
			chars = 0;
			if (show || loop) {
				if (format)
					chars = format->params.plaintext_length;
				if (index < count - 1 && current &&
				    (pers_opts.store_utf8 ?
				     (int)strlen8((UTF8*)current->plaintext) :
				     (int)strlen(current->plaintext)) != chars)
					current = NULL;
			}

			if (current) {
				if (show) {
					printf("%s", current->plaintext);
				} else if (loop) {
					strcat(joined, current->plaintext);
				} else
					list_add(db->plaintexts,
						current->plaintext);

				db->guess_count++;
			} else
			if (!loop)
			while (chars--)
				putchar('?');
		} else
		if (current) {
			found = 1;
			if (show) printf("%s%c", login,
			                 db->options->field_sep_char);
			break;
		}
	}

	if (found && show) {
		if (source[0])
			printf("%c%s", db->options->field_sep_char, source);
		else
			putchar('\n');
	}
	else if (*joined && found && loop) {
		char *plain = enc_strlwr(ldr_conv(joined));

		/* list_add_unique is O(n^2) */
		if (db->plaintexts->count < 0x10000)
			list_add_unique(db->plaintexts, plain);
		else if (strcmp(db->plaintexts->tail->data, plain))
			list_add(db->plaintexts, plain);
	}
	if (format || found) db->password_count += count;
}

void ldr_show_pw_file(struct db_main *db, char *name)
{
	read_file(db, name, RF_ALLOW_DIR, ldr_show_pw_line);
}

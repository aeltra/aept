/* test_authfile.c - source credentials from an auth.conf file
 *
 * The parser reads a file that holds passwords, written by hand; the
 * matcher decides which server gets one.  Both are tested on their own
 * through the file-scope helpers, and together through
 * aept_auth_apply() against a configured set of sources.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "authfile.c"

#include "test.h"

static struct aept_ctx ctx;
static char dir[] = "/tmp/aept-auth-XXXXXX";

/* Malformed entries are expected and each logs a warning. */
static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

static void write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");

    if (!fp) {
        perror(path);
        exit(2);
    }
    fputs(content, fp);
    fclose(fp);
    chmod(path, 0600);
}

/* mkdir -p, for the fixture trees. */
static void make_dirs(const char *path)
{
    char *copy = aept_strdup(path);

    for (char *p = copy + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(copy, 0755);
            *p = '/';
        }
    }
    mkdir(copy, 0755);
    free(copy);
}

/* Parse content as an auth file. */
static void parse(auth_list_t *list, const char *content)
{
    char *path;
    FILE *fp;

    aept_asprintf(&path, "%s/parse.conf", dir);
    write_file(path, content);
    fp = fopen(path, "r");
    memset(list, 0, sizeof(*list));
    auth_parse(list, fp, path);
    fclose(fp);
    free(path);
}

/* The login the list would hand to url, or NULL. */
static const char *login_for(const auth_list_t *list, const char *url)
{
    const auth_entry_t *e = auth_find(list, url);
    return e ? e->login : NULL;
}

static void add(struct aept_config *cfg, const char *url)
{
    char *clean, *user, *password;
    aept_source_t *src;

    aept_url_split(url, &clean, &user, &password);
    cfg->nsources++;
    cfg->sources = aept_realloc(cfg->sources, cfg->nsources * sizeof(aept_source_t));
    src = &cfg->sources[cfg->nsources - 1];
    src->name = aept_strdup("src");
    src->url = clean;
    src->user = user;
    src->password = password;
    src->file_credentials = 0;
    src->gzip = 1;
}

static void test_parse(void)
{
    auth_list_t list;

    parse(&list, "machine example.org login alice password s3cret\n");
    test_int_eq(list.count, 1, "one entry on one line");
    test_str_eq(list.count ? list.entries[0].host : NULL, "example.org", "its host");
    test_str_eq(list.count ? list.entries[0].path : NULL, "", "no path prefix");
    test_str_eq(list.count ? list.entries[0].login : NULL, "alice", "its login");
    test_str_eq(list.count ? list.entries[0].password : NULL, "s3cret", "its password");
    auth_list_free(&list);

    parse(&list, "# a comment\n"
                 "\n"
                 "machine\n  Example.ORG/dists/\n"
                 "  login alice\n"
                 "  password s3cret   # trailing comment\n"
                 "machine other.org login bob password pw\n");
    test_int_eq(list.count, 2, "entries spread over lines, with comments");
    test_str_eq(list.count ? list.entries[0].host : NULL, "example.org", "host is lower-cased");
    test_str_eq(list.count ? list.entries[0].path : NULL, "/dists",
                "the path loses its trailing slash");
    test_str_eq(list.count > 1 ? list.entries[1].login : NULL, "bob", "the second entry");
    auth_list_free(&list);

    parse(&list, "machine example.org login alice password #not-a-comment\n");
    test_str_eq(list.count ? list.entries[0].password : NULL, "#not-a-comment",
                "a value may start with '#'");
    auth_list_free(&list);

    parse(&list, "machine https://example.org:443/x login a password p\n");
    test_str_eq(list.count ? list.entries[0].host : NULL, "example.org",
                "the https scheme and default port are dropped");
    test_str_eq(list.count ? list.entries[0].path : NULL, "/x", "the path after them is kept");
    auth_list_free(&list);

    parse(&list, "machine example.org:8443 login a password p\n");
    test_str_eq(list.count ? list.entries[0].host : NULL, "example.org:8443",
                "another port is part of the host");
    auth_list_free(&list);

    parse(&list, "machine http://example.org login a password p\n"
                 "machine ftp://example.org login a password p\n");
    test_int_eq(list.count, 0, "an entry naming another scheme is ignored");
    auth_list_free(&list);

    parse(&list, "machine example.org login alice\n"
                 "machine other.org password pw\n");
    test_int_eq(list.count, 0, "an entry without both login and password is ignored");
    auth_list_free(&list);

    parse(&list, "login alice password pw\n"
                 "account x machine example.org login a password p\n");
    test_int_eq(list.count, 1, "tokens outside an entry and unknown keywords are skipped");
    auth_list_free(&list);

    parse(&list, "machine example.org login alice password\n");
    test_int_eq(list.count, 0, "a keyword without a value at the end is not an entry");
    auth_list_free(&list);

    {
        char *content, *pw = aept_malloc(LIBFETCH_URL_PWDLEN + 2);

        memset(pw, 'x', LIBFETCH_URL_PWDLEN + 1);
        pw[LIBFETCH_URL_PWDLEN + 1] = '\0';
        aept_asprintf(&content, "machine example.org login a\npassword %s\n", pw);
        parse(&list, content);
        test_int_eq(list.count, 0, "a password libfetch would truncate is refused");
        auth_list_free(&list);
        free(content);
        free(pw);
    }
}

static void test_match(void)
{
    auth_list_t list;

    parse(&list, "machine example.org/dists login prefix password p\n"
                 "machine example.org login host password p\n"
                 "machine other.org:8443 login port password p\n");

    test_str_eq(login_for(&list, "https://example.org/dists"), "prefix", "the prefix itself");
    test_str_eq(login_for(&list, "https://example.org/dists/main"), "prefix", "below the prefix");
    test_str_eq(login_for(&list, "https://example.org/distsx"), "host",
                "a prefix matches at a segment boundary only");
    test_str_eq(login_for(&list, "https://EXAMPLE.org:443/other"), "host",
                "host case and the default port do not matter");
    test_str_eq(login_for(&list, "http://example.org/dists"), NULL, "plain http never matches");
    test_str_eq(login_for(&list, "https://example.org.evil/dists"), NULL,
                "a host is compared whole");
    test_str_eq(login_for(&list, "https://other.org/"), NULL, "a port must match");
    test_str_eq(login_for(&list, "https://other.org:8443/x"), "port", "the port named");
    auth_list_free(&list);

    parse(&list, "machine example.org login first password p\n"
                 "machine example.org/dists login second password p\n");
    test_str_eq(login_for(&list, "https://example.org/dists"), "first", "the first match wins");
    auth_list_free(&list);
}

static void test_apply(void)
{
    struct aept_config cfg;
    char *file, *other;

    aept_asprintf(&file, "%s/auth.conf", dir);
    aept_asprintf(&other, "%s/other.conf", dir);
    write_file(file, "machine example.org login alice password s3cret\n");
    write_file(other, "machine example.org login carol password pw\n");

    memset(&cfg, 0, sizeof(cfg));
    add(&cfg, "https://example.org/a");
    add(&cfg, "http://example.org/b");
    add(&cfg, "https://bob:url@example.org/c");
    add(&cfg, "https://unknown.org/d");

    test_int_eq(aept_auth_apply(&cfg, file), 0, "an explicit file applies");
    test_str_eq(cfg.sources[0].user, "alice", "the https source takes the login");
    test_str_eq(cfg.sources[0].password, "s3cret", "and the password");
    test_int_eq(cfg.sources[0].file_credentials, 1, "marked as from the file");
    test_str_eq(cfg.sources[1].user, NULL, "the http source takes nothing");
    test_str_eq(cfg.sources[2].user, "bob", "credentials in the url are kept");
    test_int_eq(cfg.sources[2].file_credentials, 0, "and not marked");
    test_str_eq(cfg.sources[3].user, NULL, "a source without an entry takes nothing");
    test_str_eq(cfg.auth_file_used, file, "the file used is recorded");

    test_int_eq(aept_auth_apply(&cfg, other), 0, "applying another file");
    test_str_eq(cfg.sources[0].user, "carol", "replaces what the first file gave");
    test_str_eq(cfg.sources[2].user, "bob", "and still leaves the url's alone");

    aept_asprintf(&cfg.offline_root, "%s/empty-root", dir);
    make_dirs(cfg.offline_root);
    test_int_eq(aept_auth_apply(&cfg, NULL), 0, "no file to find is not an error");
    test_str_eq(cfg.sources[0].user, NULL, "and leaves no file credentials behind");
    test_str_eq(cfg.auth_file_used, NULL, "nor a recorded file");

    {
        char *missing;

        aept_asprintf(&missing, "%s/missing.conf", dir);
        test_int_eq(aept_auth_apply(&cfg, missing), -1, "an explicit file that is missing fails");
        free(missing);
    }

    aept_config_free(&cfg);
    free(file);
    free(other);
}

static void test_lookup(void)
{
    struct aept_config cfg;
    struct passwd *pw = getpwuid(geteuid());
    char *root, *system_file, *home_dir, *home_file, *explicit_file;

    memset(&cfg, 0, sizeof(cfg));
    add(&cfg, "https://example.org/a");

    aept_asprintf(&root, "%s/root", dir);
    aept_asprintf(&system_file, "%s/etc/aept/auth.conf", root);
    make_dirs(root);
    cfg.offline_root = aept_strdup(root);

    make_dirs(dir);
    {
        char *etc;

        aept_asprintf(&etc, "%s/etc/aept", root);
        make_dirs(etc);
        free(etc);
    }
    write_file(system_file, "machine example.org login system password p\n");

    test_int_eq(aept_auth_apply(&cfg, NULL), 0, "the system file is looked up");
    test_str_eq(cfg.sources[0].user, "system", "under the offline root");

    if (geteuid() == 0 || !pw || !pw->pw_dir || pw->pw_dir[0] != '/') {
        printf("# skipping the build-box target cases: they need a non-root user with a home\n");
        aept_config_free(&cfg);
        free(root);
        free(system_file);
        return;
    }

    aept_asprintf(&home_dir, "%s%s/RealHome/.aeltra", root, pw->pw_dir);
    aept_asprintf(&home_file, "%s/auth.conf", home_dir);
    make_dirs(home_dir);
    write_file(home_file, "machine example.org login home password p\n");

    test_int_eq(aept_auth_apply(&cfg, NULL), 0, "outside a target");
    test_str_eq(cfg.sources[0].user, "system", "the RealHome file is not read");

    {
        char *marker;

        aept_asprintf(&marker, "%s/etc/target", root);
        write_file(marker, "");
        free(marker);
    }

    test_int_eq(aept_auth_apply(&cfg, NULL), 0, "inside a target");
    test_str_eq(cfg.sources[0].user, "home", "the RealHome file wins over the system file");

    aept_asprintf(&explicit_file, "%s/explicit.conf", dir);
    write_file(explicit_file, "machine example.org login explicit password p\n");
    test_int_eq(aept_auth_apply(&cfg, explicit_file), 0, "an explicit file inside a target");
    test_str_eq(cfg.sources[0].user, "explicit", "wins over both");

    unlink(home_file);
    test_int_eq(aept_auth_apply(&cfg, NULL), 0, "a target without a RealHome file");
    test_str_eq(cfg.sources[0].user, "system", "falls back to the system file");

    aept_config_free(&cfg);
    free(root);
    free(system_file);
    free(home_dir);
    free(home_file);
    free(explicit_file);
}

int main(void)
{
    if (!mkdtemp(dir)) {
        perror(dir);
        return 2;
    }

    silence_logging();

    test_parse();
    test_match();
    test_apply();
    test_lookup();

    {
        char *cmd;

        aept_asprintf(&cmd, "rm -rf '%s'", dir);
        if (system(cmd) != 0)
            printf("# could not remove %s\n", dir);
        free(cmd);
    }

    return test_summary();
}

/*
 * Unit tests for shell quote-aware tokenization (wifi join SSIDs with spaces).
 */

#include "shell_tokenize.h"

#include <stdio.h>
#include <string.h>

static int tests_run;
static int tests_failed;

static void expect_str(const char *got, const char *want, const char *label)
{
	tests_run++;
	if (!want)
		want = "";
	if (!got)
		got = "";
	if (strcmp(got, want) != 0) {
		tests_failed++;
		fprintf(stderr, "FAIL: %s got '%s' want '%s'\n", label, got, want);
	}
}

static void test_double_quoted_ssid(void)
{
	char line[] = "wifi join \"Bailey's iPhone\"";
	char *argv[8];
	int argc;

	argc = fl_shell_tokenize_line(line, argv, 8);
	if (argc != 3) {
		tests_failed++;
		fprintf(stderr, "FAIL: argc got %d want 3\n", argc);
		return;
	}
	tests_run++;
	expect_str(argv[0], "wifi", "argv0");
	expect_str(argv[1], "join", "argv1");
	expect_str(argv[2], "Bailey's iPhone", "argv2");
}

static void test_single_quoted_ssid(void)
{
	char line[] = "wifi join 'Guest Network'";
	char *argv[8];
	int argc;

	argc = fl_shell_tokenize_line(line, argv, 8);
	if (argc != 3) {
		tests_failed++;
		fprintf(stderr, "FAIL: single-quote argc got %d want 3\n", argc);
		return;
	}
	tests_run++;
	expect_str(argv[2], "Guest Network", "single-quote ssid");
}

static void test_strip_outer_quotes(void)
{
	char s1[] = "\"Bailey's iPhone\"";
	char s2[] = "'open net'";

	fl_shell_strip_outer_quotes(s1);
	fl_shell_strip_outer_quotes(s2);
	expect_str(s1, "Bailey's iPhone", "strip double");
	expect_str(s2, "open net", "strip single");
}

int main(void)
{
	test_double_quoted_ssid();
	test_single_quoted_ssid();
	test_strip_outer_quotes();
	if (tests_failed) {
		fprintf(stderr, "test_shell_tokenize: %d/%d failed\n", tests_failed, tests_run);
		return 1;
	}
	printf("test_shell_tokenize: all %d passed\n", tests_run);
	return 0;
}

/**
 * test.c - libmdp public API tests.
 * Summary: Contract tests for the stateless mdp API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmdp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints one canonical test-case result line.
 * @param fail Nonzero when the case failed.
 * @param name Canonical test-case name.
 * @param description Human-readable test-case description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *description) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, description);
}

typedef int (*case_fn)(void);

/**
 * Executes one test case and accumulates its result.
 * @param rc Aggregate failed-case count.
 * @param fn Test-case function to execute.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one boolean test expectation.
 * @param name Expectation description.
 * @param condition Nonzero when the expectation is satisfied.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies one integer test expectation.
 * @param name Expectation description.
 * @param expected Expected integer value.
 * @param actual Actual integer value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one string test expectation.
 * @param name Expectation description.
 * @param expected Expected string value.
 * @param actual Actual string value.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected,
    const char *actual) {
    if (!actual || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual ? actual : "NULL");
        return 1;
    }
    return 0;
}

/**
 * Tests document creation and one-time split ownership.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_open(void) {
    char input[] = "---\ntitle: Home\n---\n# Hello";
    kc_mdp_t *mdp = NULL;
    int fail = 0;

    fail += expect_int("open rejects NULL out", KC_MDP_ERROR,
        kc_mdp_open(NULL, input));
    fail += expect_int("open rejects NULL input", KC_MDP_ERROR,
        kc_mdp_open(&mdp, NULL));
    fail += expect_true("failed open clears output", mdp == NULL);
    fail += expect_int("open accepts document", KC_MDP_OK,
        kc_mdp_open(&mdp, input));
    fail += expect_true("open returns document", mdp != NULL);

    if (mdp != NULL) {
        input[4] = 'X';
        fail += expect_string("open owns split metadata",
            "title: Home", kc_mdp_meta(mdp));
        fail += expect_string("open owns split body",
            "# Hello", kc_mdp_body(mdp));
    }

    kc_mdp_close(mdp);
    case_result(fail, "kc_mdp_open",
        "creates one persistent document with an owned split");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests cached Markdown-to-HTML rendering.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_html(void) {
    const char *first;
    const char *second;
    kc_mdp_t *mdp = NULL;
    int fail = 0;

    fail += expect_true("html rejects NULL document", kc_mdp_html(NULL) == NULL);
    fail += expect_int("open composite document", KC_MDP_OK,
        kc_mdp_open(&mdp,
            "---\ntitle: Home\n---\n# Hello\n\n"
            "Text **bold** *italic* `code`.\n\n"
            "- one\n- two\n\n> quote\n\n"
            "```\n<a>\n```\n\n---\n\n"
            "<div>\n*raw*\n</div>\n"));

    first = mdp ? kc_mdp_html(mdp) : NULL;
    fail += expect_true("html composite returns non-NULL", first != NULL);
    if (first != NULL) {
        fail += expect_true("html renders heading",
            strstr(first, "<h1>Hello</h1>\n") != NULL);
        fail += expect_true("html renders inline markup",
            strstr(first,
                "<strong>bold</strong> <em>italic</em> <code>code</code>") != NULL);
        fail += expect_true("html renders list",
            strstr(first, "<ul>\n<li>one</li>\n<li>two</li>\n</ul>\n") != NULL);
        fail += expect_true("html renders blockquote",
            strstr(first, "<blockquote>\n<p>quote</p>\n</blockquote>\n") != NULL);
        fail += expect_true("html escapes fenced code",
            strstr(first, "<pre><code>&lt;a&gt;\n</code></pre>\n") != NULL);
        fail += expect_true("html renders horizontal rule",
            strstr(first, "<hr>\n") != NULL);
        fail += expect_true("html passes raw block",
            strstr(first, "<div>\n*raw*\n</div>\n") != NULL);
        fail += expect_true("html excludes frontmatter",
            strstr(first, "title: Home") == NULL);
        second = kc_mdp_html(mdp);
        fail += expect_true("html reuses cached result", second == first);
    }
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open empty document", KC_MDP_OK,
        kc_mdp_open(&mdp, ""));
    first = mdp ? kc_mdp_html(mdp) : NULL;
    fail += expect_true("html empty returns non-NULL", first != NULL);
    if (first != NULL) {
        fail += expect_string("html empty returns empty string", "", first);
    }
    kc_mdp_close(mdp);

    case_result(fail, "kc_mdp_html",
        "renders once and caches the supported Markdown contract");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests ANSI rendering from the same Markdown parser semantics.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_ansi(void) {
    const char *first;
    const char *second;
    kc_mdp_t *mdp = NULL;
    int fail = 0;

    fail += expect_true("ansi rejects NULL document", kc_mdp_ansi(NULL) == NULL);
    fail += expect_int("open ANSI document", KC_MDP_OK,
        kc_mdp_open(&mdp,
            "## Subtitle\n\n"
            "Text **bold** *italic* `code` and ~~gone~~.\n\n"
            "1. first\n2. second\n\n"
            "- [ ] todo\n- [x] done\n\n"
            "| Name | Value |\n| --- | --- |\n"
            "| one | two |\n| longer-name | 7 |\n\n"
            "> quote\n\n"
            "```c\nint x = 1;\n```\n"));

    first = mdp ? kc_mdp_ansi(mdp) : NULL;
    fail += expect_true("ansi returns non-NULL", first != NULL);
    if (first != NULL) {
        fail += expect_true("ansi removes heading markers",
            strstr(first, "## Subtitle") == NULL);
        fail += expect_true("ansi styles heading",
            strstr(first, "\x1b[1mSubtitle\x1b[22m\n\n") != NULL);
        fail += expect_true("ansi styles inline Markdown",
            strstr(first,
                "Text \x1b[1mbold\x1b[22m \x1b[3mitalic\x1b[23m "
                "\x1b[2mcode\x1b[22m and "
                "\x1b[9mgone\x1b[29m.\n\n") != NULL);
        fail += expect_true("ansi renders ordered list",
            strstr(first, "  1. first\n  2. second\n") != NULL);
        fail += expect_true("ansi renders task list",
            strstr(first, "  ☐ todo\n  ☑ done\n") != NULL);
        fail += expect_true("ansi aligns table columns",
            strstr(first,
                "\x1b[1mName        │ Value\x1b[22m\n"
                "one         │ two  \n"
                "longer-name │ 7    \n\n") != NULL);
        fail += expect_true("ansi renders quote without markdown marker",
            strstr(first,
                "\x1b[2mquote\n\n\x1b[22m\n") != NULL);
        fail += expect_true("ansi renders fenced code without fences",
            strstr(first,
                "\x1b[2m  int x = 1;\n\x1b[22m\n\n") != NULL);
        fail += expect_true("ansi contains no fenced code marker",
            strstr(first, "```") == NULL);
        second = kc_mdp_ansi(mdp);
        fail += expect_true("ansi reuses cached result", second == first);
    }
    kc_mdp_close(mdp);

    case_result(fail, "kc_mdp_ansi",
        "renders terminal output through the shared Markdown parser");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests prioritized practical Markdown extensions.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_extensions(void) {
    const char *html;
    kc_mdp_t *mdp = NULL;
    int fail = 0;

    fail += expect_int("open extended Markdown", KC_MDP_OK,
        kc_mdp_open(&mdp,
            "1. one\n2. two\n\n"
            "```c\nint main(void) {}\n```\n\n"
            "| Name | Value |\n| --- | --- |\n| one | two |\n\n"
            "- [ ] todo\n- [x] done\n\n"
            "~~removed~~\n"));

    html = mdp ? kc_mdp_html(mdp) : NULL;
    fail += expect_true("extended Markdown returns HTML", html != NULL);
    if (html != NULL) {
        fail += expect_true("renders ordered lists",
            strstr(html, "<ol>\n<li>one</li>\n<li>two</li>\n</ol>\n") != NULL);
        fail += expect_true("renders fenced code language",
            strstr(html,
                "<pre><code class=\"language-c\">int main(void) {}\n"
                "</code></pre>\n") != NULL);
        fail += expect_true("renders tables",
            strstr(html,
                "<table>\n<thead>\n<tr><th>Name</th><th>Value</th></tr>\n"
                "</thead>\n<tbody>\n<tr><td>one</td><td>two</td></tr>\n"
                "</tbody>\n</table>\n") != NULL);
        fail += expect_true("renders unchecked task",
            strstr(html,
                "<li><input type=\"checkbox\" disabled> todo</li>\n") != NULL);
        fail += expect_true("renders checked task",
            strstr(html,
                "<li><input type=\"checkbox\" disabled checked> "
                "done</li>\n") != NULL);
        fail += expect_true("renders strikethrough",
            strstr(html, "<p><del>removed</del></p>\n") != NULL);
    }

    kc_mdp_close(mdp);
    case_result(fail, "kc_mdp_extensions",
        "renders prioritized practical Markdown extensions");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests body access through the persistent document.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_body(void) {
    kc_mdp_t *mdp = NULL;
    const char *first;
    const char *second;
    int fail = 0;

    fail += expect_true("body rejects NULL document", kc_mdp_body(NULL) == NULL);
    fail += expect_int("open LF frontmatter", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\ntitle: Home\n---\n# Hello"));
    first = mdp ? kc_mdp_body(mdp) : NULL;
    second = mdp ? kc_mdp_body(mdp) : NULL;
    fail += expect_string("body LF strips frontmatter", "# Hello", first);
    fail += expect_true("body returns stable view", second == first);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open CRLF frontmatter", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\r\ntitle: Home\r\n---\r\n# Hello"));
    fail += expect_string("body CRLF strips frontmatter",
        "# Hello", mdp ? kc_mdp_body(mdp) : NULL);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open unclosed frontmatter", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\ntitle: Home\n# Hello"));
    fail += expect_string("body unclosed frontmatter preserves input",
        "---\ntitle: Home\n# Hello", mdp ? kc_mdp_body(mdp) : NULL);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open empty body", KC_MDP_OK,
        kc_mdp_open(&mdp, ""));
    fail += expect_string("body empty returns empty string",
        "", mdp ? kc_mdp_body(mdp) : NULL);
    kc_mdp_close(mdp);

    case_result(fail, "kc_mdp_body",
        "returns the persistent body view after one split");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests frontmatter access through the persistent document.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_meta(void) {
    kc_mdp_t *mdp = NULL;
    const char *first;
    const char *second;
    int fail = 0;

    fail += expect_true("meta rejects NULL document", kc_mdp_meta(NULL) == NULL);
    fail += expect_int("open LF metadata", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\ntitle: Home\n---\n# Hello"));
    first = mdp ? kc_mdp_meta(mdp) : NULL;
    second = mdp ? kc_mdp_meta(mdp) : NULL;
    fail += expect_string("meta LF returns raw frontmatter", "title: Home", first);
    fail += expect_true("meta returns stable view", second == first);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open CRLF metadata", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\r\ntitle: Home\r\n---\r\n# Hello"));
    fail += expect_string("meta CRLF returns raw frontmatter",
        "title: Home", mdp ? kc_mdp_meta(mdp) : NULL);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open document without metadata", KC_MDP_OK,
        kc_mdp_open(&mdp, "# Hello"));
    fail += expect_string("meta absent returns empty string",
        "", mdp ? kc_mdp_meta(mdp) : NULL);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open unclosed metadata", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\ntitle: Home\n# Hello"));
    fail += expect_string("meta unclosed returns empty string",
        "", mdp ? kc_mdp_meta(mdp) : NULL);
    kc_mdp_close(mdp);

    case_result(fail, "kc_mdp_meta",
        "returns the persistent recognized frontmatter view");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests document release through the public API.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_close(void) {
    kc_mdp_t *mdp = NULL;
    int fail = 0;

    fail += expect_int("open document for close", KC_MDP_OK,
        kc_mdp_open(&mdp, "# Hello"));
    fail += expect_true("document allocated for close", mdp != NULL);
    if (mdp != NULL) {
        fail += expect_true("html can be cached before close",
            kc_mdp_html(mdp) != NULL);
        fail += expect_true("ansi can be cached before close",
            kc_mdp_ansi(mdp) != NULL);
    }
    kc_mdp_close(mdp);
    kc_mdp_close(NULL);

    case_result(fail, "kc_mdp_close",
        "releases document state and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the public build-version query.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_version(void) {
    int fail = 0;

    fail += expect_true("version returns non-zero", kc_mdp_version() != 0U);
    case_result(fail, "kc_mdp_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Runs the complete portable mdp test suite.
 * @return Number of failed test cases.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 8;
    test_case_current = 0;
    run_case(&rc, case_kc_mdp_open);
    run_case(&rc, case_kc_mdp_html);
    run_case(&rc, case_kc_mdp_ansi);
    run_case(&rc, case_kc_mdp_extensions);
    run_case(&rc, case_kc_mdp_body);
    run_case(&rc, case_kc_mdp_meta);
    run_case(&rc, case_kc_mdp_close);
    run_case(&rc, case_kc_mdp_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches the requested test case.
 * @param argc Command-line argument count.
 * @param argv Command-line argument vector.
 * @return 0 on success, nonzero on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }

    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_mdp_open") == 0) return case_kc_mdp_open();
    if (strcmp(argv[1], "kc_mdp_html") == 0) return case_kc_mdp_html();
    if (strcmp(argv[1], "kc_mdp_ansi") == 0) return case_kc_mdp_ansi();
    if (strcmp(argv[1], "kc_mdp_extensions") == 0) {
        return case_kc_mdp_extensions();
    }
    if (strcmp(argv[1], "kc_mdp_body") == 0) return case_kc_mdp_body();
    if (strcmp(argv[1], "kc_mdp_meta") == 0) return case_kc_mdp_meta();
    if (strcmp(argv[1], "kc_mdp_close") == 0) return case_kc_mdp_close();
    if (strcmp(argv[1], "kc_mdp_version") == 0) return case_kc_mdp_version();

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}

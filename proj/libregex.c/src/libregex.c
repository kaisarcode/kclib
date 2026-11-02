/**
 * libregex.c - Portable regular expression matching.
 * Summary: Compiles small regular expressions without host regex dependencies.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libregex.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_REGEX_BUILD_VERSION
#define KC_REGEX_BUILD_VERSION 0
#endif

#define KC_REGEX_PATTERN_MAX 4096U
#define KC_REGEX_NODES_MAX 512U
#define KC_REGEX_STEPS_MAX 100000U
#define KC_REGEX_DEPTH_MAX 4096U

typedef enum {
    KC_REGEX_CHAR, KC_REGEX_DOT, KC_REGEX_CLASS, KC_REGEX_SEQUENCE,
    KC_REGEX_CHOICE, KC_REGEX_REPEAT, KC_REGEX_GROUP
} kc_regex_kind_t;

typedef struct kc_regex_node {
    kc_regex_kind_t kind;
    struct kc_regex_node *left;
    struct kc_regex_node *right;
    unsigned char bitmap[32];
    unsigned char ch;
    size_t group;
    size_t minimum;
    size_t maximum;
} kc_regex_node_t;

typedef struct {
    const char *text;
    size_t pos;
    size_t nodes;
    size_t groups;
    int error;
} kc_regex_parser_t;

typedef struct {
    size_t pos;
    int full;
    size_t start[KC_REGEX_CAPTURES];
    size_t end[KC_REGEX_CAPTURES];
} kc_regex_state_t;

typedef struct kc_regex_cont_t {
    const struct kc_regex_cont_t *parent;
    const kc_regex_node_t *node;
    size_t group;
    size_t repeat_count;
    size_t repeat_pos;
    int type;
} kc_regex_cont_t;

/**
 * Release one compiled pattern tree recursively.
 * @param node Owned tree root, or NULL.
 * @return None.
 */
static void kc_regex_node_free(kc_regex_node_t *node)
{
    if (!node) return;
    kc_regex_node_free(node->left);
    kc_regex_node_free(node->right);
    free(node);
}

/**
 * Allocate one bounded pattern node.
 * @param parser Mutable parser context.
 * @param kind Node operation.
 * @return New node or NULL on invalid complexity or allocation.
 */
static kc_regex_node_t *kc_regex_node_new(kc_regex_parser_t *parser,
    kc_regex_kind_t kind)
{
    kc_regex_node_t *node;

    if (++parser->nodes > KC_REGEX_NODES_MAX) {
        parser->error = 1;
        return NULL;
    }
    node = (kc_regex_node_t *)calloc(1U, sizeof(*node));
    if (!node) parser->error = 1;
    else node->kind = kind;
    return node;
}

/**
 * Set one supported character-class byte.
 * @param node Character-class node.
 * @param byte Byte to accept.
 * @return None.
 */
static void kc_regex_class_byte(kc_regex_node_t *node, unsigned char byte)
{
    node->bitmap[byte / 8U] |= (unsigned char)(1U << (byte % 8U));
}

/**
 * Parse one pattern expression, including alternatives.
 * @param parser Current parser state.
 * @return Owned pattern AST, or NULL for invalid syntax.
 */
static kc_regex_node_t *kc_regex_parse_alt(kc_regex_parser_t *parser);

/**
 * Parse a literal, class, group or wildcard atom.
 * @param parser Current parser state.
 * @return Owned node, or NULL on failure.
 */
static kc_regex_node_t *kc_regex_parse_atom(kc_regex_parser_t *parser)
{
    kc_regex_node_t *node;
    unsigned char ch = (unsigned char)parser->text[parser->pos++];

    if (ch == '(') {
        if (++parser->groups > KC_REGEX_CAPTURES) {
            parser->error = 1;
            return NULL;
        }
        node = kc_regex_node_new(parser, KC_REGEX_GROUP);
        if (!node) return NULL;
        node->group = parser->groups - 1U;
        node->left = kc_regex_parse_alt(parser);
        if (parser->text[parser->pos] != ')') parser->error = 1;
        else parser->pos++;
        if (parser->error) {
            kc_regex_node_free(node);
            return NULL;
        }
        return node;
    }
    if (ch == '[') {
        int negate = 0, first = 1, closed = 0;
        unsigned char prev = 0;

        node = kc_regex_node_new(parser, KC_REGEX_CLASS);
        if (!node) return NULL;
        if (parser->text[parser->pos] == '^') {
            negate = 1;
            parser->pos++;
        }
        while (parser->text[parser->pos]) {
            unsigned char current =
                (unsigned char)parser->text[parser->pos++];
            if (current == ']' && !first) {
                closed = 1;
                break;
            }
            if (current == '\\' && parser->text[parser->pos])
                current = (unsigned char)parser->text[parser->pos++];
            if (current == '-' && !first &&
                parser->text[parser->pos] &&
                parser->text[parser->pos] != ']') {
                unsigned char last =
                    (unsigned char)parser->text[parser->pos++];
                unsigned int i;

                if (last == '\\' && parser->text[parser->pos])
                    last = (unsigned char)parser->text[parser->pos++];
                if (last < prev) {
                    parser->error = 1;
                    break;
                }
                for (i = prev; i <= last; ++i)
                    kc_regex_class_byte(node, (unsigned char)i);
                prev = last;
            } else {
                kc_regex_class_byte(node, current);
                prev = current;
            }
            first = 0;
        }
        if (!closed || first) parser->error = 1;
        if (negate) {
            size_t i;
            for (i = 0U; i < sizeof(node->bitmap); ++i)
                node->bitmap[i] = (unsigned char)~node->bitmap[i];
        }
        if (parser->error) {
            kc_regex_node_free(node);
            return NULL;
        }
        return node;
    }
    if (ch == '.' ) return kc_regex_node_new(parser, KC_REGEX_DOT);
    if (ch == '\\' && parser->text[parser->pos])
        ch = (unsigned char)parser->text[parser->pos++];
    else if (ch == '\\' || ch == '*' || ch == '+' || ch == '?' ||
             ch == ')' || ch == '|' || ch == '{' || ch == '}' ||
             ch == '^' || ch == '$') {
        parser->error = 1;
        return NULL;
    }
    node = kc_regex_node_new(parser, KC_REGEX_CHAR);
    if (node) node->ch = ch;
    return node;
}

/**
 * Parse concatenated atoms with optional repetition.
 * @param parser Current parser state.
 * @return Owned sequence tree, or NULL for an empty sequence.
 */
static kc_regex_node_t *kc_regex_parse_seq(kc_regex_parser_t *parser)
{
    kc_regex_node_t *tree = NULL;

    while (!parser->error && parser->text[parser->pos] &&
        parser->text[parser->pos] != ')' &&
        parser->text[parser->pos] != '|') {
        kc_regex_node_t *atom = kc_regex_parse_atom(parser);
        unsigned char next = (unsigned char)parser->text[parser->pos];

        if (!atom) break;
        if (next == '*' || next == '+' || next == '?') {
            kc_regex_node_t *repeat = kc_regex_node_new(parser,
                KC_REGEX_REPEAT);
            if (!repeat) {
                kc_regex_node_free(atom);
                break;
            }
            repeat->left = atom;
            repeat->minimum = next == '+' ? 1U : 0U;
            repeat->maximum = next == '?' ? 1U : SIZE_MAX;
            atom = repeat;
            parser->pos++;
        }
        if (tree) {
            kc_regex_node_t *sequence = kc_regex_node_new(parser,
                KC_REGEX_SEQUENCE);
            if (!sequence) {
                kc_regex_node_free(atom);
                break;
            }
            sequence->left = tree;
            sequence->right = atom;
            tree = sequence;
        } else tree = atom;
    }
    if (parser->error) {
        kc_regex_node_free(tree);
        return NULL;
    }
    return tree;
}

/**
 * Parse alternations of full sequences.
 * @param parser Parser context.
 * @return Owned AST, or NULL on invalid input.
 */
static kc_regex_node_t *kc_regex_parse_alt(kc_regex_parser_t *parser)
{
    kc_regex_node_t *tree = kc_regex_parse_seq(parser);

    while (!parser->error && parser->text[parser->pos] == '|') {
        kc_regex_node_t *choice = kc_regex_node_new(parser,
            KC_REGEX_CHOICE);
        if (!choice) break;
        parser->pos++;
        choice->left = tree;
        choice->right = kc_regex_parse_seq(parser);
        tree = choice;
    }
    if (parser->error) {
        kc_regex_node_free(tree);
        return NULL;
    }
    return tree;
}

/**
 * Compile the supported portable ERE subset.
 * @param pattern Borrowed pattern text.
 * @param groups Receives number of capture groups.
 * @return Owned tree or NULL for invalid syntax.
 */
static kc_regex_node_t *kc_regex_compile(const char *pattern, size_t *groups)
{
    kc_regex_parser_t parser = {0};
    kc_regex_node_t *node;
    size_t len;

    if (!pattern || !groups) return NULL;
    len = strlen(pattern);
    if (len > KC_REGEX_PATTERN_MAX) return NULL;
    parser.text = pattern;
    node = kc_regex_parse_alt(&parser);
    if (parser.error || parser.text[parser.pos]) {
        kc_regex_node_free(node);
        return NULL;
    }
    *groups = parser.groups;
    return node;
}

/**
 * Advance a match into its next continuation.
 * @param cont Pending sequence, group or repetition.
 * @param path Canonical request path.
 * @param state Current capture state.
 * @param result Receives completed match state.
 * @param steps Shared bounded work counter.
 * @param depth Current recursion depth.
 * @return Nonzero when the full path matched.
 */
static int kc_regex_advance(const kc_regex_cont_t *cont, const char *path,
    kc_regex_state_t state, kc_regex_state_t *result,
    size_t *steps, size_t depth);

/**
 * Evaluate one compiled node with bounded backtracking.
 * @param node Current expression node.
 * @param cont Remaining pattern continuation.
 * @param path Canonical request path.
 * @param state Current position and captures.
 * @param result Receives complete match.
 * @param steps Shared work counter.
 * @param depth Recursion depth.
 * @return Nonzero for a complete path match.
 */
static int kc_regex_match_node(const kc_regex_node_t *node,
    const kc_regex_cont_t *cont, const char *path,
    kc_regex_state_t state, kc_regex_state_t *result,
    size_t *steps, size_t depth)
{
    kc_regex_cont_t next;

    if (++*steps > KC_REGEX_STEPS_MAX || depth > KC_REGEX_DEPTH_MAX) {
        *steps = KC_REGEX_STEPS_MAX + 1U;
        return 0;
    }
    if (!node) return kc_regex_advance(cont, path, state, result,
        steps, depth + 1U);
    switch (node->kind) {
        case KC_REGEX_SEQUENCE:
            next.parent = cont;
            next.node = node->right;
            next.type = 0;
            return kc_regex_match_node(node->left, &next, path, state,
                result, steps, depth + 1U);
        case KC_REGEX_CHOICE:
            if (kc_regex_match_node(node->left, cont, path, state,
                    result, steps, depth + 1U)) return 1;
            return kc_regex_match_node(node->right, cont, path, state,
                result, steps, depth + 1U);
        case KC_REGEX_REPEAT:
            next.parent = cont;
            next.node = node;
            next.type = 2;
            next.repeat_count = 0U;
            next.repeat_pos = state.pos;
            return kc_regex_advance(&next, path, state, result,
                steps, depth + 1U);
        case KC_REGEX_GROUP:
            next.parent = cont;
            next.node = NULL;
            next.type = 1;
            next.group = node->group;
            state.start[node->group] = state.pos;
            return kc_regex_match_node(node->left, &next, path, state,
                result, steps, depth + 1U);
        case KC_REGEX_CHAR:
            if ((unsigned char)path[state.pos] != node->ch ||
                path[state.pos] == '\0') return 0;
            break;
        case KC_REGEX_DOT:
            if (path[state.pos] == '\0') return 0;
            break;
        case KC_REGEX_CLASS:
            if (path[state.pos] == '\0' ||
                !(node->bitmap[(unsigned char)path[state.pos] / 8U] &
                (1U << ((unsigned char)path[state.pos] % 8U)))) return 0;
            break;
    }
    state.pos++;
    return kc_regex_advance(cont, path, state, result, steps, depth + 1U);
}

/**
 * Continue a pending sequence, group closure or greedy repeat.
 * @param cont Continuation frame.
 * @param path Canonical path.
 * @param state Current match state.
 * @param result Receives a successful complete match.
 * @param steps Bounded work counter.
 * @param depth Recursion depth.
 * @return Nonzero when the entire pattern matches.
 */
static int kc_regex_advance(const kc_regex_cont_t *cont, const char *path,
    kc_regex_state_t state, kc_regex_state_t *result,
    size_t *steps, size_t depth)
{
    kc_regex_cont_t next;

    if (++*steps > KC_REGEX_STEPS_MAX || depth > KC_REGEX_DEPTH_MAX)
        return 0;
    if (!cont) {
        if (state.full && path[state.pos]) return 0;
        *result = state;
        return 1;
    }
    if (cont->type == 0)
        return kc_regex_match_node(cont->node, cont->parent, path,
            state, result, steps, depth + 1U);
    if (cont->type == 1) {
        state.end[cont->group] = state.pos;
        return kc_regex_advance(cont->parent, path, state, result,
            steps, depth + 1U);
    }
    if (cont->type == 2) {
        const kc_regex_node_t *repeat = cont->node;

        if (cont->repeat_count < repeat->maximum &&
            (cont->repeat_count == 0U || state.pos > cont->repeat_pos)) {
            next = *cont;
            next.repeat_count++;
            next.repeat_pos = state.pos;
            if (kc_regex_match_node(repeat->left, &next, path, state,
                    result, steps, depth + 1U)) return 1;
        }
        if (cont->repeat_count >= repeat->minimum)
            return kc_regex_advance(cont->parent, path, state, result,
                steps, depth + 1U);
    }
    return 0;
}

/**
 * Match one expression against the complete path.
 * @param node Compiled pattern.
 * @param groups Number of capture groups.
 * @param path Canonical URI path.
 * @param result Receives captured byte ranges.
 * @return Nonzero when matched.
 */
static int kc_regex_run(const kc_regex_node_t *node, size_t groups,
    const char *path, size_t offset, int full, kc_regex_state_t *result,
    size_t *work)
{
    kc_regex_state_t state = {0};
    size_t i;

    state.pos = offset;
    state.full = full;

    for (i = 0U; i < groups; ++i) {
        state.start[i] = SIZE_MAX;
        state.end[i] = SIZE_MAX;
    }
    return kc_regex_match_node(node, NULL, path, state, result,
        work, 0U);
}

struct kc_regex {
    kc_regex_node_t *pattern;
    size_t groups;
};

/**
 * Compile a pattern into an independent reusable handle.
 * @param out Receives owned regex.
 * @param pattern Borrowed pattern.
 * @return Result status.
 */
int kc_regex_open(kc_regex_t **out, const char *pattern)
{
    kc_regex_t *regex;

    if (!out || !pattern) return KC_REGEX_EINVAL;
    *out = NULL;
    regex = (kc_regex_t *)calloc(1U, sizeof(*regex));
    if (!regex) return KC_REGEX_ENOMEM;
    regex->pattern = kc_regex_compile(pattern, &regex->groups);
    if (!regex->pattern && pattern[0]) {
        free(regex);
        return KC_REGEX_EPATTERN;
    }
    *out = regex;
    return KC_REGEX_OK;
}

/**
 * Copy all available captures to a caller-owned output array.
 * @param regex Compiled pattern.
 * @param state Completed match spans.
 * @param start Start of group zero.
 * @param captures Optional output.
 * @param capacity Available slots.
 * @param count Optional total slots.
 * @return KC_REGEX_OK.
 */
static int kc_regex_output(const kc_regex_t *regex,
    const kc_regex_state_t *state, size_t start,
    kc_regex_capture_t *captures, size_t capacity, size_t *count)
{
    size_t i, total = regex->groups + 1U;

    if (count) *count = total;
    for (i = 0U; i < capacity && i < total; ++i) {
        if (i == 0U) {
            captures[i].start = start;
            captures[i].end = state->pos;
        } else {
            captures[i].start = state->start[i - 1U];
            captures[i].end = state->end[i - 1U];
        }
    }
    return KC_REGEX_OK;
}

/**
 * Match an entire input, preserving group byte offsets.
 * @param regex Compiled expression.
 * @param text Borrowed input.
 * @param captures Optional capture array.
 * @param capacity Number of slots.
 * @param count Optional required slot count.
 * @return Success, no match or a negative status.
 */
int kc_regex_match(const kc_regex_t *regex, const char *text,
    kc_regex_capture_t *captures, size_t capacity, size_t *count)
{
    kc_regex_state_t state;
    size_t steps = 0U;

    if (count) *count = 0U;
    if (!regex || !text || (capacity && !captures))
        return KC_REGEX_EINVAL;
    if (!kc_regex_run(regex->pattern, regex->groups, text, 0U, 1,
            &state, &steps))
        return steps > KC_REGEX_STEPS_MAX ?
            KC_REGEX_ELIMIT : KC_REGEX_NOMATCH;
    return kc_regex_output(regex, &state, 0U, captures, capacity, count);
}

/**
 * Locate the earliest match from an arbitrary byte offset.
 * @param regex Compiled expression.
 * @param text Borrowed input.
 * @param offset Starting byte index.
 * @param captures Optional capture array.
 * @param capacity Number of slots.
 * @param count Optional group count.
 * @return Success, no match or a negative status.
 */
int kc_regex_search(const kc_regex_t *regex, const char *text, size_t offset,
    kc_regex_capture_t *captures, size_t capacity, size_t *count)
{
    size_t len, start;

    if (count) *count = 0U;
    if (!regex || !text || (capacity && !captures))
        return KC_REGEX_EINVAL;
    len = strlen(text);
    if (offset > len) return KC_REGEX_EINVAL;
    for (start = offset; start <= len; ++start) {
        kc_regex_state_t state;
        size_t steps = 0U;

        if (kc_regex_run(regex->pattern, regex->groups, text, start, 0,
                &state, &steps))
            return kc_regex_output(regex, &state, start,
                captures, capacity, count);
        if (steps > KC_REGEX_STEPS_MAX) return KC_REGEX_ELIMIT;
    }
    return KC_REGEX_NOMATCH;
}

/**
 * Release compiled expression and its owned syntax tree.
 * @param regex Owned expression or NULL.
 * @return None.
 */
void kc_regex_close(kc_regex_t *regex)
{
    if (!regex) return;
    kc_regex_node_free(regex->pattern);
    free(regex);
}

/**
 * Describe one status code.
 * @param status Regex result.
 * @return Static description.
 */
const char *kc_regex_strerror(int status)
{
    switch (status) {
        case KC_REGEX_OK: return "success";
        case KC_REGEX_NOMATCH: return "no match";
        case KC_REGEX_EINVAL: return "invalid argument";
        case KC_REGEX_ENOMEM: return "out of memory";
        case KC_REGEX_EPATTERN: return "invalid pattern";
        case KC_REGEX_ELIMIT: return "matching limit exceeded";
        default: return "unknown error";
    }
}

/**
 * Return the generated build timestamp.
 * @return Build timestamp or zero.
 */
uint64_t kc_regex_version(void)
{
    return (uint64_t)KC_REGEX_BUILD_VERSION;
}

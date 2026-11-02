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

typedef struct {
    const kc_regex_node_t *node;
    size_t parent;
    size_t group;
    size_t repeat_count;
    size_t repeat_pos;
    int type;
} kc_regex_cont_t;

typedef struct {
    const kc_regex_node_t *node;
    kc_regex_state_t state;
    size_t cont;
    int type;
} kc_regex_job_t;

typedef struct {
    kc_regex_job_t *jobs;
    size_t job_count;
    size_t job_capacity;
    kc_regex_cont_t *frames;
    size_t frame_count;
    size_t frame_capacity;
} kc_regex_work_t;

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
 * Store a backtracking task in an explicitly allocated work stack.
 * The work limit bounds both matching and queued alternatives.
 * @param work Mutable matching workspace.
 * @param node Node to evaluate, or NULL for continuation.
 * @param cont Continuation index or SIZE_MAX.
 * @param state Current captures and input position.
 * @param type Zero evaluates a node, one advances a continuation.
 * @return Success, allocation error or work limit.
 */
static int kc_regex_schedule(kc_regex_work_t *work,
    const kc_regex_node_t *node, size_t cont,
    kc_regex_state_t state, int type)
{
    kc_regex_job_t *jobs;
    size_t capacity;

    if (work->job_count == KC_REGEX_STEPS_MAX) return KC_REGEX_ELIMIT;
    if (work->job_count == work->job_capacity) {
        capacity = work->job_capacity ? work->job_capacity * 2U : 32U;
        if (capacity > KC_REGEX_STEPS_MAX) capacity = KC_REGEX_STEPS_MAX;
        jobs = (kc_regex_job_t *)realloc(work->jobs,
            capacity * sizeof(*jobs));
        if (!jobs) return KC_REGEX_ENOMEM;
        work->jobs = jobs;
        work->job_capacity = capacity;
    }
    work->jobs[work->job_count].node = node;
    work->jobs[work->job_count].cont = cont;
    work->jobs[work->job_count].state = state;
    work->jobs[work->job_count].type = type;
    work->job_count++;
    return KC_REGEX_OK;
}

/**
 * Store one persistent continuation frame using a stable array index.
 * @param work Mutable matching workspace.
 * @param frame New continuation and its parent index.
 * @param index Receives a stable index valid for this match.
 * @return Success, allocation error or work limit.
 */
static int kc_regex_frame(kc_regex_work_t *work,
    kc_regex_cont_t frame, size_t *index)
{
    kc_regex_cont_t *frames;
    size_t capacity;

    if (work->frame_count == KC_REGEX_STEPS_MAX) return KC_REGEX_ELIMIT;
    if (work->frame_count == work->frame_capacity) {
        capacity = work->frame_capacity ? work->frame_capacity * 2U : 32U;
        if (capacity > KC_REGEX_STEPS_MAX) capacity = KC_REGEX_STEPS_MAX;
        frames = (kc_regex_cont_t *)realloc(work->frames,
            capacity * sizeof(*frames));
        if (!frames) return KC_REGEX_ENOMEM;
        work->frames = frames;
        work->frame_capacity = capacity;
    }
    *index = work->frame_count;
    work->frames[work->frame_count++] = frame;
    return KC_REGEX_OK;
}

/**
 * Evaluate a compiled expression without consuming the native call stack.
 * Work and backtracking alternatives live in bounded, temporary arrays.
 * Alternatives and repetitions are evaluated in left-first greedy order.
 * @param node Compiled expression root.
 * @param groups Number of capture groups.
 * @param path Borrowed input text.
 * @param offset Start position.
 * @param full Whether to require complete input consumption.
 * @param result Receives the accepted match and group spans.
 * @param steps Shared work budget, including previous search offsets.
 * @return KC_REGEX_OK, KC_REGEX_NOMATCH, or a negative status.
 */
static int kc_regex_run(const kc_regex_node_t *node, size_t groups,
    const char *path, size_t offset, int full, kc_regex_state_t *result,
    size_t *steps)
{
    kc_regex_work_t work = {0};
    kc_regex_state_t state = {0};
    size_t i, length = strlen(path);
    int status;

    state.pos = offset;
    state.full = full;
    for (i = 0U; i < groups; ++i) {
        state.start[i] = SIZE_MAX;
        state.end[i] = SIZE_MAX;
    }
    status = kc_regex_schedule(&work, node, SIZE_MAX, state, 0);
    while (status == KC_REGEX_OK && work.job_count) {
        kc_regex_job_t job;
        kc_regex_cont_t frame;
        size_t next;

        if (*steps >= KC_REGEX_STEPS_MAX) {
            status = KC_REGEX_ELIMIT;
            break;
        }
        ++*steps;
        job = work.jobs[--work.job_count];
        state = job.state;

        if (job.type == 0) {
            if (!job.node) {
                status = kc_regex_schedule(&work, NULL, job.cont,
                    state, 1);
                continue;
            }
            switch (job.node->kind) {
                case KC_REGEX_SEQUENCE:
                    frame = (kc_regex_cont_t){0};
                    frame.type = 0;
                    frame.parent = job.cont;
                    frame.node = job.node->right;
                    status = kc_regex_frame(&work, frame, &next);
                    if (status == KC_REGEX_OK)
                        status = kc_regex_schedule(&work,
                            job.node->left, next, state, 0);
                    break;

                case KC_REGEX_CHOICE:
                    status = kc_regex_schedule(&work, job.node->right,
                        job.cont, state, 0);
                    if (status == KC_REGEX_OK)
                        status = kc_regex_schedule(&work, job.node->left,
                            job.cont, state, 0);
                    break;

                case KC_REGEX_REPEAT:
                    frame = (kc_regex_cont_t){0};
                    frame.type = 2;
                    frame.parent = job.cont;
                    frame.node = job.node;
                    frame.repeat_pos = state.pos;
                    status = kc_regex_frame(&work, frame, &next);
                    if (status == KC_REGEX_OK)
                        status = kc_regex_schedule(&work, NULL,
                            next, state, 1);
                    break;

                case KC_REGEX_GROUP:
                    frame = (kc_regex_cont_t){0};
                    frame.type = 1;
                    frame.parent = job.cont;
                    frame.group = job.node->group;
                    state.start[frame.group] = state.pos;
                    status = kc_regex_frame(&work, frame, &next);
                    if (status == KC_REGEX_OK)
                        status = kc_regex_schedule(&work,
                            job.node->left, next, state, 0);
                    break;

                case KC_REGEX_CHAR:
                    if (state.pos < length &&
                        (unsigned char)path[state.pos] == job.node->ch) {
                        state.pos++;
                        status = kc_regex_schedule(&work, NULL,
                            job.cont, state, 1);
                    }
                    break;

                case KC_REGEX_DOT:
                    if (state.pos < length) {
                        state.pos++;
                        status = kc_regex_schedule(&work, NULL,
                            job.cont, state, 1);
                    }
                    break;

                case KC_REGEX_CLASS:
                    if (state.pos < length) {
                        unsigned char ch = (unsigned char)path[state.pos];

                        if (job.node->bitmap[ch / 8U] &
                            (1U << (ch % 8U))) {
                            state.pos++;
                            status = kc_regex_schedule(&work, NULL,
                                job.cont, state, 1);
                        }
                    }
                    break;
            }
        } else if (job.cont == SIZE_MAX) {
            if (!state.full || state.pos == length) {
                *result = state;
                goto done;
            }
        } else {
            frame = work.frames[job.cont];
            switch (frame.type) {
                case 0:
                    status = kc_regex_schedule(&work, frame.node,
                        frame.parent, state, 0);
                    break;

                case 1:
                    state.end[frame.group] = state.pos;
                    status = kc_regex_schedule(&work, NULL,
                        frame.parent, state, 1);
                    break;

                case 2:
                    if (frame.repeat_count >= frame.node->minimum)
                        status = kc_regex_schedule(&work, NULL,
                            frame.parent, state, 1);
                    if (status != KC_REGEX_OK) break;
                    if (frame.repeat_count < frame.node->maximum &&
                        (frame.repeat_count == 0U ||
                         state.pos > frame.repeat_pos)) {
                        frame.repeat_count++;
                        frame.repeat_pos = state.pos;
                        status = kc_regex_frame(&work, frame, &next);
                        if (status == KC_REGEX_OK)
                            status = kc_regex_schedule(&work,
                                frame.node->left, next, state, 0);
                    }
                    break;
            }
        }
    }
    if (status == KC_REGEX_OK) status = KC_REGEX_NOMATCH;

done:
    free(work.jobs);
    free(work.frames);
    return status;
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
    {
        int status = kc_regex_run(regex->pattern, regex->groups, text,
            0U, 1, &state, &steps);

        if (status != KC_REGEX_OK) return status;
    }
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
    size_t len, start, steps = 0U;

    if (count) *count = 0U;
    if (!regex || !text || (capacity && !captures))
        return KC_REGEX_EINVAL;
    len = strlen(text);
    if (offset > len) return KC_REGEX_EINVAL;
    for (start = offset; start <= len; ++start) {
        kc_regex_state_t state;
        int status = kc_regex_run(regex->pattern, regex->groups, text,
            start, 0, &state, &steps);

        if (status == KC_REGEX_OK)
            return kc_regex_output(regex, &state, start,
                captures, capacity, count);
        if (status != KC_REGEX_NOMATCH) return status;
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

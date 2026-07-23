// A faithful implementation of the classic Unix ed line editor.
// Supports all POSIX ed commands: a, c, d, e, E, f, g, h, H, i, j,
// k, l, m, n, p, P, q, Q, r, s, t, u, v, w, W, =, !, #
// with addresses: n, ., $, +n, -n, /re/, ?re?, 'x, % , (1,$), ; (.,$)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAX_LINES 500
#define MAX_LINE_LEN 128
#define MAX_FILENAME 64
#define MAX_CMD 512

// ---- Global State ----
char buffer[MAX_LINES][MAX_LINE_LEN];
int total_lines = 0;          // number of lines in buffer
int current_line = -1;        // 0-indexed; -1 means no current line (buffer empty)
char filename[MAX_FILENAME];  // current filename
int modified = 0;             // buffer modified since last whole-buffer write
int verbose_errors = 0;       // H toggles this
int show_prompt = 0;          // P toggles this
char last_error[128];         // last error message (for h command)
char last_re[128];            // last search/regex pattern
char last_sub_pat[128];       // last substitution pattern
char last_sub_rep[128];       // last substitution replacement
int  last_sub_gflag = 0;      // last substitution was global
char last_sub_delim = '/';    // last substitution delimiter
int  pending_report = 0;      // non-zero if s command will itself print the line
char last_shell_cmd[MAX_CMD]; // last shell command for !!

// ---- Undo State ----
int undo_avail = 0;                 // 1 if undo state is valid
int undo_total;                     // saved total_lines
int undo_current;                   // saved current_line
char undo_buffer[MAX_LINES][MAX_LINE_LEN];  // saved buffer

// ---- Mark State ----
int marks[26];   // line number for mark 'a'..'z', or -1 if unset

// ---- Function Declarations ----
void error(char *msg);
int match(char *re, char *text);
int get_line(char *s, int max);
char *skip_ws(char *p);

// ---- String Helpers ----

int my_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void my_strcpy(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < MAX_LINE_LEN - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *(unsigned char *)a - *(unsigned char *)b;
}

void my_strncpy(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n-1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

// ---- Error handling ----

void error(char *msg) {
    printf("?\n");
    my_strcpy(last_error, msg);
    if (verbose_errors && msg[0] != '\0')
        printf("%s\n", msg);
}

// ---- Snapshot for undo ----

void save_undo(void) {
    undo_total = total_lines;
    undo_current = current_line;
    for (int i = 0; i < total_lines; i++)
        my_strcpy(undo_buffer[i], buffer[i]);
    undo_avail = 1;
}

void do_undo(void) {
    if (!undo_avail) {
        error("no previous change to undo");
        return;
    }
    total_lines = undo_total;
    current_line = undo_current;
    for (int i = 0; i < total_lines; i++)
        my_strcpy(buffer[i], undo_buffer[i]);
    modified = 1;
    undo_avail = 0;  // can only undo once
}

// ---- Regex Matching (from K&R / xv6 grep.c) ----
// Supports: ^ . * $  (basic regex)

int matchhere(char *re, char *text);
int matchstar(int c, char *re, char *text);

int match(char *re, char *text) {
    if (re[0] == '^')
        return matchhere(re + 1, text);
    do {
        if (matchhere(re, text))
            return 1;
    } while (*text++ != '\0');
    return 0;
}

int matchhere(char *re, char *text) {
    if (re[0] == '\0')
        return 1;
    if (re[1] == '*')
        return matchstar(re[0], re + 2, text);
    if (re[0] == '$' && re[1] == '\0')
        return *text == '\0';
    if (*text != '\0' && (re[0] == '.' || re[0] == *text))
        return matchhere(re + 1, text + 1);
    return 0;
}

int matchstar(int c, char *re, char *text) {
    do {
        if (matchhere(re, text))
            return 1;
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}

// ---- I/O ----

// Read a line from stdin.  Stores characters (no newline) into s,
// null-terminates.  Returns number of chars read (0 on EOF).
int get_line(char *s, int max) {
    int i = 0;
    char c;
    while (i < max - 1 && read(0, &c, 1) > 0) {
        if (c == '\r' || c == '\n')
            break;
        s[i++] = c;
    }
    s[i] = '\0';
    return i;
}

// Skip leading whitespace, return first non-whitespace char.
char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

// ---- Buffer Primitives ----

// Insert a line after position 'after' (0-indexed, -1 means before line 0).
// Returns 1 on success, 0 on failure (buffer full).
int insert_line(int after, char *s) {
    if (total_lines >= MAX_LINES)
        return 0;
    for (int i = total_lines; i > after + 1; i--)
        my_strcpy(buffer[i], buffer[i - 1]);
    my_strcpy(buffer[after + 1], s);
    total_lines++;
    current_line = after + 1;
    // Adjust marks: marks >= after+1 shift up by 1
    for (int i = 0; i < 26; i++) {
        if (marks[i] >= after + 1)
            marks[i]++;
    }
    return 1;
}

// Delete lines from 'from' to 'to' inclusive (0-indexed).
void delete_lines(int from, int to) {
    int count = to - from + 1;
    for (int i = from; i + count < total_lines; i++)
        my_strcpy(buffer[i], buffer[i + count]);
    total_lines -= count;
    if (total_lines == 0) {
        current_line = -1;
    } else if (current_line > to) {
        current_line -= count;
    } else if (current_line >= from) {
        if (from < total_lines)
            current_line = from;
        else
            current_line = total_lines - 1;
    }
    // Adjust marks: marks > to shift down by count; marks in [from,to] become invalid
    for (int i = 0; i < 26; i++) {
        if (marks[i] > to)
            marks[i] -= count;
        else if (marks[i] >= from)
            marks[i] = -1;
    }
}

// ---- Display ----

void do_print(int from, int to) {
    for (int i = from; i <= to; i++) {
        printf("%s\n", buffer[i]);
    }
    current_line = to;
}

void do_number(int from, int to) {
    for (int i = from; i <= to; i++) {
        printf("%d\t%s\n", i + 1, buffer[i]);
    }
    current_line = to;
}

void do_list(int from, int to) {
    for (int i = from; i <= to; i++) {
        char *p = buffer[i];
        while (*p) {
            if (*p == '\t')
                printf(">");
            else if (*p == '\b')
                printf("\\b");
            else if (*p == '\\')
                printf("\\\\");
            else
                printf("%c", *p);
            p++;
        }
        printf("$\n");
    }
    current_line = to;
}

void do_equal(int line) {
    printf("%d\n", line + 1);
}

// ---- Edit Operations ----

void do_append(int after) {
    save_undo();
    char input[MAX_LINE_LEN];
    while (1) {
        int n = get_line(input, MAX_LINE_LEN);
        if (n <= 0) break; // EOF
        if (my_strcmp(input, ".") == 0) break;
        if (!insert_line(after, input)) {
            error("buffer overflow");
            break;
        }
        after++;
    }
    modified = 1;
}

void do_insert(int before) {
    do_append(before - 1);
}

void do_delete(int from, int to) {
    if (from < 0 || from >= total_lines || to < from || to >= total_lines) {
        error("out of range");
        return;
    }
    save_undo();
    delete_lines(from, to);
    modified = 1;
}

void do_change(int from, int to) {
    if (from < 0 || from >= total_lines || to < from || to >= total_lines) {
        if (from == 0 && total_lines == 0) {
            // address 0 in empty buffer: just append
            do_append(-1);
            return;
        }
        error("out of range");
        return;
    }
    save_undo();
    int after = from - 1;
    delete_lines(from, to);
    // Now enter append mode at the deletion point
    char input[MAX_LINE_LEN];
    while (1) {
        int n = get_line(input, MAX_LINE_LEN);
        if (n <= 0) break;
        if (my_strcmp(input, ".") == 0) break;
        if (!insert_line(after, input)) {
            error("buffer overflow");
            break;
        }
        after++;
    }
    modified = 1;
}

// Apply a single s/// substitution to one line.
// Returns 1 if a substitution was made, 0 otherwise.
int do_substitute_line(int idx, char *pat, char *rep, int gflag) {
    char line[MAX_LINE_LEN];
    my_strcpy(line, buffer[idx]);
    char result[MAX_LINE_LEN];
    result[0] = '\0';
    char *src = line;
    int substituted = 0;

    while (*src) {
        int anchored = (pat[0] == '^');
        char *pat_body = anchored ? pat + 1 : pat;

        int found = 0;
        int match_len = 0;
        char *match_start = 0;

        if (anchored) {
            if (matchhere(pat_body, src)) {
                match_start = src;
                int len = 0;
                while (src[len]) {
                    char test[MAX_LINE_LEN];
                    int t;
                    for (t = 0; t <= len; t++) test[t] = src[t];
                    test[t] = '\0';
                    if (matchhere(pat_body, test))
                        match_len = len + 1;
                    len++;
                }
                if (match_len == 0) match_len = 1;
                found = 1;
            }
        } else {
            char *scan = src;
            while (*scan) {
                if (matchhere(pat_body, scan)) {
                    match_start = scan;
                    int len = 0;
                    while (scan[len]) {
                        char test[MAX_LINE_LEN];
                        int t;
                        for (t = 0; t <= len; t++) test[t] = scan[t];
                        test[t] = '\0';
                        if (matchhere(pat_body, test))
                            match_len = len + 1;
                        len++;
                    }
                    if (match_len == 0) match_len = 1;
                    found = 1;
                    break;
                }
                scan++;
            }
        }

        if (found && (!substituted || gflag)) {
            // Copy text before the match
            int pre_len = match_start - src;
            int rlen = my_strlen(result);
            int i;
            for (i = 0; i < pre_len && rlen + i < MAX_LINE_LEN - 1; i++)
                result[rlen + i] = src[i];
            result[rlen + i] = '\0';

            // Copy replacement text
            int k = 0;
            while (rep[k] && my_strlen(result) < MAX_LINE_LEN - 1) {
                int rl = my_strlen(result);
                if (rep[k] == '\\' && rep[k+1] == '&') {
                    result[rl] = '&';
                    result[rl+1] = '\0';
                    k += 2;
                } else if (rep[k] == '&') {
                    for (int t = 0; t < match_len && rl + t < MAX_LINE_LEN - 1; t++)
                        result[rl + t] = match_start[t];
                    result[rl + match_len] = '\0';
                    k++;
                } else if (rep[k] == '\\' && rep[k+1] >= '1' && rep[k+1] <= '9') {
                    result[rl] = rep[k];
                    result[rl+1] = rep[k+1];
                    result[rl+2] = '\0';
                    k += 2;
                } else if (rep[k] == '\\' && rep[k+1] != '\0') {
                    result[rl] = rep[k+1];
                    result[rl+1] = '\0';
                    k += 2;
                } else {
                    result[rl] = rep[k];
                    result[rl+1] = '\0';
                    k++;
                }
            }

            src = match_start + match_len;
            substituted = 1;
        } else {
            // No more matches: copy rest
            int rlen = my_strlen(result);
            int i;
            for (i = 0; src[i] && rlen + i < MAX_LINE_LEN - 1; i++)
                result[rlen + i] = src[i];
            result[rlen + i] = '\0';
            break;
        }
    }

    if (substituted) {
        my_strcpy(buffer[idx], result);
        current_line = idx;
    }
    return substituted;
}

void do_substitute(int from, int to, char *pat, char *rep, int gflag) {
    save_undo();
    int last_matched = -1;
    for (int i = from; i <= to; i++) {
        if (match(pat, buffer[i])) {
            do_substitute_line(i, pat, rep, gflag);
            last_matched = i;
        }
    }
    if (last_matched >= 0) {
        current_line = last_matched;
        // Print the line if no command-suffix flags were given
        if (!pending_report)
            printf("%s\n", buffer[current_line]);
        modified = 1;
    } else {
        error("no match");
    }
}

void do_join(int from, int to) {
    if (from < 0 || from >= total_lines || to <= from || to >= total_lines) {
        error("out of range");
        return;
    }
    // Check total length first to avoid silent truncation
    int total_len = 0;
    for (int i = from; i <= to; i++)
        total_len += my_strlen(buffer[i]);
    if (total_len >= MAX_LINE_LEN - 1) {
        error("result too long");
        return;
    }
    save_undo();
    char *dest = buffer[from];
    int dlen = my_strlen(dest);
    for (int i = from + 1; i <= to; i++) {
        int slen = my_strlen(buffer[i]);
        for (int j = 0; j < slen; j++)
            dest[dlen++] = buffer[i][j];
        dest[dlen] = '\0';
    }
    delete_lines(from + 1, to);
    current_line = from;
    modified = 1;
}

void do_move(int from, int to, int dest) {
    int count = to - from + 1;
    // Use heap allocation to avoid 64KB stack overflow
    char **saved = malloc(count * sizeof(char *));
    if (!saved) { error("out of memory"); return; }
    for (int i = 0; i < count; i++) {
        saved[i] = malloc(MAX_LINE_LEN);
        if (!saved[i]) { error("out of memory"); for (int j = 0; j < i; j++) free(saved[j]); free(saved); return; }
        my_strcpy(saved[i], buffer[from + i]);
    }

    save_undo();
    delete_lines(from, to);

    // Adjust destination after deletion
    if (dest > from)
        dest -= count;
    if (dest < -1) dest = -1;
    if (dest > total_lines) dest = total_lines;
    if (dest > total_lines - 1) dest = total_lines - 1;

    for (int i = 0; i < count; i++) {
        insert_line(dest, saved[i]);
        free(saved[i]);
        dest++;
    }
    free(saved);
    current_line = dest - 1;
    modified = 1;
}

int do_copy(int from, int to, int dest) {
    int count = to - from + 1;
    // Use heap allocation to avoid 64KB stack overflow
    char **saved = malloc(count * sizeof(char *));
    if (!saved) { error("out of memory"); return 0; }
    for (int i = 0; i < count; i++) {
        saved[i] = malloc(MAX_LINE_LEN);
        if (!saved[i]) { error("out of memory"); for (int j = 0; j < i; j++) free(saved[j]); free(saved); return 0; }
        my_strcpy(saved[i], buffer[from + i]);
    }

    save_undo();
    if (dest < -1) dest = -1;
    if (dest >= total_lines) dest = total_lines - 1;

    for (int i = 0; i < count; i++) {
        insert_line(dest, saved[i]);
        free(saved[i]);
        dest++;
    }
    free(saved);
    current_line = dest - 1;
    modified = 1;
    return 1;
}

void do_mark(char c) {
    if (c < 'a' || c > 'z') {
        error("invalid mark character");
        return;
    }
    marks[c - 'a'] = current_line;
}

// ---- Global Commands ----

void do_global(char *pat, char *cmd, int inverse) {
    int marks[MAX_LINES];
    int nmarks = 0;
    for (int i = 0; i < total_lines; i++) {
        int matched = match(pat, buffer[i]);
        if (inverse ? !matched : matched) {
            if (nmarks < MAX_LINES)
                marks[nmarks++] = i;
        }
    }
    if (nmarks == 0)
        return;

    save_undo();

    // Process deletions from bottom to top
    int ndel = 0;
    int del_marks[MAX_LINES];

    for (int i = 0; i < nmarks; i++) {
        int idx = marks[i];
        if (cmd[0] == 'p' || cmd[0] == '\0') {
            printf("%s\n", buffer[idx]);
            current_line = idx;
        } else if (cmd[0] == 'n') {
            printf("%d\t%s\n", idx + 1, buffer[idx]);
            current_line = idx;
        } else if (cmd[0] == 'l') {
            do_list(idx, idx);
        } else if (cmd[0] == 'd') {
            del_marks[ndel++] = idx;
        } else if (cmd[0] == 's') {
            char *cp = cmd + 1;
            if (*cp == '/') {
                cp++;
                char pat2[MAX_LINE_LEN];
                char rep2[MAX_LINE_LEN];
                int gflag = 0;
                char delim = '/';
                int k = 0;
                while (*cp && *cp != delim) {
                    if (*cp == '\\' && *(cp+1) == delim) { cp++; pat2[k++] = delim; cp++; }
                    else pat2[k++] = *cp++;
                }
                pat2[k] = '\0';
                if (*cp == delim) cp++;
                k = 0;
                while (*cp && *cp != delim && *cp != 'g' && *cp != 'G') {
                    if (*cp == '\\' && *(cp+1) == delim) { cp++; rep2[k++] = delim; cp++; }
                    else rep2[k++] = *cp++;
                }
                rep2[k] = '\0';
                if (*cp == delim) cp++;
                if (*cp == 'g' || *cp == 'G') gflag = 1;
                do_substitute_line(idx, pat2, rep2, gflag);
            }
        }
    }

    // Process deletions from bottom to top
    for (int i = ndel - 1; i >= 0; i--) {
        delete_lines(del_marks[i], del_marks[i]);
    }
    if (ndel > 0)
        modified = 1;
}

// ---- File Operations ----

// Read entire file into a malloc'd buffer, return size or -1 on error.
// Caller must free the returned buffer.
int read_file_bytes(char *name, char **buf) {
    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        *buf = 0;
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        *buf = 0;
        return -1;
    }
    int size = st.size;
    *buf = malloc(size + 1);
    if (*buf == 0) {
        close(fd);
        return -1;
    }
    int total = 0;
    while (total < size) {
        int n = read(fd, *buf + total, size - total);
        if (n <= 0) break;
        total += n;
    }
    (*buf)[total] = '\0';
    close(fd);
    return total;
}

void load_file(char *name) {
    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        total_lines = 0;
        current_line = -1;
        return;
    }

    char c;
    int line_idx = 0;
    int char_idx = 0;
    char lbuf[MAX_LINE_LEN];
    lbuf[0] = '\0';

    while (read(fd, &c, 1) > 0 && line_idx < MAX_LINES) {
        if (c == '\n') {
            lbuf[char_idx] = '\0';
            my_strcpy(buffer[line_idx], lbuf);
            line_idx++;
            char_idx = 0;
        } else if (char_idx < MAX_LINE_LEN - 1) {
            lbuf[char_idx++] = c;
        }
    }
    if (char_idx > 0 && line_idx < MAX_LINES) {
        lbuf[char_idx] = '\0';
        my_strcpy(buffer[line_idx], lbuf);
        line_idx++;
    }

    total_lines = line_idx;
    if (total_lines > 0)
        current_line = total_lines - 1;   // orig ed: set to last line
    else
        current_line = -1;
    close(fd);
    printf("%d\n", total_lines);
}

int save_file(char *name) {
    int fd = open(name, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) {
        error("cannot open file for writing");
        return 0;
    }
    for (int i = 0; i < total_lines; i++) {
        write(fd, buffer[i], my_strlen(buffer[i]));
        write(fd, "\n", 1);
    }
    close(fd);
    printf("%d\n", total_lines);
    modified = 0;
    return 1;
}

// Append buffer to an existing file (or create it).
int save_file_append(char *name) {
    char *orig = 0;
    int orig_size = read_file_bytes(name, &orig);
    int fd = open(name, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) {
        error("cannot open file for writing");
        if (orig) free(orig);
        return 0;
    }
    if (orig && orig_size > 0) {
        write(fd, orig, orig_size);
    }
    if (orig) free(orig);
    for (int i = 0; i < total_lines; i++) {
        write(fd, buffer[i], my_strlen(buffer[i]));
        write(fd, "\n", 1);
    }
    close(fd);
    printf("%d\n", total_lines);
    // W does NOT clear the modified flag
    return 1;
}

void do_read(char *name, int after) {
    char *content = 0;
    int size = read_file_bytes(name, &content);
    if (size < 0) {
        error("cannot open file for reading");
        return;
    }
    save_undo();
    int line_count = 0;
    int pos = 0;
    char lbuf[MAX_LINE_LEN];
    int ci = 0;
    while (pos < size) {
        char c = content[pos++];
        if (c == '\n') {
            lbuf[ci] = '\0';
            insert_line(after, lbuf);
            after++;
            line_count++;
            ci = 0;
        } else if (ci < MAX_LINE_LEN - 1) {
            lbuf[ci++] = c;
        }
    }
    if (ci > 0) {
        lbuf[ci] = '\0';
        insert_line(after, lbuf);
        line_count++;
    }
    free(content);
    printf("%d\n", line_count);
}

int do_edit(char *name) {
    int i;
    for (i = 0; name[i] && i < MAX_FILENAME - 1; i++)
        filename[i] = name[i];
    filename[i] = '\0';
    modified = 0;
    total_lines = 0;
    current_line = -1;
    load_file(filename);
    return 1;
}

// ---- Address Parsing ----

int parse_one_addr(char **p, int *addr) {
    char *s = *p;
    s = skip_ws(s);

    if (*s == '\0')
        return -1;

    if (*s == '.') {
        if (current_line < 0) {
            error("no current line");
            return -1;
        }
        *addr = current_line;
        s++;
    } else if (*s == '$') {
        if (total_lines == 0) {
            error("empty buffer");
            return -1;
        }
        *addr = total_lines - 1;
        s++;
    } else if (*s == '\'') {
        // Mark reference: 'x
        s++;
        if (*s >= 'a' && *s <= 'z') {
            int m = marks[*s - 'a'];
            if (m < 0) {
                error("mark not set");
                return -1;
            }
            *addr = m;
            s++;
        } else {
            return -1;
        }
    } else if (*s == '+' || *s == '-') {
        int sign = (*s == '+') ? 1 : -1;
        s++;
        int n = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (*s - '0');
            s++;
        }
        if (n == 0) n = 1;
        if (current_line < 0) {
            error("no current line");
            return -1;
        }
        *addr = current_line + sign * n;
        if (*addr < 0) *addr = 0;
        if (*addr >= total_lines) *addr = total_lines - 1;
    } else if (*s == '/') {
        s++;
        char pat[128];
        int k = 0;
        while (*s && *s != '/') {
            if (k >= sizeof(pat) - 1) { while (*s && *s != '/') s++; break; }
            if (*s == '\\' && *(s+1) == '/') { s++; pat[k++] = '/'; s++; }
            else pat[k++] = *s++;
        }
        pat[k] = '\0';
        // If pattern is empty, use last_re
        if (pat[0] == '\0')
            my_strcpy(pat, last_re);
        if (*s == '/') s++;
        my_strcpy(last_re, pat);
        int start = (current_line < 0) ? 0 : current_line + 1;
        int found = -1;
        for (int i = start; i < total_lines; i++) {
            if (match(pat, buffer[i])) { found = i; break; }
        }
        if (found < 0) {
            for (int i = 0; i < start && i < total_lines; i++) {
                if (match(pat, buffer[i])) { found = i; break; }
            }
        }
        if (found < 0) {
            error("no match");
            return -1;
        }
        *addr = found;
        current_line = found;
        // Print the matched line
        printf("%s\n", buffer[found]);
    } else if (*s == '?') {
        s++;
        char pat[128];
        int k = 0;
        while (*s && *s != '?') {
            if (k >= sizeof(pat) - 1) { while (*s && *s != '?') s++; break; }
            if (*s == '\\' && *(s+1) == '?') { s++; pat[k++] = '?'; s++; }
            else pat[k++] = *s++;
        }
        pat[k] = '\0';
        // If pattern is empty, use last_re
        if (pat[0] == '\0')
            my_strcpy(pat, last_re);
        if (*s == '?') s++;
        my_strcpy(last_re, pat);
        int start = (current_line < 0) ? total_lines - 1 : current_line - 1;
        int found = -1;
        for (int i = start; i >= 0; i--) {
            if (match(pat, buffer[i])) { found = i; break; }
        }
        if (found < 0) {
            for (int i = total_lines - 1; i > start; i--) {
                if (match(pat, buffer[i])) { found = i; break; }
            }
        }
        if (found < 0) {
            error("no match");
            return -1;
        }
        *addr = found;
        current_line = found;
        // Print the matched line
        printf("%s\n", buffer[found]);
    } else if (*s >= '0' && *s <= '9') {
        int n = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (*s - '0');
            s++;
        }
        if (n == 0) {
            *addr = -1; // line 0: before first line
        } else {
            if (n > total_lines) {
                error("out of range");
                return -1;
            }
            *addr = n - 1;
        }
    } else {
        return -1;
    }

    *p = s;
    return 0;
}

// Parse command line: [addr[,addr]] command [args]
// Stores parsed values, returns the command character (0 if no command).
char parse_cmd(char *buf, int *addr1, int *addr2, char *args) {
    char *p = buf;
    args[0] = '\0';
    *addr1 = -1;
    *addr2 = -1;
    int have_addr = 0;
    int comma_seen = 0;

    p = skip_ws(p);

    // Handle % as special address meaning 1,$
    if (*p == '%') {
        *addr1 = 0;
        *addr2 = total_lines > 0 ? total_lines - 1 : 0;
        have_addr = 1;
        comma_seen = 1;
        p = skip_ws(p + 1);
        goto got_cmd;
    }

    // Handle bare ',' => 1,$  and  ',<addr>' => 1,<addr>
    // Handle bare ';' => .,$  and  ';<addr>' => .,<addr>
    // These must come before parse_one_addr, which doesn't recognize , or ;
    if (*p == ',') {
        *addr1 = 0;
        have_addr = 1;
        comma_seen = 1;
        p = skip_ws(p + 1);
        if (parse_one_addr(&p, addr2) != 0) {
            // No second address given: default to $
            *addr2 = total_lines > 0 ? total_lines - 1 : 0;
        }
        p = skip_ws(p);
        goto got_cmd;
    }
    if (*p == ';') {
        *addr1 = current_line >= 0 ? current_line : 0;
        have_addr = 1;
        comma_seen = 1;
        p = skip_ws(p + 1);
        if (parse_one_addr(&p, addr2) != 0) {
            // No second address given: default to $
            *addr2 = total_lines > 0 ? total_lines - 1 : 0;
        }
        p = skip_ws(p);
        goto got_cmd;
    }

    if (parse_one_addr(&p, addr1) == 0) {
        have_addr = 1;
        p = skip_ws(p);
        if (*p == ',') {
            comma_seen = 1;
            p = skip_ws(p + 1);
            if (parse_one_addr(&p, addr2) == 0) {
                p = skip_ws(p);
            } else if (total_lines > 0) {
                *addr2 = total_lines - 1;
            } else {
                *addr2 = 0;
            }
        } else if (*p == ';') {
            comma_seen = 1;
            if (*addr1 >= 0 && *addr1 < total_lines)
                current_line = *addr1;
            p = skip_ws(p + 1);
            if (parse_one_addr(&p, addr2) == 0) {
                p = skip_ws(p);
            } else if (total_lines > 0) {
                *addr2 = total_lines - 1;
            } else {
                *addr2 = 0;
            }
        }
    }

got_cmd:
    p = skip_ws(p);
    pending_report = 0;  // reset
    char cmd = *p;
    if (cmd) {
        p++;
        // Check for l/n/p suffix
        char suffix = 0;
        while (*p == 'l' || *p == 'n' || *p == 'p') {
            suffix = *p;
            p++;
        }
        if (suffix) {
            // Command suffix: remember it for printing after command execution
            pending_report = 1;
        }

        // Skip leading whitespace before collecting args
        p = skip_ws(p);
        int i = 0;
        while (*p && i < MAX_CMD - 1) {
            args[i++] = *p++;
        }
        args[i] = '\0';
        // Trim trailing whitespace
        while (i > 0 && (args[i-1] == ' ' || args[i-1] == '\t'))
            args[--i] = '\0';
    }

    // Default addresses
    if (cmd == '\0') {
        if (have_addr) {
            cmd = 'p';
            if (!comma_seen && *addr2 < 0)
                *addr2 = *addr1;
        }
    }

    // Set default ranges for commands
    if (cmd == 'p' || cmd == 'l' || cmd == 'n' || cmd == 'd' ||
        cmd == 's' || cmd == '&' || cmd == '~') {
        if (!have_addr) {
            *addr1 = current_line;
            *addr2 = current_line;
        } else if (have_addr && !comma_seen) {
            *addr2 = *addr1;
        }
    } else if (cmd == 'j') {
        if (!have_addr) {
            *addr1 = current_line;
            *addr2 = (current_line < total_lines - 1) ? current_line + 1 : current_line;
        } else if (have_addr && !comma_seen) {
            *addr2 = (*addr1 < total_lines - 1) ? *addr1 + 1 : *addr1;
        }
    } else if (cmd == 'c' || cmd == 'm' || cmd == 't') {
        if (!have_addr) {
            *addr1 = current_line;
            *addr2 = current_line;
        } else if (have_addr && !comma_seen) {
            *addr2 = *addr1;
        }
    } else if (cmd == 'a' || cmd == 'i') {
        if (!have_addr)
            *addr1 = current_line;
    } else if (cmd == 'r') {
        if (!have_addr)
            *addr1 = current_line;
    } else if (cmd == '=') {
        if (!have_addr)
            *addr1 = current_line;
    } else if (cmd == 'g' || cmd == 'v') {
        if (*addr1 < 0) *addr1 = 0;
        if (*addr2 < 0) *addr2 = total_lines - 1;
    }

    return cmd;
}

// ---- Main ----

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: ed <filename>\n");
        exit(1);
    }

    // Copy filename
    int i;
    for (i = 0; argv[1][i] && i < MAX_FILENAME - 1; i++)
        filename[i] = argv[1][i];
    filename[i] = '\0';

    // Initialize globals
    last_error[0] = '\0';
    last_re[0] = '\0';
    last_sub_pat[0] = '\0';
    last_sub_rep[0] = '\0';
    last_sub_delim = '/';
    last_shell_cmd[0] = '\0';
    undo_avail = 0;
    for (int i = 0; i < 26; i++) marks[i] = -1;

    // Load file
    load_file(filename);
    modified = 0;

    char cmd_buf[MAX_CMD];
    while (1) {
        if (show_prompt)
            printf("*");

        int n = get_line(cmd_buf, MAX_CMD);
        if (n <= 0) {
            // EOF: if modified, refuse with ?
            if (modified) {
                error("warning: file modified");
                continue;
            }
            break;
        }

        char *p = skip_ws(cmd_buf);
        if (*p == '\0') {
            // Empty line: advance one line and print (if possible)
            if (current_line < 0 || current_line >= total_lines - 1) {
                error("end of buffer");
                continue;
            }
            current_line++;
            printf("%s\n", buffer[current_line]);
            continue;
        }

        if (*p == '#')
            continue;

        int addr1, addr2;
        char args[MAX_CMD];
        char cmd = parse_cmd(p, &addr1, &addr2, args);

        if (cmd == '\0') {
            error("unknown command");
            continue;
        }

        // Validate range addresses for commands that need them
        int needs_range = (cmd == 'p' || cmd == 'n' || cmd == 'l' || cmd == 'd' ||
                           cmd == 's' || cmd == '&' || cmd == '~' || cmd == 'c' ||
                           cmd == 'm' || cmd == 't' || cmd == 'j');

        if (needs_range && (addr1 < 0 || addr2 < 0 || addr1 >= total_lines || addr2 >= total_lines)) {
            error("out of range");
            continue;
        }

        // Dispatch
        if (cmd == 'q') {
            if (modified) {
                error("warning: file modified");
                continue;
            }
            break;
        } else if (cmd == 'Q') {
            break;
        } else if (cmd == 'p') {
            do_print(addr1, addr2);
        } else if (cmd == 'n') {
            do_number(addr1, addr2);
        } else if (cmd == 'l') {
            do_list(addr1, addr2);
        } else if (cmd == '=') {
            if (addr1 < 0) {
                if (total_lines == 0)
                    printf("0\n");
            } else if (addr1 >= total_lines) {
                error("out of range");
            } else {
                do_equal(addr1);
            }
        } else if (cmd == 'd') {
            do_delete(addr1, addr2);
        } else if (cmd == 'a') {
            int pt = (addr1 < 0) ? current_line : addr1;
            if (pt < -1) pt = -1;
            if (pt >= total_lines) pt = total_lines - 1;
            do_append(pt);
        } else if (cmd == 'i') {
            int pt = (addr1 < 0) ? current_line : addr1;
            if (pt < 0) pt = 0;
            if (pt >= total_lines) pt = total_lines - 1;
            do_insert(pt);
        } else if (cmd == 'c') {
            do_change(addr1, addr2);
        } else if (cmd == 'j') {
            if (addr1 == addr2) {
                error("out of range");
                continue;
            }
            do_join(addr1, addr2);
        } else if (cmd == 'm') {
            char *ap = args;
            int dest_addr;
            if (parse_one_addr(&ap, &dest_addr) != 0) {
                error("invalid destination address");
                continue;
            }
            if (dest_addr < 0) dest_addr = -1;
            if (dest_addr >= total_lines) dest_addr = total_lines - 1;
            do_move(addr1, addr2, dest_addr);
        } else if (cmd == 't') {
            char *ap = args;
            int dest_addr;
            if (parse_one_addr(&ap, &dest_addr) != 0) {
                error("invalid destination address");
                continue;
            }
            if (dest_addr < 0) dest_addr = -1;
            if (dest_addr >= total_lines) dest_addr = total_lines - 1;
            do_copy(addr1, addr2, dest_addr);
        } else if (cmd == 's') {
            char *ap = args;
            if (*ap == '\0') {
                // Just "s" with no args: re-use last substitution
                if (last_sub_pat[0] == '\0') {
                    error("no previous substitution");
                    continue;
                }
                do_substitute(addr1, addr2, last_sub_pat, last_sub_rep, last_sub_gflag);
                // If a command suffix (p/l/n) was consumed by parse_cmd, print
                if (pending_report && current_line >= 0)
                    printf("%s\n", buffer[current_line]);
                continue;
            }
            // Check if args is just flags (g/G/p/l/n/digits) for repeat-last-sub
            int just_flags = 1;
            char *fp = ap;
            while (*fp) {
                if (*fp != 'g' && *fp != 'G' && *fp != 'p' && *fp != 'l' && *fp != 'n' &&
                    !(*fp >= '0' && *fp <= '9')) {
                    just_flags = 0;
                    break;
                }
                fp++;
            }
            if (just_flags) {
                // Re-use last sub with new flags
                if (last_sub_pat[0] == '\0') {
                    error("no previous substitution");
                    continue;
                }
                int gflag = 0;
                int pflag = 0, lflag = 0, nflag = 0;
                while (*ap) {
                    if (*ap == 'g' || *ap == 'G') gflag = 1;
                    else if (*ap == 'p') pflag = 1;
                    else if (*ap == 'l') lflag = 1;
                    else if (*ap == 'n') nflag = 1;
                    ap++;
                }
                do_substitute(addr1, addr2, last_sub_pat, last_sub_rep, gflag);
                if (pflag && current_line >= 0)
                    printf("%s\n", buffer[current_line]);
                else if (lflag && current_line >= 0)
                    do_list(current_line, current_line);
                else if (nflag && current_line >= 0)
                    do_number(current_line, current_line);
                continue;
            }
            char delim = *ap;
            if (delim == ' ' || delim == '\t') {
                error("invalid delimiter");
                continue;
            }
            ap++;
            char pat[128];
            char rep[128];
            int k = 0;
            while (*ap && *ap != delim) {
                if (k >= sizeof(pat) - 1) { while (*ap && *ap != delim) ap++; break; }
                if (*ap == '\\' && *(ap+1) == delim) { ap++; pat[k++] = delim; ap++; }
                else if (*ap == '\\' && *(ap+1) == '\\') { ap++; pat[k++] = '\\'; ap++; }
                else pat[k++] = *ap++;
            }
            pat[k] = '\0';
            // If pattern is empty, use the last regex pattern
            if (pat[0] == '\0')
                my_strcpy(pat, last_re);
            if (*ap == delim) ap++; else { error("unterminated pattern"); continue; }
            k = 0;
            while (*ap && *ap != delim && *ap != 'g' && *ap != 'G' &&
                   *ap != 'p' && *ap != 'l' && *ap != 'n' &&
                   !(*ap >= '0' && *ap <= '9')) {
                if (k >= sizeof(rep) - 1) break;
                if (*ap == '\\' && *(ap+1) == delim) { ap++; rep[k++] = delim; ap++; }
                else if (*ap == '\\' && *(ap+1) == '\\') { ap++; rep[k++] = '\\'; ap++; }
                else rep[k++] = *ap++;
            }
            rep[k] = '\0';
            int gflag = 0;
            int pflag = 0;
            int lflag = 0;
            int nflag = 0;
            while (*ap) {
                if (*ap == 'g' || *ap == 'G') gflag = 1;
                else if (*ap == 'p') pflag = 1;
                else if (*ap == 'l') lflag = 1;
                else if (*ap == 'n') nflag = 1;
                ap++;
            }
            my_strcpy(last_sub_pat, pat);
            my_strcpy(last_sub_rep, rep);
            last_sub_gflag = gflag;
            last_sub_delim = delim;
            // Save the flags for pending report
            int saved_report = pending_report;
            if (pflag || lflag || nflag)
                pending_report = 1;
            do_substitute(addr1, addr2, pat, rep, gflag);
            // If p/l/n suffix flag was given, print the current line
            if (pflag && current_line >= 0)
                printf("%s\n", buffer[current_line]);
            else if (lflag && current_line >= 0)
                do_list(current_line, current_line);
            else if (nflag && current_line >= 0)
                do_number(current_line, current_line);
            pending_report = saved_report;
            // If a command suffix (p/l/n) was consumed by parse_cmd, print as p
            if (saved_report && current_line >= 0)
                printf("%s\n", buffer[current_line]);
        } else if (cmd == '&' || cmd == '~') {
            if (last_sub_pat[0] == '\0') {
                error("no previous substitution");
                continue;
            }
            do_substitute(addr1, addr2, last_sub_pat, last_sub_rep, last_sub_gflag);
        } else if (cmd == 'g') {
            char *ap = skip_ws(args);
            if (*ap != '/') { error("invalid global pattern"); continue; }
            ap++;
            char pat[128];
            int k = 0;
            while (*ap && *ap != '/') {
                if (k >= sizeof(pat) - 1) { while (*ap && *ap != '/') ap++; break; }
                if (*ap == '\\' && *(ap+1) == '/') { ap++; pat[k++] = '/'; ap++; }
                else pat[k++] = *ap++;
            }
            pat[k] = '\0';
            if (pat[0] == '\0') my_strcpy(pat, last_re);
            my_strcpy(last_re, pat);
            if (*ap == '/') {
                ap++;
                char cmd2[MAX_CMD];
                int j = 0;
                while (*ap && j < MAX_CMD - 1) cmd2[j++] = *ap++;
                cmd2[j] = '\0';
                do_global(pat, cmd2, 0);
            } else {
                error("unterminated pattern");
            }
        } else if (cmd == 'v') {
            char *ap = skip_ws(args);
            if (*ap != '/') { error("invalid global pattern"); continue; }
            ap++;
            char pat[128];
            int k = 0;
            while (*ap && *ap != '/') {
                if (k >= sizeof(pat) - 1) { while (*ap && *ap != '/') ap++; break; }
                if (*ap == '\\' && *(ap+1) == '/') { ap++; pat[k++] = '/'; ap++; }
                else pat[k++] = *ap++;
            }
            pat[k] = '\0';
            if (pat[0] == '\0') my_strcpy(pat, last_re);
            my_strcpy(last_re, pat);
            if (*ap == '/') {
                ap++;
                char cmd2[MAX_CMD];
                int j = 0;
                while (*ap && j < MAX_CMD - 1) cmd2[j++] = *ap++;
                cmd2[j] = '\0';
                do_global(pat, cmd2, 1);
            } else {
                error("unterminated pattern");
            }
        } else if (cmd == 'w') {
            char *name = skip_ws(args);
            if (*name == '\0') {
                if (filename[0] == '\0') {
                    error("no filename");
                    continue;
                }
                name = filename;
            } else {
                if (filename[0] == '\0') {
                    int j;
                    for (j = 0; name[j] && j < MAX_FILENAME - 1; j++)
                        filename[j] = name[j];
                    filename[j] = '\0';
                }
            }
            save_file(name);
        } else if (cmd == 'W') {
            char *name = skip_ws(args);
            if (*name == '\0') {
                if (filename[0] == '\0') {
                    error("no filename");
                    continue;
                }
                name = filename;
            }
            save_file_append(name);
        } else if (cmd == 'r') {
            char *name = skip_ws(args);
            if (*name == '\0') {
                if (filename[0] == '\0') {
                    error("no filename");
                    continue;
                }
                name = filename;
            }
            int pt = (addr1 < 0) ? ((total_lines > 0) ? total_lines - 1 : -1) : addr1;
            if (pt < -1) pt = -1;
            if (pt >= total_lines) pt = total_lines - 1;
            do_read(name, pt);
        } else if (cmd == 'e' || cmd == 'E') {
            char *name = skip_ws(args);
            if (*name == '\0') {
                if (filename[0] == '\0') {
                    error("no filename");
                    continue;
                }
                name = filename;
            }
            if (cmd == 'e' && modified) {
                error("warning: file modified");
                continue;
            }
            do_edit(name);
        } else if (cmd == 'f') {
            char *name = skip_ws(args);
            if (*name == '\0') {
                if (filename[0] == '\0')
                    error("no filename");
                else
                    printf("%s\n", filename);
            } else {
                int j;
                for (j = 0; name[j] && j < MAX_FILENAME - 1; j++)
                    filename[j] = name[j];
                filename[j] = '\0';
            }
        } else if (cmd == 'h') {
            if (last_error[0] != '\0')
                printf("%s\n", last_error);
            else
                printf("?\n");
        } else if (cmd == 'H') {
            verbose_errors = !verbose_errors;
        } else if (cmd == 'P') {
            show_prompt = !show_prompt;
        } else if (cmd == 'u') {
            do_undo();
        } else if (cmd == 'k') {
            char *ap = skip_ws(args);
            if (*ap >= 'a' && *ap <= 'z' && *(ap+1) == '\0')
                do_mark(*ap);
            else
                error("invalid mark character");
        } else if (cmd == '!') {
            char *cmd_str = skip_ws(args);
            if (*cmd_str == '\0') {
                // Just "!" - repeat last shell command
                if (last_shell_cmd[0] == '\0') {
                    printf("!\n");
                    continue;
                }
                cmd_str = last_shell_cmd;
                printf("%s\n", cmd_str);
            } else if (*cmd_str == '!' && *(cmd_str+1) == '\0') {
                // "!!" - repeat last shell command
                if (last_shell_cmd[0] == '\0') {
                    printf("!\n");
                    continue;
                }
                cmd_str = last_shell_cmd;
                printf("%s\n", cmd_str);
            } else {
                // Save for later !!
                my_strncpy(last_shell_cmd, cmd_str, MAX_CMD);
            }
            // Try to exec the command via sh
            int pid = fork();
            if (pid < 0) {
                error("fork failed");
                continue;
            }
            if (pid == 0) {
                // Build argv for sh -c cmd_str
                char *sh_argv[] = { "sh", "-c", cmd_str, 0 };
                exec("/sh", sh_argv);
                exec("sh", sh_argv);
                // If sh is not found in common places
                printf("!\n");
                exit(1);
            }
            wait(0);
            printf("!\n");
        } else {
            error("unknown command");
        }
    }

    exit(0);
}

#include "replay/replay.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* replay <file.log> [--json] — prints a summary of the decoded records (spec §22.2). All logic
 * lives in replaylib; session 2.7 adds the engine run behind the same CLI. */

static int usage(const char *argv0)
{
    fprintf(stderr, "usage: %s <file.log> [--json]\n", argv0);
    return 2;
}

int main(int argc, char **argv)
{
    const char *self = (argc > 0 && argv[0]) ? argv[0] : "replay";
    const char *path = NULL;
    int json = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            if (json) return usage(self);
            json = 1;
        } else if (argv[i][0] == '-' || path) {
            return usage(self);
        } else {
            path = argv[i];
        }
    }
    if (!path) return usage(self);

    replay_summary_t s;
    errno = 0;
    if (replay_summarize_file(path, &s) != 0) {
        fprintf(stderr, "%s: %s: %s\n", self, path, strerror(errno));
        return 1;
    }
    if (json) replay_print_json(&s, stdout);
    else replay_print_text(&s, stdout);
    return 0;
}

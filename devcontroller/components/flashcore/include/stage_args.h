/* devcontroller/components/flashcore/include/stage_args.h -- stage_args: a PURE parser for the
 * console `flash stage <size> <sha256hex>` argument pair (Plan 5.6, dev-kit as primary interface,
 * Task 7). Shared by the future `flash stage` console command and any host test that wants to
 * exercise the argument grammar without a real console. IDF-free so
 * devcontroller/test/test_stage_args.c links it directly on the host.
 */
#ifndef STAGE_ARGS_H
#define STAGE_ARGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parses argv[0..argc) as "<size> <64 hex>" (argv[0] is the first positional argument -- the
 * caller strips the command word and any flags such as --json before calling). On success
 * (return 0) *size holds the decimal size and sha[0..32) the decoded digest. Returns:
 *   -1  argc != 2 (wrong number of positional arguments)
 *   -2  <size> is not a valid, fully-consumed decimal integer in (0, 0xFFFFFFFF]
 *   -3  <sha> is not exactly 64 hex characters (upper or lower case accepted)
 */
int stage_args_parse(int argc, char **argv, uint32_t *size, uint8_t sha[32]);

#ifdef __cplusplus
}
#endif

#endif /* STAGE_ARGS_H */

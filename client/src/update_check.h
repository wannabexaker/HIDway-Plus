#ifndef HIDWAY_UPDATE_CHECK_H
#define HIDWAY_UPDATE_CHECK_H
/* One read-only update check per start. The HTTP, JSON and release checks
 * stay in the PowerShell updater module: the client runs
 * hidway-updates.ps1 -Check from its own folder on a worker thread and reads
 * the single result line. Nothing here downloads, installs or arms. */
#include <windows.h>
#include <stddef.h>

/* wParam of the posted result message */
enum { HW_UPDATE_SILENT = 0, /* up to date, no release, older, preview: say nothing */
       HW_UPDATE_AVAILABLE = 1, /* a newer stable release; hw_update_check_tag() names it */
       HW_UPDATE_FAILED = 2 };  /* error, rate limit, timeout, garbage or no script */

#define HW_UPDATE_TAG_MAX 24 /* "v" + three numbers of at most 6 digits + dots + NUL */
#define HW_UPDATE_TIMEOUT_MS 20000u
#define HW_UPDATE_OUTPUT_MAX 512u

/* 1 when `tag` is v<digits>.<digits>.<digits>, each number 1 to 6 digits. */
int hw_update_tag_valid(const char *tag);

/* Strictly parse the whole output of hidway-updates.ps1 -Check: exactly one
 * line "HIDWAY-UPDATE state=<State> tag=<tag or empty>", optionally ended by
 * CRLF or LF. Returns HW_UPDATE_AVAILABLE (and copies the tag) only for
 * state=Available with a valid tag; HW_UPDATE_SILENT for the module's other
 * well-formed answers (Current, None, Older, Preview); HW_UPDATE_FAILED for
 * Error, RateLimited and everything malformed. */
int hw_update_parse(const char *output, size_t length, char *tag, size_t tag_cap);

/* 1 when hidway-updates.ps1 and hidway-updater.psm1 are beside this
 * executable (regular files, no reparse points). */
int hw_update_scripts_present(void);

/* Starts the check on a worker thread when the scripts are present. When it
 * ends (at most about HW_UPDATE_TIMEOUT_MS later; the child is killed then)
 * it posts `message` to `window` with wParam HW_UPDATE_*. Returns 1 when the
 * worker was started; at most one check runs per process. */
int hw_update_check_begin(HWND window, UINT message);

/* The tag of an HW_UPDATE_AVAILABLE result; valid on the GUI thread after
 * the message arrived. */
const char *hw_update_check_tag(void);
#endif

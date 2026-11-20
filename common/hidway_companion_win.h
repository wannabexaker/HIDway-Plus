#ifndef HIDWAY_COMPANION_WIN_H
#define HIDWAY_COMPANION_WIN_H
#include <windows.h>
enum { HW_COMPANION_CLIENT, HW_COMPANION_MONITOR, HW_COMPANION_UPDATES, HW_COMPANION_SETUP };
enum { HW_COMPANION_OK, HW_COMPANION_MISSING, HW_COMPANION_OTHER_COPY, HW_COMPANION_FAILED };
/* Opens only the selected sibling from this executable's directory. Existing
 * instances must belong to that same installed pair. Never arms or elevates. */
int hw_companion_open(HWND owner,int target);
const wchar_t *hw_companion_error(int result);
#endif

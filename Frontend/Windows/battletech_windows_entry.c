#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <stddef.h>
#include <wchar.h>

#include "battletech_game_entry.h"

int WINAPI battletech_launcher_main(HINSTANCE instance, HINSTANCE previous,
                                    LPWSTR command_line, int show);

static void free_game_arguments(char **arguments, int count) {
    int index;

    if (!arguments) {
        return;
    }
    for (index = 0; index < count; ++index) {
        if (arguments[index]) {
            HeapFree(GetProcessHeap(), 0, arguments[index]);
        }
    }
    HeapFree(GetProcessHeap(), 0, arguments);
}

static int run_game_mode(wchar_t **wide_arguments, int count) {
    char **arguments;
    int index;
    int result = 2;

    arguments = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                   (size_t)(count + 1) * sizeof(*arguments));
    if (!arguments) {
        return result;
    }

    for (index = 0; index < count; ++index) {
        BOOL replaced = FALSE;
        int bytes = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                        wide_arguments[index], -1, NULL, 0,
                                        NULL, &replaced);
        if (bytes <= 0 || replaced) {
            goto done;
        }
        arguments[index] = (char *)HeapAlloc(GetProcessHeap(), 0,
                                             (size_t)bytes);
        if (!arguments[index]) {
            goto done;
        }
        replaced = FALSE;
        if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                 wide_arguments[index], -1, arguments[index],
                                 bytes, NULL, &replaced) || replaced) {
            goto done;
        }
    }

    result = battletech_game_main(count, arguments);

done:
    free_game_arguments(arguments, count);
    return result;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    LPWSTR command_line, int show) {
    wchar_t **arguments;
    int count = 0;
    int result;

    arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) {
        return 2;
    }
    if (count >= 2 && wcscmp(arguments[1], L"--game") == 0) {
        result = run_game_mode(arguments, count);
        LocalFree(arguments);
        return result;
    }
    LocalFree(arguments);
    return battletech_launcher_main(instance, previous, command_line, show);
}

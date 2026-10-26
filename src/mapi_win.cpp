// VitaParms – Simple-MAPI-Aufruf für Windows (ohne Qt, ohne mapi.h)
// Öffnet im Standard-Mailprogramm eine neue Nachricht mit Anhang.
// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include <windows.h>

namespace {
// Strukturen nach Microsoft-Dokumentation (mapi.h, Unicode-Variante)
struct VpMapiRecipDescW {
    ULONG  ulReserved;
    ULONG  ulRecipClass;
    PWSTR  lpszName;
    PWSTR  lpszAddress;
    ULONG  ulEIDSize;
    LPVOID lpEntryID;
};
struct VpMapiFileDescW {
    ULONG  ulReserved;
    ULONG  flFlags;
    ULONG  nPosition;
    PWSTR  lpszPathName;
    PWSTR  lpszFileName;
    PVOID  lpFileType;
};
struct VpMapiMessageW {
    ULONG  ulReserved;
    PWSTR  lpszSubject;
    PWSTR  lpszNoteText;
    PWSTR  lpszMessageType;
    PWSTR  lpszDateReceived;
    PWSTR  lpszConversationID;
    ULONG  flFlags;
    VpMapiRecipDescW *lpOriginator;
    ULONG  nRecipCount;
    VpMapiRecipDescW *lpRecips;
    ULONG  nFileCount;
    VpMapiFileDescW *lpFiles;
};
typedef ULONG (WINAPI *MapiSendMailW)(ULONG_PTR, ULONG_PTR, VpMapiMessageW *, ULONG, ULONG);

const ULONG kMapiTo        = 1;
const ULONG kMapiLogonUi   = 0x00000001;
const ULONG kMapiDialog    = 0x00000008;
} // namespace

// Rückgabe: 0 = Erfolg, 1 = vom Benutzer abgebrochen, sonst MAPI-Fehlercode;
//          -1 = kein MAPI-fähiges Mailprogramm gefunden
int vpMapiSendMail(void *hwnd, wchar_t *to, wchar_t *subject, wchar_t *body,
                   wchar_t *path, wchar_t *fileName)
{
    HMODULE lib = LoadLibraryW(L"mapi32.dll");
    if (!lib)
        return -1;
    auto send = reinterpret_cast<MapiSendMailW>(reinterpret_cast<void *>(GetProcAddress(lib, "MAPISendMailW")));
    if (!send) {
        FreeLibrary(lib);
        return -1;
    }
    VpMapiRecipDescW recip = {};
    recip.ulRecipClass = kMapiTo;
    recip.lpszName = to;
    VpMapiFileDescW file = {};
    file.nPosition = static_cast<ULONG>(-1);
    file.lpszPathName = path;
    file.lpszFileName = fileName;
    VpMapiMessageW msg = {};
    msg.lpszSubject = subject;
    msg.lpszNoteText = body;
    msg.nRecipCount = (to && *to) ? 1 : 0;
    msg.lpRecips = msg.nRecipCount ? &recip : nullptr;
    msg.nFileCount = 1;
    msg.lpFiles = &file;
    const ULONG rc = send(0, reinterpret_cast<ULONG_PTR>(hwnd), &msg, kMapiLogonUi | kMapiDialog, 0);
    FreeLibrary(lib);
    return static_cast<int>(rc);
}
#endif

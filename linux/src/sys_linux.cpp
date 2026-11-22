// Linux side of sys.h (POSIX + SDL). Paths are converted to UTF-8 for the C library.
//
// Folders: settings, save states and screenshots go to $XDG_DATA_HOME/mickey-donald-collection
// (~/.local/share/...). ROMs are searched there (and in its "roms" folder), next to the AppImage (or the
// executable, for a plain build) and in the working directory, each with its "roms" subfolder.
#include "app/sys.h"
#include <SDL.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>

std::string to_utf8(const std::wstring& w) {
    std::string s;
    for (wchar_t wc : w) {
        uint32_t c = (uint32_t)wc;
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
        else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 0x3F)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}

std::wstring from_utf8(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
        if (n < 0) { w += (wchar_t)0xFFFD; i++; continue; }
        uint32_t cp = n ? c & (0x3Fu >> n) : c;
        size_t j = i + 1;
        for (int k = 0; k < n && j < s.size(); k++, j++) cp = (cp << 6) | ((unsigned char)s[j] & 0x3F);
        w += (wchar_t)cp;
        i = j;
    }
    return w;
}

std::wstring path_join(const std::wstring& dir, const std::wstring& name) { return dir + L"/" + name; }

std::wstring file_name(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? path : path.substr(s + 1);
}

FILE* open_file(const std::wstring& path, const char* mode) { return fopen(to_utf8(path).c_str(), mode); }

bool read_file(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = open_file(path, "rb");
    if (!f) return false;
    uint64_t n = file_size(f);
    out.resize((size_t)n);
    bool ok = n == 0 || fread(out.data(), 1, (size_t)n, f) == n;
    fclose(f);
    return ok;
}

bool read_text(const std::wstring& path, std::string& out) {
    std::vector<uint8_t> d;
    if (!read_file(path, d)) return false;
    out.assign(d.begin(), d.end());
    return true;
}

bool write_file(const std::wstring& path, const void* data, size_t n) {
    // write a temporary file and rename it over the old one, so a crash mid-write never leaves half a file
    const std::wstring tmp = path + L".tmp";
    FILE* f = open_file(tmp, "wb");
    if (!f) return false;
    bool ok = n == 0 || fwrite(data, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    return ok && rename(to_utf8(tmp).c_str(), to_utf8(path).c_str()) == 0;
}

bool file_seek(FILE* f, uint64_t pos) { return fseeko(f, (off_t)pos, SEEK_SET) == 0; }

uint64_t file_size(FILE* f) {
    off_t cur = ftello(f);
    fseeko(f, 0, SEEK_END);
    off_t n = ftello(f);
    fseeko(f, cur, SEEK_SET);
    return n < 0 ? 0 : (uint64_t)n;
}

void make_dir(const std::wstring& path) { mkdir(to_utf8(path).c_str(), 0755); }

bool list_dir(const std::wstring& dir, std::vector<DirEntry>& out) {
    out.clear();
    const std::string d = to_utf8(dir);
    DIR* h = opendir(d.c_str());
    if (!h) return false;
    while (dirent* e = readdir(h)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        struct stat st;
        if (stat((d + "/" + e->d_name).c_str(), &st) != 0) continue;
        DirEntry de;
        de.name = from_utf8(e->d_name);
        de.size = (uint64_t)st.st_size;
        de.mtime = (uint64_t)st.st_mtime * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec;
        de.is_dir = S_ISDIR(st.st_mode);
        out.push_back(std::move(de));
    }
    closedir(h);
    return true;
}

// The folder of the running executable (inside an AppImage: its mounted usr/bin, read-only).
std::wstring exe_dir() {
    static std::wstring dir;
    if (dir.empty()) {
        char buf[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        std::string p = n > 0 ? std::string(buf, (size_t)n) : std::string(".");
        size_t s = p.find_last_of('/');
        dir = from_utf8(s == std::string::npos ? std::string(".") : p.substr(0, s));
    }
    return dir;
}

namespace {
void make_dirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

// Where the player sees the game: the AppImage file's folder, else the executable's.
std::wstring home_of_game() {
    if (const char* ai = getenv("APPIMAGE")) {
        std::string p = ai;
        size_t s = p.find_last_of('/');
        if (s != std::string::npos) return from_utf8(p.substr(0, s));
    }
    return exe_dir();
}
}  // namespace

std::wstring data_dir() {
    static std::wstring dir;
    if (dir.empty()) {
        std::string base;
        if (const char* x = getenv("XDG_DATA_HOME"); x && *x == '/') base = x;
        else if (const char* h = getenv("HOME"); h && *h) base = std::string(h) + "/.local/share";
        else base = "/tmp";
        base += "/mickey-donald-collection";
        make_dirs(base);
        dir = from_utf8(base);
    }
    return dir;
}

std::vector<std::wstring> rom_dirs() {
    std::vector<std::wstring> dirs = {path_join(data_dir(), L"roms"), data_dir(), home_of_game(),
                                      path_join(home_of_game(), L"roms")};
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd)) {
        dirs.push_back(from_utf8(cwd));
        dirs.push_back(path_join(from_utf8(cwd), L"roms"));
    }
    std::vector<std::wstring> out;
    for (const auto& d : dirs) {
        bool dup = false;
        for (const auto& o : out) dup = dup || o == d;
        if (!dup) out.push_back(d);
    }
    return out;
}

std::wstring export_dir() { return data_dir(); }

// (xdg-open: a web page in the browser, a folder in the file manager)
void open_url(const char* url) { SDL_OpenURL(url); }

// Through the curl command (present on practically every desktop), so the build needs no libcurl.
bool http_get(const char* url, std::string& out) {
    out.clear();
    std::string quoted = "'";
    for (const char* p = url; *p; p++) quoted += *p == '\'' ? std::string("'\\''") : std::string(1, *p);
    quoted += "'";
    const std::string cmd = "curl -fsSL --max-time 8 -H 'User-Agent: MickeyDonaldCollection' " + quoted + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    const int status = pclose(p);
    if (status != 0) out.clear();
    return !out.empty();
}

std::string today_text() {
    time_t t = time(nullptr);
    tm lt;
    localtime_r(&t, &lt);
    char buf[16];
    strftime(buf, sizeof buf, "%Y-%m-%d", &lt);
    return buf;
}

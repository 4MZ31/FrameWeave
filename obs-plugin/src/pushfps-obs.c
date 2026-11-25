/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * PushFPS for OBS Studio
 *
 * Adds a "PushFPS" source to OBS. The source shows PushFPS's output (the console
 * picture after frame generation and AI enhancement) in any scene, at the rate
 * PushFPS presents it, and can start PushFPS for you.
 *
 * How it works: PushFPS does the processing on the GPU in its own process. This
 * source drives one of OBS's own capture sources as a private child, pointed at the
 * PushFPS window and configured for it:
 *   - Game capture (default): hooks the D3D12 video swapchain, so OBS receives the
 *     generated frames at full rate. PushFPS is started with --obs-game-capture,
 *     which draws its interface in software so the video is the only swapchain.
 *   - Window capture (fallback): Windows Graphics Capture of the PushFPS window,
 *     for systems where game capture is blocked.
 * The window is found by its executable, so it follows PushFPS when it is started
 * later, restarted or renamed in its title bar.
 *
 * Copyright (C) 2026 PushFPS contributors. GPL-3.0-or-later, like PushFPS and OBS.
 */
#include <obs-module.h>
#include <obs-frontend-api.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

OBS_DECLARE_MODULE()

#define PLUGIN_NAME "pushfps-obs"
#define PLUGIN_VERSION "1.2.0"
#define SOURCE_ID "pushfps_source"
#define PUSHFPS_EXE L"FrameWeave.exe"
#define DEFAULT_PATH L"%LOCALAPPDATA%\\Programs\\FrameWeave\\FrameWeave.exe"
#define LOCAL_KEY L"Software\\FrameWeave\\OBS"
#define LOCAL_EXE L"Executable"
#define EXE_PATH_CAP (MAX_PATH * 4)
#define SCAN_INTERVAL 1.5f

#define plog(level, fmt, ...) blog(level, "[" PLUGIN_NAME "] " fmt, ##__VA_ARGS__)

enum capture_method { METHOD_GAME = 0, METHOD_WINDOW = 1 };

struct pushfps_source {
	obs_source_t *self;
	CRITICAL_SECTION lock;
	obs_source_t *child; /* guarded by lock */
	int child_method;    /* method the child was made for, -1 = none */
	volatile LONG wanted_method;
	char window[1024]; /* encoded "title:class:exe" the child points at */
	bool autostart;
	float since_scan;
	bool logged_missing;
	HWND last_window; /* graphics thread: checked before a full window scan */
};

/* ---------------------------------------------------------------- helpers -- */

static void wide_to_utf8(const wchar_t *in, char *out, int out_len)
{
	out[0] = 0;
	if (in && *in)
		WideCharToMultiByte(CP_UTF8, 0, in, -1, out, out_len, NULL, NULL);
}

/* OBS's window string encoding (libobs/util/windows/window-helpers.c). */
static void encode_part(const char *in, char *out, size_t out_len)
{
	size_t o = 0;
	for (; *in && o + 4 < out_len; ++in) {
		if (*in == '#') {
			memcpy(out + o, "#22", 3);
			o += 3;
		} else if (*in == ':') {
			memcpy(out + o, "#3A", 3);
			o += 3;
		} else {
			out[o++] = *in;
		}
	}
	out[o] = 0;
}

static bool is_pushfps_process(DWORD pid)
{
	bool match = false;
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process)
		return false;
	wchar_t path[MAX_PATH * 2];
	DWORD len = (DWORD)(sizeof(path) / sizeof(path[0]));
	if (QueryFullProcessImageNameW(process, 0, path, &len)) {
		const wchar_t *name = wcsrchr(path, L'\\');
		name = name ? name + 1 : path;
		match = _wcsicmp(name, PUSHFPS_EXE) == 0;
	}
	CloseHandle(process);
	return match;
}

struct window_search {
	HWND best;
	LONGLONG best_area;
};

static BOOL CALLBACK find_window_proc(HWND hwnd, LPARAM param)
{
	struct window_search *search = (struct window_search *)param;
	if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != NULL)
		return TRUE;
	if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
		return TRUE;
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (!pid || !is_pushfps_process(pid))
		return TRUE;
	RECT rc;
	if (!GetClientRect(hwnd, &rc))
		return TRUE;
	const LONGLONG area = (LONGLONG)(rc.right - rc.left) * (LONGLONG)(rc.bottom - rc.top);
	if (!search->best || area > search->best_area) {
		search->best = hwnd;
		search->best_area = area;
	}
	return TRUE;
}

/* The PushFPS main window, or NULL when PushFPS is not running. */
static HWND find_pushfps_window(void)
{
	struct window_search search = {0};
	EnumWindows(find_window_proc, (LPARAM)&search);
	return search.best;
}

/* The PushFPS window found last time when it is still there; otherwise a full scan.
 * Called on the graphics thread every SCAN_INTERVAL: checking one window is far
 * cheaper than opening every top-level window's process. */
static HWND find_pushfps_window_cached(HWND *last)
{
	if (*last && IsWindow(*last) && IsWindowVisible(*last)) {
		DWORD pid = 0;
		GetWindowThreadProcessId(*last, &pid);
		if (pid && is_pushfps_process(pid))
			return *last;
	}
	*last = find_pushfps_window();
	return *last;
}

/* Builds the encoded window string OBS's capture sources understand. */
static bool window_string(HWND hwnd, char *out, size_t out_len)
{
	wchar_t wtitle[512], wclass[256];
	char title[1024], cls[512], enc_title[2048], enc_class[1024];
	if (!GetWindowTextW(hwnd, wtitle, 512))
		wtitle[0] = 0;
	if (!GetClassNameW(hwnd, wclass, 256))
		return false;
	wide_to_utf8(wtitle, title, sizeof(title));
	wide_to_utf8(wclass, cls, sizeof(cls));
	encode_part(title, enc_title, sizeof(enc_title));
	encode_part(cls, enc_class, sizeof(enc_class));
	return snprintf(out, out_len, "%s:%s:FrameWeave.exe", enc_title, enc_class) < (int)out_len;
}

/* A scene can request autostart, but only the local user chooses the installation. */
static bool installed_exe(const wchar_t *path)
{
	if (!((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
	    path[1] != L':' || path[2] != L'\\')
		return false;
	const wchar_t *name = wcsrchr(path, L'\\');
	if (_wcsicmp(name + 1, PUSHFPS_EXE) != 0)
		return false;
	wchar_t root[] = {path[0], L':', L'\\', 0};
	if (GetDriveTypeW(root) == DRIVE_REMOTE)
		return false;
	const DWORD attrs = GetFileAttributesW(path);
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool resolve_exe_path(wchar_t *out, DWORD capacity)
{
	DWORD bytes = capacity * sizeof(*out);
	const LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, LOCAL_KEY, LOCAL_EXE, RRF_RT_REG_SZ,
	                                    NULL, out, &bytes);
	if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
		const DWORD n = ExpandEnvironmentStringsW(DEFAULT_PATH, out, capacity);
		if (!n || n > capacity)
			return false;
	} else if (result != ERROR_SUCCESS) {
		return false;
	}
	return installed_exe(out);
}

static bool choose_installed_exe(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	UNUSED_PARAMETER(data);
	wchar_t path[EXE_PATH_CAP] = {0};
	OPENFILENAMEW dialog = {0};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = GetActiveWindow();
	dialog.lpstrTitle = L"Choose the local FrameWeave.exe for OBS";
	dialog.lpstrFilter = L"FrameWeave (FrameWeave.exe)\0FrameWeave.exe\0";
	dialog.lpstrFile = path;
	dialog.nMaxFile = EXE_PATH_CAP;
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&dialog))
		return false;
	if (!installed_exe(path)) {
		MessageBoxW(dialog.hwndOwner, L"Choose FrameWeave.exe on a local drive.", L"FrameWeave", MB_OK | MB_ICONWARNING);
		return false;
	}
	if (RegSetKeyValueW(HKEY_CURRENT_USER, LOCAL_KEY, LOCAL_EXE, REG_SZ, path,
	                    (DWORD)((wcslen(path) + 1) * sizeof(*path))) != ERROR_SUCCESS)
		MessageBoxW(dialog.hwndOwner, L"Could not save the FrameWeave location.", L"FrameWeave", MB_OK | MB_ICONERROR);
	return false;
}

/* Starts PushFPS (or brings it to the front). Returns a short status for the log. */
static const char *launch_pushfps(void)
{
	HWND existing = find_pushfps_window();
	if (existing) {
		if (IsIconic(existing))
			ShowWindow(existing, SW_RESTORE);
		SetForegroundWindow(existing);
		return "already running";
	}
	wchar_t path[EXE_PATH_CAP];
	if (!resolve_exe_path(path, EXE_PATH_CAP))
		return "FrameWeave.exe not found; choose its location in the source properties";
	wchar_t dir[EXE_PATH_CAP];
	wcscpy_s(dir, EXE_PATH_CAP, path);
	wchar_t *slash = wcsrchr(dir, L'\\');
	if (slash == dir + 2)
		slash[1] = 0; /* Keep the backslash in a drive root. */
	else
		*slash = 0;
	wchar_t command[EXE_PATH_CAP + 32];
	swprintf_s(command, EXE_PATH_CAP + 32, L"\"%ls\" --obs-game-capture", path);
	STARTUPINFOW startup = {0};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {0};
	if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, dir, &startup, &process))
		return "could not start FrameWeave.exe";
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return "started";
}

/* ------------------------------------------------------------ child source -- */

static obs_data_t *child_settings(int method, const char *window)
{
	obs_data_t *s = obs_data_create();
	obs_data_set_string(s, "window", window);
	obs_data_set_int(s, "priority", 2); /* WINDOW_PRIORITY_EXE */
	if (method == METHOD_GAME) {
		obs_data_set_string(s, "capture_mode", "window");
		obs_data_set_bool(s, "capture_cursor", false);
		obs_data_set_bool(s, "allow_transparency", false);
		obs_data_set_bool(s, "limit_framerate", false);
		obs_data_set_bool(s, "capture_overlays", false);
		obs_data_set_bool(s, "anti_cheat_hook", true);
		obs_data_set_int(s, "hook_rate", 2); /* fast: picks PushFPS up soon after it starts */
	} else {
		obs_data_set_int(s, "method", 2); /* Windows Graphics Capture */
		obs_data_set_bool(s, "cursor", false);
		obs_data_set_bool(s, "client_area", true);
		obs_data_set_bool(s, "compatibility", false);
	}
	return s;
}

/* Graphics thread only (video_tick), so it never races video_render. */
static void rebuild_child(struct pushfps_source *ps, int method)
{
	obs_data_t *settings = child_settings(method, ps->window);
	const char *kind = method == METHOD_GAME ? "game_capture" : "window_capture";
	obs_source_t *fresh = obs_source_create_private(kind, "FrameWeave capture", settings);
	obs_data_release(settings);
	if (!fresh)
		plog(LOG_WARNING, "could not create %s (is the win-capture plugin installed?)", kind);

	EnterCriticalSection(&ps->lock);
	obs_source_t *old = ps->child;
	ps->child = fresh;
	ps->child_method = method;
	LeaveCriticalSection(&ps->lock);

	if (old) {
		obs_source_remove_active_child(ps->self, old);
		obs_source_release(old);
	}
	if (fresh)
		obs_source_add_active_child(ps->self, fresh);
	plog(LOG_INFO, "capture method: %s", kind);
}

/* --------------------------------------------------------- source callbacks -- */

static const char *ps_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "FrameWeave";
}

static void ps_update(void *data, obs_data_t *settings)
{
	struct pushfps_source *ps = data;
	InterlockedExchange(&ps->wanted_method, (LONG)obs_data_get_int(settings, "method"));
	ps->autostart = obs_data_get_bool(settings, "autostart");
	ps->since_scan = SCAN_INTERVAL; /* look again on the next tick */
}

static void *ps_create(obs_data_t *settings, obs_source_t *source)
{
	struct pushfps_source *ps = calloc(1, sizeof(*ps));
	if (!ps)
		return NULL;
	ps->self = source;
	ps->child_method = -1;
	InitializeCriticalSection(&ps->lock);
	strcpy_s(ps->window, sizeof(ps->window), "FrameWeave::FrameWeave.exe");
	ps_update(ps, settings);
	plog(LOG_INFO, "source created");
	return ps;
}

static void ps_destroy(void *data)
{
	struct pushfps_source *ps = data;
	if (ps->child) {
		obs_source_remove_active_child(ps->self, ps->child);
		obs_source_release(ps->child);
	}
	DeleteCriticalSection(&ps->lock);
	free(ps);
}

static void ps_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "method", METHOD_GAME);
	obs_data_set_default_bool(settings, "autostart", true);
}

static void ps_activate(void *data)
{
	struct pushfps_source *ps = data;
	if (ps->autostart && !find_pushfps_window())
		plog(LOG_INFO, "auto start: %s", launch_pushfps());
}

static void ps_tick(void *data, float seconds)
{
	struct pushfps_source *ps = data;
	const int wanted = (int)InterlockedCompareExchange(&ps->wanted_method, 0, 0) == METHOD_WINDOW ? METHOD_WINDOW
													 : METHOD_GAME;
	bool rebuild = ps->child_method != wanted;

	ps->since_scan += seconds;
	if (ps->since_scan >= SCAN_INTERVAL) {
		ps->since_scan = 0.0f;
		HWND hwnd = find_pushfps_window_cached(&ps->last_window);
		char current[1024];
		if (hwnd && window_string(hwnd, current, sizeof(current)) && strcmp(current, ps->window) != 0) {
			strcpy_s(ps->window, sizeof(ps->window), current);
			ps->logged_missing = false;
			if (!rebuild && ps->child) {
				obs_data_t *s = child_settings(ps->child_method, ps->window);
				obs_source_update(ps->child, s);
				obs_data_release(s);
			}
			plog(LOG_INFO, "found the FrameWeave window");
		} else if (!hwnd && !ps->logged_missing) {
			ps->logged_missing = true;
			plog(LOG_INFO, "FrameWeave is not running yet; waiting for it");
		}
	}
	if (rebuild)
		rebuild_child(ps, wanted);
}

/* Render and tick both run on the graphics thread, and only the tick replaces the
 * child, so the render reads it without the lock. */
static void ps_render(void *data, gs_effect_t *effect)
{
	struct pushfps_source *ps = data;
	UNUSED_PARAMETER(effect);
	if (ps->child)
		obs_source_video_render(ps->child);
}

/* Other threads take a strong reference under the lock and use it outside. */
static obs_source_t *child_ref(struct pushfps_source *ps)
{
	EnterCriticalSection(&ps->lock);
	obs_source_t *child = ps->child ? obs_source_get_ref(ps->child) : NULL;
	LeaveCriticalSection(&ps->lock);
	return child;
}

static uint32_t child_size(struct pushfps_source *ps, bool width)
{
	obs_source_t *child = child_ref(ps);
	if (!child)
		return 0;
	const uint32_t v = width ? obs_source_get_width(child) : obs_source_get_height(child);
	obs_source_release(child);
	return v;
}

static uint32_t ps_width(void *data)
{
	return child_size(data, true);
}

static uint32_t ps_height(void *data)
{
	return child_size(data, false);
}

static void ps_enum_sources(void *data, obs_source_enum_proc_t cb, void *param)
{
	struct pushfps_source *ps = data;
	obs_source_t *child = child_ref(ps);
	if (!child)
		return;
	cb(ps->self, child, param);
	obs_source_release(child);
}

static bool ps_launch_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(p);
	UNUSED_PARAMETER(props);
	plog(LOG_INFO, "open FrameWeave: %s", launch_pushfps());
	return true; /* refresh the status line */
}

static obs_properties_t *ps_properties(void *data)
{
	struct pushfps_source *ps = data;
	obs_properties_t *props = obs_properties_create();

	const bool running = find_pushfps_window() != NULL;
	obs_properties_add_text(props, "status",
				running ? "FrameWeave is running. Its picture shows here."
					: "FrameWeave is not running. Click \"Open FrameWeave\" or show this source.",
				OBS_TEXT_INFO);
	obs_properties_add_button(props, "launch", "Open FrameWeave", ps_launch_clicked);

	obs_property_t *m = obs_properties_add_list(props, "method", "Capture method", OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(m, "Game capture (full frame rate, recommended)", METHOD_GAME);
	obs_property_list_add_int(m, "Window capture (if game capture shows nothing)", METHOD_WINDOW);
	obs_property_set_long_description(
		m, "Game capture receives every generated frame. It needs FrameWeave started with "
		   "--obs-game-capture (\"Open FrameWeave\" does that) or Settings > Compatible > OBS Game Capture "
		   "compatibility turned on in FrameWeave.");

	obs_properties_add_bool(props, "autostart", "Start FrameWeave when this source is shown");

	obs_properties_add_button(props, "choose_local_install", "FrameWeave.exe location (this PC)", choose_installed_exe);

	obs_properties_add_text(props, "audio_hint",
				"Sound: add an \"Application Audio Capture\" source and pick FrameWeave.exe.",
				OBS_TEXT_INFO);
	UNUSED_PARAMETER(ps);
	return props;
}

static struct obs_source_info pushfps_source_info = {
	.id = SOURCE_ID,
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = ps_get_name,
	.create = ps_create,
	.destroy = ps_destroy,
	.update = ps_update,
	.get_defaults = ps_defaults,
	.get_properties = ps_properties,
	.activate = ps_activate,
	.video_tick = ps_tick,
	.video_render = ps_render,
	.get_width = ps_width,
	.get_height = ps_height,
	.enum_active_sources = ps_enum_sources,
	.enum_all_sources = ps_enum_sources,
	.icon_type = OBS_ICON_TYPE_GAME_CAPTURE,
};

/* ------------------------------------------------------------------ module -- */

static void tools_open_pushfps(void *unused)
{
	UNUSED_PARAMETER(unused);
	plog(LOG_INFO, "Tools > Open FrameWeave: %s", launch_pushfps());
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "FrameWeave";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "FrameWeave source: console frame generation from a capture card, inside OBS.";
}

bool obs_module_load(void)
{
	obs_register_source(&pushfps_source_info);
	plog(LOG_INFO, "loaded version %s", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	/* The frontend API is there only in the OBS app (not in obs-ffmpeg-mux etc.). */
	obs_frontend_add_tools_menu_item("Open FrameWeave", tools_open_pushfps, NULL);
}

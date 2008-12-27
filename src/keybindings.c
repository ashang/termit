#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "termit.h"
#include "configs.h"
#include "termit_core_api.h"
#include "lua_api.h"
#include "keybindings.h"

extern lua_State* L;

static Display* disp;

void trace_keybindings()
{
#ifdef DEBUG
    TRACE_MSG("");
    TRACE("len: %d", configs.key_bindings->len);
    gint i = 0;
    for (; i<configs.key_bindings->len; ++i) {
        struct KeyBinding* kb = &g_array_index(configs.key_bindings, struct KeyBinding, i);
        TRACE("%s: %d, %d(%ld)", kb->name, kb->state, kb->keyval, kb->keycode);
    }
    TRACE_MSG("");
#endif
}
#define ADD_DEFAULT_KEYBINDING(keybinding_, lua_callback_) \
{ \
lua_getglobal(ls, lua_callback_); \
int func = luaL_ref(ls, LUA_REGISTRYINDEX); \
termit_bind_key(keybinding_, func); \
}
#define ADD_DEFAULT_MOUSEBINDING(mouse_event_, lua_callback_) \
{ \
lua_getglobal(ls, lua_callback_); \
int func = luaL_ref(ls, LUA_REGISTRYINDEX); \
termit_bind_mouse(mouse_event_, func); \
}

void termit_set_default_keybindings()
{
    lua_State* ls = L;
    disp = XOpenDisplay(NULL);
    ADD_DEFAULT_KEYBINDING("Alt-Left", "prevTab");
    ADD_DEFAULT_KEYBINDING("Alt-Right", "nextTab");
    ADD_DEFAULT_KEYBINDING("Ctrl-t", "openTab");
    ADD_DEFAULT_KEYBINDING("Ctrl-w", "closeTab");
    ADD_DEFAULT_KEYBINDING("Ctrl-Insert", "copy");
    ADD_DEFAULT_KEYBINDING("Shift-Insert", "paste");
    // push func to stack, get ref
    trace_keybindings();

    ADD_DEFAULT_MOUSEBINDING("DoubleClick", "openTab");
}

struct TermitModifier {
    const gchar* name;
    guint state;
};
struct TermitModifier termit_modifiers[] =
{
    {"Alt", GDK_MOD1_MASK}, 
    {"Ctrl", GDK_CONTROL_MASK},
    {"Shift", GDK_SHIFT_MASK},
    {"AltCtrl", GDK_CONTROL_MASK | GDK_MOD1_MASK},
    {"CtrlAlt", GDK_CONTROL_MASK | GDK_MOD1_MASK},
    {"ShiftCtrl", GDK_CONTROL_MASK | GDK_SHIFT_MASK},
    {"CtrlShift", GDK_CONTROL_MASK | GDK_SHIFT_MASK},
    {"AltShift", GDK_MOD1_MASK | GDK_SHIFT_MASK},
    {"ShiftAlt", GDK_MOD1_MASK | GDK_SHIFT_MASK},
    {"AltCtrlShift", GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK},
    {"AltShiftCtrl", GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK},
    {"CtrlAltShift", GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK},
    {"CtrlShiftAlt", GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK},
    {"ShiftAltCtrl", GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK},
    {"ShiftCtrlAlt", GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK}
};
static guint TermitModsSz = sizeof(termit_modifiers)/sizeof(struct TermitModifier);

static guint get_modifier_state(const gchar* token)
{
    if (!token)
        return GDK_NOTHING;
    gint i = 0;
    for (; i<TermitModsSz; ++i) {
        if (!strcmp(token, termit_modifiers[i].name))
            return termit_modifiers[i].state;
    }
    return GDK_NOTHING;
}

static gint get_kb_index(const gchar* name)
{
    gint i = 0;
    for (; i<configs.key_bindings->len; ++i) {
        struct KeyBinding* kb = &g_array_index(configs.key_bindings, struct KeyBinding, i);
        if (!strcmp(kb->name, name))
            return i;
    }
    return -1;
}

struct TermitMouseEvent {
    const gchar* name;
    GdkEventType type;
};
struct TermitMouseEvent termit_mouse_events[] =
{
    {"DoubleClick", GDK_2BUTTON_PRESS}
};
static guint TermitMouseEventsSz = sizeof(termit_mouse_events)/sizeof(struct TermitMouseEvent);

gint get_mouse_event_type(const gchar* event_name)
{
    if (!event_name)
        return GDK_NOTHING;
    gint i = 0;
    for (; i<TermitMouseEventsSz; ++i) {
        if (!strcmp(event_name, termit_mouse_events[i].name))
            return termit_mouse_events[i].type;
    }
    return GDK_NOTHING;
};

static gint get_mb_index(GdkEventType type)
{
    gint i = 0;
    for (; i<configs.mouse_bindings->len; ++i) {
        struct MouseBinding* mb = &g_array_index(configs.mouse_bindings, struct MouseBinding, i);
        if (type == mb->type)
            return i;
    }
    return -1;
}

void termit_unbind_key(const gchar* keybinding)
{
    gint kb_index = get_kb_index(keybinding);
    if (kb_index < 0) {
        TRACE("keybinding [%s] not found - skipping", keybinding);
        return;
    }
    struct KeyBinding* kb = &g_array_index(configs.key_bindings, struct KeyBinding, kb_index);
    g_free(kb->name);
    g_array_remove_index(configs.key_bindings, kb_index);
}

void termit_bind_key(const gchar* keybinding, int lua_callback)
{
    gchar** tokens = g_strsplit(keybinding, "-", 2);
    // token[0] - modifier. Only Alt, Ctrl or Shift allowed.
    if (!tokens[0] || !tokens[1])
        return;
    guint tmp_state = get_modifier_state(tokens[0]);
    if (tmp_state == GDK_NOTHING) {
        TRACE("Bad modifier: %s", keybinding);
        return;
    }
    // token[1] - key. Only alfabet and numeric keys allowed.
    guint tmp_keyval = gdk_keyval_from_name(tokens[1]);
    if (tmp_keyval == GDK_VoidSymbol) {
        TRACE("Bad keyval: %s", keybinding);
        return;
    }
//    TRACE("%s: %s(%d), %s(%d)", kb->name, tokens[0], tmp_state, tokens[1], tmp_keyval);
    g_strfreev(tokens);
    
    gint kb_index = get_kb_index(keybinding);
    if (kb_index < 0) {
        struct KeyBinding kb = {0};
        kb.name = g_strdup(keybinding);
        kb.state = tmp_state;
        kb.keyval = gdk_keyval_to_lower(tmp_keyval);
        kb.keycode = XKeysymToKeycode(disp, kb.keyval);
        kb.lua_callback = lua_callback;
        g_array_append_val(configs.key_bindings, kb);
    } else {
        struct KeyBinding* kb = &g_array_index(configs.key_bindings, struct KeyBinding, kb_index);
        kb->state = tmp_state;
        kb->keyval = gdk_keyval_to_lower(tmp_keyval);
        kb->keycode = XKeysymToKeycode(disp, kb->keyval);
        luaL_unref(L, LUA_REGISTRYINDEX, kb->lua_callback);
        kb->lua_callback = lua_callback;
    }
}

void termit_bind_mouse(const gchar* mouse_event, int lua_callback)
{
    GdkEventType type = get_mouse_event_type(mouse_event);
    if (type == GDK_NOTHING) {
        TRACE("unknown event: %s", mouse_event);
        return;
    }
    gint mb_index = get_mb_index(type);
    if (mb_index < 0) {
        struct MouseBinding mb = {0};
        mb.type = type;
        mb.lua_callback = lua_callback;
        g_array_append_val(configs.mouse_bindings, mb);
    } else {
        struct MouseBinding* mb = &g_array_index(configs.mouse_bindings, struct MouseBinding, mb_index);
        mb->type = type;
        luaL_unref(L, LUA_REGISTRYINDEX, mb->lua_callback);
        mb->lua_callback = lua_callback;
    }
}

void termit_unbind_mouse(const gchar* mouse_event)
{
    GdkEventType type = get_mouse_event_type(mouse_event);
    if (type == GDK_NOTHING) {
        TRACE("unknown event: %s", mouse_event);
        return;
    }
    gint mb_index = get_mb_index(type);
    if (mb_index < 0) {
        TRACE("mouse event [%d] not found - skipping", type);
        return;
    }
    g_array_remove_index(configs.mouse_bindings, mb_index);
}

static gboolean termit_key_press_use_keycode(GdkEventKey *event)
{
    gint i = 0;
    for (; i<configs.key_bindings->len; ++i) {
        struct KeyBinding* kb = &g_array_index(configs.key_bindings, struct KeyBinding, i);
        if (kb && (event->state & kb->state) == kb->state)
            if (event->hardware_keycode == kb->keycode) {
                termit_lua_dofunction(kb->lua_callback);
                return TRUE;
            }
    }
    return FALSE;
}

static gboolean termit_key_press_use_keysym(GdkEventKey *event)
{
    gint i = 0;
    for (; i<configs.key_bindings->len; ++i) {
        struct KeyBinding* kb = &g_array_index(configs.key_bindings, struct KeyBinding, i);
        if (kb && (event->state & kb->state) == kb->state)
            if (gdk_keyval_to_lower(event->keyval) == kb->keyval) {
                termit_lua_dofunction(kb->lua_callback);
                return TRUE;
            }
    }
    return FALSE;
}

gboolean termit_key_event(GdkEventKey* event)
{
    switch(configs.kb_policy) {
    case TermitKbUseKeycode:
        return termit_key_press_use_keycode(event);
        break;
    case TermitKbUseKeysym:
        return termit_key_press_use_keysym(event);
        break;
    default:
        ERROR("unknown kb_policy: %d", configs.kb_policy);
    }
    return FALSE;
}

gboolean termit_mouse_event(GdkEventButton* event)
{
    gint i = 0;
    for (; i<configs.mouse_bindings->len; ++i) {
        struct MouseBinding* kb = &g_array_index(configs.mouse_bindings, struct MouseBinding, i);
        if (kb && (event->type & kb->type))
            termit_lua_dofunction(kb->lua_callback);
    }
    return FALSE;
}


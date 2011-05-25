/*
 ** Callbacks
 ** (c) 2009 by Robert Manea et al.
*/

#include "uzbl-core.h"
#include "callbacks.h"
#include "events.h"
#include "util.h"
#include "io.h"
#include "menu.h"
#include "type.h"

void
set_proxy_url() {
    const gchar *url = uzbl.net.proxy_url;
    SoupSession *session  = uzbl.net.soup_session;
    SoupURI     *soup_uri = NULL;

    if (url != NULL || *url != 0 || *url != ' ')
        soup_uri = soup_uri_new(url);

    g_object_set(G_OBJECT(session), SOUP_SESSION_PROXY_URI, soup_uri, NULL);

    if(soup_uri)
        soup_uri_free(soup_uri);
}


void
set_authentication_handler() {
    /* Check if WEBKIT_TYPE_SOUP_AUTH_DIALOG feature is set */
    GSList *flist = soup_session_get_features (uzbl.net.soup_session, (GType) WEBKIT_TYPE_SOUP_AUTH_DIALOG);
    guint feature_is_set = g_slist_length(flist);
    g_slist_free(flist);

    if (uzbl.behave.authentication_handler == NULL || *uzbl.behave.authentication_handler == 0) {
        if (!feature_is_set)
            soup_session_add_feature_by_type
                (uzbl.net.soup_session, (GType) WEBKIT_TYPE_SOUP_AUTH_DIALOG);
    } else {
        if (feature_is_set)
            soup_session_remove_feature_by_type
                (uzbl.net.soup_session, (GType) WEBKIT_TYPE_SOUP_AUTH_DIALOG);
    }
    return;
}


void
set_status_background() {
    /* labels and hboxes do not draw their own background. applying this
     * on the vbox/main_window is ok as the statusbar is the only affected
     * widget. (if not, we could also use GtkEventBox) */
    GtkWidget* widget = uzbl.gui.main_window ? uzbl.gui.main_window : GTK_WIDGET (uzbl.gui.plug);

#if GTK_CHECK_VERSION(2,91,0)
    GdkRGBA color;
    gdk_rgba_parse (&color, uzbl.behave.status_background);
    gtk_widget_override_background_color (widget, GTK_STATE_NORMAL, &color);
#else
    GdkColor color;
    gdk_color_parse (uzbl.behave.status_background, &color);
    gtk_widget_modify_bg (widget, GTK_STATE_NORMAL, &color);
#endif
}


void
set_icon() {
    if(file_exists(uzbl.gui.icon)) {
        if (uzbl.gui.main_window)
            gtk_window_set_icon_from_file (GTK_WINDOW (uzbl.gui.main_window), uzbl.gui.icon, NULL);
    } else {
        g_printerr ("Icon \"%s\" not found. ignoring.\n", uzbl.gui.icon);
    }
}

void
cmd_set_geometry() {
    int ret=0, x=0, y=0;
    unsigned int w=0, h=0;
    if(uzbl.gui.geometry) {
        if(uzbl.gui.geometry[0] == 'm') { /* m/maximize/maximized */
            gtk_window_maximize((GtkWindow *)(uzbl.gui.main_window));
        } else {
            /* we used to use gtk_window_parse_geometry() but that didn't work how it was supposed to */
            ret = XParseGeometry(uzbl.gui.geometry, &x, &y, &w, &h);
            if(ret & XValue)
                gtk_window_move((GtkWindow *)uzbl.gui.main_window, x, y);
            if(ret & WidthValue)
                gtk_window_resize((GtkWindow *)uzbl.gui.main_window, w, h);
        }
    }

    /* update geometry var with the actual geometry
       this is necessary as some WMs don't seem to honour
       the above setting and we don't want to end up with
       wrong geometry information
    */
    retrieve_geometry();
}

void
cmd_set_status() {
    if (!uzbl.behave.show_status) {
        gtk_widget_hide(uzbl.gui.mainbar);
    } else {
        gtk_widget_show(uzbl.gui.mainbar);
    }
    update_title();
}

/* is the given string made up entirely of decimal digits? */
gboolean
string_is_integer(const char *s) {
    return (strspn(s, "0123456789") == strlen(s));
}

void
cmd_load_uri() {
    const gchar *uri = uzbl.state.uri;

    gchar   *newuri;
    SoupURI *soup_uri;

    /* Strip leading whitespaces */
    while (*uri && isspace(*uri))
        uri++;

    /* evaluate javascript: URIs */
    if (!strncmp (uri, "javascript:", 11)) {
        eval_js(uzbl.gui.web_view, uri, NULL, "javascript:");
        return;
    }

    /* attempt to parse the URI */
    soup_uri = soup_uri_new(uri);

    if (!soup_uri) {
        /* it's not a valid URI, maybe it's a path on the filesystem. */
        const gchar *fullpath;
        if (g_path_is_absolute (uri))
            fullpath = uri;
        else {
            gchar *wd = g_get_current_dir ();
            fullpath = g_build_filename (wd, uri, NULL);
            g_free(wd);
        }

        struct stat stat_result;
        if (! g_stat(fullpath, &stat_result))
            newuri = g_strconcat("file://", fullpath, NULL);
        else
            newuri = g_strconcat("http://", uri, NULL);
    } else {
        if(soup_uri->host == NULL && string_is_integer(soup_uri->path))
            /* the user probably typed in a host:port without a scheme */
            newuri = g_strconcat("http://", uri, NULL);
        else
            newuri = g_strdup(uri);

        soup_uri_free(soup_uri);
    }

    webkit_web_view_load_uri (uzbl.gui.web_view, newuri);
    g_free (newuri);
}

void
cmd_max_conns() {
    g_object_set(G_OBJECT(uzbl.net.soup_session),
            SOUP_SESSION_MAX_CONNS, uzbl.net.max_conns, NULL);
}

void
cmd_max_conns_host() {
    g_object_set(G_OBJECT(uzbl.net.soup_session),
            SOUP_SESSION_MAX_CONNS_PER_HOST, uzbl.net.max_conns_host, NULL);
}

void
cmd_http_debug() {
    soup_session_remove_feature
        (uzbl.net.soup_session, SOUP_SESSION_FEATURE(uzbl.net.soup_logger));
    /* do we leak if this doesn't get freed? why does it occasionally crash if freed? */
    /*g_free(uzbl.net.soup_logger);*/

    uzbl.net.soup_logger = soup_logger_new(uzbl.behave.http_debug, -1);
    soup_session_add_feature(uzbl.net.soup_session,
            SOUP_SESSION_FEATURE(uzbl.net.soup_logger));
}

GObject*
view_settings() {
    return G_OBJECT(webkit_web_view_get_settings(uzbl.gui.web_view));
}

void
cmd_font_size() {
    GObject *ws = view_settings();
    if (uzbl.behave.font_size > 0) {
        g_object_set (ws, "default-font-size", uzbl.behave.font_size, NULL);
    }

    if (uzbl.behave.monospace_size > 0) {
        g_object_set (ws, "default-monospace-font-size",
                      uzbl.behave.monospace_size, NULL);
    } else {
        g_object_set (ws, "default-monospace-font-size",
                      uzbl.behave.font_size, NULL);
    }
}

void
cmd_zoom_level() {
    webkit_web_view_set_zoom_level (uzbl.gui.web_view, uzbl.behave.zoom_level);
}

#define EXPOSE_WEBKIT_VIEW_SETTINGS(SYM, STORAGE, PROPERTY) void cmd_##SYM() { \
  g_object_set(view_settings(), (PROPERTY), (STORAGE), NULL); \
}

EXPOSE_WEBKIT_VIEW_SETTINGS(default_font_family,    uzbl.behave.default_font_family,    "default-font-family")
EXPOSE_WEBKIT_VIEW_SETTINGS(monospace_font_family,  uzbl.behave.monospace_font_family,  "monospace-font-family")
EXPOSE_WEBKIT_VIEW_SETTINGS(sans_serif_font_family, uzbl.behave.sans_serif_font_family, "sans_serif-font-family")
EXPOSE_WEBKIT_VIEW_SETTINGS(serif_font_family,      uzbl.behave.serif_font_family,      "serif-font-family")
EXPOSE_WEBKIT_VIEW_SETTINGS(cursive_font_family,    uzbl.behave.cursive_font_family,    "cursive-font-family")
EXPOSE_WEBKIT_VIEW_SETTINGS(fantasy_font_family,    uzbl.behave.fantasy_font_family,    "fantasy-font-family")

EXPOSE_WEBKIT_VIEW_SETTINGS(minimum_font_size,      uzbl.behave.minimum_font_size,      "minimum_font_size")

EXPOSE_WEBKIT_VIEW_SETTINGS(disable_plugins, !uzbl.behave.disable_plugins, "enable-plugins")
EXPOSE_WEBKIT_VIEW_SETTINGS(disable_scripts, !uzbl.behave.disable_scripts, "enable-scripts")

EXPOSE_WEBKIT_VIEW_SETTINGS(javascript_windows, uzbl.behave.javascript_windows, "javascript-can-open-windows-automatically")

EXPOSE_WEBKIT_VIEW_SETTINGS(autoload_img,       uzbl.behave.autoload_img,       "auto-load-images")
EXPOSE_WEBKIT_VIEW_SETTINGS(autoshrink_img,     uzbl.behave.autoshrink_img,     "auto-shrink-images")

EXPOSE_WEBKIT_VIEW_SETTINGS(enable_pagecache,   uzbl.behave.enable_pagecache,   "enable-page-cache")
EXPOSE_WEBKIT_VIEW_SETTINGS(enable_private,     uzbl.behave.enable_private,     "enable-private-browsing")

EXPOSE_WEBKIT_VIEW_SETTINGS(enable_spellcheck,  uzbl.behave.enable_spellcheck,  "enable-spell-checking")
EXPOSE_WEBKIT_VIEW_SETTINGS(resizable_txt,      uzbl.behave.resizable_txt,      "resizable-text-areas")

EXPOSE_WEBKIT_VIEW_SETTINGS(style_uri,        uzbl.behave.style_uri,         "user-stylesheet-uri")
EXPOSE_WEBKIT_VIEW_SETTINGS(print_bg,         uzbl.behave.print_bg,          "print-backgrounds")
EXPOSE_WEBKIT_VIEW_SETTINGS(enforce_96dpi,    uzbl.behave.enforce_96dpi,     "enforce-96-dpi")

EXPOSE_WEBKIT_VIEW_SETTINGS(caret_browsing,   uzbl.behave.caret_browsing,    "enable-caret-browsing")

EXPOSE_WEBKIT_VIEW_SETTINGS(default_encoding, uzbl.behave.default_encoding,  "default-encoding")

void
set_current_encoding() {
    gchar *encoding = uzbl.behave.current_encoding;
    if(strlen(encoding) == 0)
        encoding = NULL;

    webkit_web_view_set_custom_encoding(uzbl.gui.web_view, encoding);
}

void
cmd_fifo_dir() {
    uzbl.behave.fifo_dir = init_fifo(uzbl.behave.fifo_dir);
}

void
cmd_socket_dir() {
    uzbl.behave.socket_dir = init_socket(uzbl.behave.socket_dir);
}

void
cmd_inject_html() {
    if(uzbl.behave.inject_html) {
        webkit_web_view_load_html_string (uzbl.gui.web_view,
                uzbl.behave.inject_html, NULL);
    }
}

void
cmd_useragent() {
    if (*uzbl.net.useragent == ' ') {
        g_free (uzbl.net.useragent);
        uzbl.net.useragent = NULL;
    } else {
        g_object_set(G_OBJECT(uzbl.net.soup_session), SOUP_SESSION_USER_AGENT,
            uzbl.net.useragent, NULL);
    }
}

void
set_accept_languages() {
    if (*uzbl.net.accept_languages == ' ') {
        g_free (uzbl.net.accept_languages);
        uzbl.net.accept_languages = NULL;
    } else {
        g_object_set(G_OBJECT(uzbl.net.soup_session),
            SOUP_SESSION_ACCEPT_LANGUAGE, uzbl.net.accept_languages, NULL);
    }
}

void
cmd_scrollbars_visibility() {
    GtkPolicyType policy = uzbl.gui.scrollbars_visible ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER;

    gtk_scrolled_window_set_policy ( GTK_SCROLLED_WINDOW (uzbl.gui.scrolled_win), policy, policy );
}

/* requires webkit >=1.1.14 */
void
cmd_view_source() {
    webkit_web_view_set_view_source_mode(uzbl.gui.web_view,
            (gboolean) uzbl.behave.view_source);
}

void
cmd_set_zoom_type () {
    if(uzbl.behave.zoom_type)
        webkit_web_view_set_full_content_zoom (uzbl.gui.web_view, TRUE);
    else
        webkit_web_view_set_full_content_zoom (uzbl.gui.web_view, FALSE);
}

void
link_hover_cb (WebKitWebView *page, const gchar *title, const gchar *link, gpointer data) {
    (void) page; (void) title; (void) data;
    State *s = &uzbl.state;

    if(s->last_selected_url)
        g_free(s->last_selected_url);

    if(s->selected_url) {
        s->last_selected_url = g_strdup(s->selected_url);
        g_free(s->selected_url);
        s->selected_url = NULL;
    } else
        s->last_selected_url = NULL;

    if(s->last_selected_url && g_strcmp0(link, s->last_selected_url))
        send_event(LINK_UNHOVER, NULL, TYPE_STR, s->last_selected_url, NULL);

    if (link) {
        s->selected_url = g_strdup(link);
        send_event(LINK_HOVER,   NULL, TYPE_STR, s->selected_url, NULL);
    }

    update_title();
}

void
title_change_cb (WebKitWebView* web_view, GParamSpec param_spec) {
    (void) web_view;
    (void) param_spec;
    const gchar *title = webkit_web_view_get_title(web_view);
    if (uzbl.gui.main_title)
        g_free (uzbl.gui.main_title);
    uzbl.gui.main_title = title ? g_strdup (title) : g_strdup ("(no title)");
    update_title();
    send_event(TITLE_CHANGED, NULL, TYPE_STR, uzbl.gui.main_title, NULL);
    g_setenv("UZBL_TITLE", uzbl.gui.main_title, TRUE);
}

void
progress_change_cb (WebKitWebView* web_view, GParamSpec param_spec) {
    (void) param_spec;
    int progress = webkit_web_view_get_progress(web_view) * 100;
    send_event(LOAD_PROGRESS, NULL, TYPE_INT, progress, NULL);
}

void
load_status_change_cb (WebKitWebView* web_view, GParamSpec param_spec) {
    (void) param_spec;

    WebKitWebFrame  *frame;
    WebKitLoadStatus status = webkit_web_view_get_load_status(web_view);
    switch(status) {
        case WEBKIT_LOAD_PROVISIONAL:
            send_event(LOAD_START,  NULL, TYPE_STR, uzbl.state.uri ? uzbl.state.uri : "", NULL);
            break;
        case WEBKIT_LOAD_COMMITTED:
            frame = webkit_web_view_get_main_frame(web_view);
            send_event(LOAD_COMMIT, NULL, TYPE_STR, webkit_web_frame_get_uri (frame), NULL);
            break;
        case WEBKIT_LOAD_FINISHED:
            send_event(LOAD_FINISH, NULL, TYPE_STR, uzbl.state.uri, NULL);
            break;
        case WEBKIT_LOAD_FIRST_VISUALLY_NON_EMPTY_LAYOUT:
            break; /* we don't do anything with this (yet) */
        case WEBKIT_LOAD_FAILED:
            break; /* load_error_cb will handle this case */
    }
}

gboolean
load_error_cb (WebKitWebView* page, WebKitWebFrame* frame, gchar *uri, gpointer web_err, gpointer ud) {
    (void) page; (void) frame; (void) ud;
    GError *err = web_err;

    send_event (LOAD_ERROR, NULL,
        TYPE_STR, uri,
        TYPE_INT, err->code,
        TYPE_STR, err->message,
        NULL);

    return FALSE;
}

void
uri_change_cb (WebKitWebView *web_view, GParamSpec param_spec) {
  (void) param_spec;

  g_free (uzbl.state.uri);
  g_object_get (web_view, "uri", &uzbl.state.uri, NULL);
  g_setenv("UZBL_URI", uzbl.state.uri, TRUE);
}

void
selection_changed_cb(WebKitWebView *webkitwebview, gpointer ud) {
    (void)ud;
    gchar *tmp;

    webkit_web_view_copy_clipboard(webkitwebview);
    tmp = gtk_clipboard_wait_for_text(gtk_clipboard_get (GDK_SELECTION_CLIPBOARD));
    send_event(SELECTION_CHANGED, NULL, TYPE_STR, tmp ? tmp : "", NULL);
    g_free(tmp);
}

void
destroy_cb (GtkWidget* widget, gpointer data) {
    (void) widget;
    (void) data;
    gtk_main_quit ();
}

gboolean
configure_event_cb(GtkWidget* window, GdkEventConfigure* event) {
    (void) window;
    (void) event;
    gchar *lastgeo = NULL;

    lastgeo = g_strdup(uzbl.gui.geometry);
    retrieve_geometry();

    if(strcmp(lastgeo, uzbl.gui.geometry))
        send_event(GEOMETRY_CHANGED, NULL, TYPE_STR, uzbl.gui.geometry, NULL);
    g_free(lastgeo);

    return FALSE;
}

gboolean
focus_cb(GtkWidget* window, GdkEventFocus* event, void *ud) {
    (void) window;
    (void) event;
    (void) ud;

    send_event (event->in?FOCUS_GAINED:FOCUS_LOST, NULL, NULL);

    return FALSE;
}

gboolean
key_press_cb (GtkWidget* window, GdkEventKey* event) {
    (void) window;

    if(event->type == GDK_KEY_PRESS)
        key_to_event(event->keyval, event->state, event->is_modifier, GDK_KEY_PRESS);

    return uzbl.behave.forward_keys ? FALSE : TRUE;
}

gboolean
key_release_cb (GtkWidget* window, GdkEventKey* event) {
    (void) window;

    if(event->type == GDK_KEY_RELEASE)
        key_to_event(event->keyval, event->state, event->is_modifier, GDK_KEY_RELEASE);

    return uzbl.behave.forward_keys ? FALSE : TRUE;
}

gboolean
button_press_cb (GtkWidget* window, GdkEventButton* event) {
    (void) window;
    gint context;
    gchar *details;
    gboolean propagate = FALSE,
             sendev    = FALSE;

    if(event->type == GDK_BUTTON_PRESS) {
        if(uzbl.state.last_button)
            gdk_event_free((GdkEvent *)uzbl.state.last_button);
        uzbl.state.last_button = (GdkEventButton *)gdk_event_copy((GdkEvent *)event);

        context = get_click_context(NULL);
        /* left click */
        if(event->button == 1) {
            if((context & WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE))
                send_event(FORM_ACTIVE, NULL, TYPE_NAME, "button1", NULL);
            else if((context & WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT))
                send_event(ROOT_ACTIVE, NULL, TYPE_NAME, "button1", NULL);
        }
        else if(event->button == 2 && !(context & WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE)) {
            sendev    = TRUE;
            propagate = TRUE;
        }
        else if(event->button > 3) {
            sendev    = TRUE;
            propagate = TRUE;
        }

        if(sendev) {
            details = g_strdup_printf("Button%d", event->button);
            send_event(KEY_PRESS, NULL, TYPE_NAME, details, NULL);
            g_free (details);
        }
    }

    return propagate;
}

gboolean
button_release_cb (GtkWidget* window, GdkEventButton* event) {
    (void) window;
    gint context;
    gchar *details;
    gboolean propagate = FALSE,
             sendev    = FALSE;

    context = get_click_context(NULL);
    if(event->type == GDK_BUTTON_RELEASE) {
        if(event->button == 2 && !(context & WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE)) {
            sendev    = TRUE;
            propagate = TRUE;
        }
        else if(event->button > 3) {
            sendev    = TRUE;
            propagate = TRUE;
        }

        if(sendev) {
            details = g_strdup_printf("Button%d", event->button);
            send_event(KEY_RELEASE, NULL, TYPE_NAME, details, NULL);
            g_free (details);
        }
    }

    return propagate;
}

gboolean
motion_notify_cb(GtkWidget* window, GdkEventMotion* event, gpointer user_data) {
    (void) window;
    (void) event;
    (void) user_data;

    send_event (PTR_MOVE, NULL,
        TYPE_FLOAT, event->x,
        TYPE_FLOAT, event->y,
        TYPE_INT, event->state,
        NULL);

    return FALSE;
}

gboolean
navigation_decision_cb (WebKitWebView *web_view, WebKitWebFrame *frame, WebKitNetworkRequest *request, WebKitWebNavigationAction *navigation_action, WebKitWebPolicyDecision *policy_decision, gpointer user_data) {
    (void) web_view;
    (void) frame;
    (void) navigation_action;
    (void) user_data;

    const gchar* uri = webkit_network_request_get_uri (request);
    gboolean decision_made = FALSE;

    if (uzbl.state.verbose)
        printf("Navigation requested -> %s\n", uri);

    if (uzbl.behave.scheme_handler) {
        GString *result = g_string_new ("");
        GArray *a = g_array_new (TRUE, FALSE, sizeof(gchar*));
        const CommandInfo *c = parse_command_parts(uzbl.behave.scheme_handler, a);

        if(c) {
            g_array_append_val(a, uri);
            run_parsed_command(c, a, result);
        }
        g_array_free(a, TRUE);

        if(result->len > 0) {
            char *p = strchr(result->str, '\n' );
            if ( p != NULL ) *p = '\0';
            if (!strcmp(result->str, "USED")) {
                webkit_web_policy_decision_ignore(policy_decision);
                decision_made = TRUE;
            }
        }

        g_string_free(result, TRUE);
    }
    if (!decision_made)
        webkit_web_policy_decision_use(policy_decision);

    return TRUE;
}

gboolean
new_window_cb (WebKitWebView *web_view, WebKitWebFrame *frame,
        WebKitNetworkRequest *request, WebKitWebNavigationAction *navigation_action,
        WebKitWebPolicyDecision *policy_decision, gpointer user_data) {
    (void) web_view;
    (void) frame;
    (void) navigation_action;
    (void) policy_decision;
    (void) user_data;

    if (uzbl.state.verbose)
        printf ("New window requested -> %s \n", webkit_network_request_get_uri (request));

    /* This event function causes troubles with `target="_blank"` anchors.
     * Either we:
     *  1. Comment it out and target blank links are ignored.
     *  2. Uncomment it and two windows are opened when you click on target
     *     blank links.
     *
     * This problem is caused by create_web_view_cb also being called whenever
     * this callback is triggered thus resulting in the doubled events.
     *
     * We are leaving this uncommented as we would rather links open twice
     * than not at all.
     */
    send_event (NEW_WINDOW, NULL, TYPE_STR, webkit_network_request_get_uri (request), NULL);

    webkit_web_policy_decision_ignore (policy_decision);
    return TRUE;
}

gboolean
mime_policy_cb(WebKitWebView *web_view, WebKitWebFrame *frame, WebKitNetworkRequest *request, gchar *mime_type,  WebKitWebPolicyDecision *policy_decision, gpointer user_data) {
    (void) frame;
    (void) request;
    (void) user_data;

    /* If we can display it, let's display it... */
    if (webkit_web_view_can_show_mime_type (web_view, mime_type)) {
        webkit_web_policy_decision_use (policy_decision);
        return TRUE;
    }

    /* ...everything we can't display is downloaded */
    webkit_web_policy_decision_download (policy_decision);
    return TRUE;
}

void
request_starting_cb(WebKitWebView *web_view, WebKitWebFrame *frame, WebKitWebResource *resource,
        WebKitNetworkRequest *request, WebKitNetworkResponse *response, gpointer user_data) {
    (void) web_view;
    (void) frame;
    (void) resource;
    (void) response;
    (void) user_data;

    send_event (REQUEST_STARTING, NULL, TYPE_STR, webkit_network_request_get_uri(request), NULL);
}

void
create_web_view_js_cb (WebKitWebView* web_view, GParamSpec param_spec) {
    (void) web_view;
    (void) param_spec;

    webkit_web_view_stop_loading(web_view);
    const gchar* uri = webkit_web_view_get_uri(web_view);

    if (strncmp(uri, "javascript:", strlen("javascript:")) == 0) {
        eval_js(uzbl.gui.web_view, (gchar*) uri + strlen("javascript:"), NULL, "javascript:");
        gtk_widget_destroy(GTK_WIDGET(web_view));
    }
    else
        send_event(NEW_WINDOW, NULL, TYPE_STR, uri, NULL);
}

/*@null@*/ WebKitWebView*
create_web_view_cb (WebKitWebView  *web_view, WebKitWebFrame *frame, gpointer user_data) {
    (void) web_view;
    (void) frame;
    (void) user_data;

    if (uzbl.state.verbose)
        printf("New web view -> javascript link...\n");

    WebKitWebView* new_view = WEBKIT_WEB_VIEW(webkit_web_view_new());

    g_object_connect (new_view, "signal::notify::uri",
                           G_CALLBACK(create_web_view_js_cb), NULL, NULL);
    return new_view;
}

void
download_progress_cb(WebKitDownload *download, GParamSpec *pspec, gpointer user_data) {
    (void) pspec; (void) user_data;

    gdouble progress;
    g_object_get(download, "progress", &progress, NULL);

    const gchar *dest_uri = webkit_download_get_destination_uri(download);
    const gchar *dest_path = dest_uri + strlen("file://");

    send_event(DOWNLOAD_PROGRESS, NULL,
        TYPE_STR, dest_path,
        TYPE_FLOAT, progress,
        NULL);
}

void
download_status_cb(WebKitDownload *download, GParamSpec *pspec, gpointer user_data) {
    (void) pspec; (void) user_data;

    WebKitDownloadStatus status;
    g_object_get(download, "status", &status, NULL);

    switch(status) {
        case WEBKIT_DOWNLOAD_STATUS_CREATED:
        case WEBKIT_DOWNLOAD_STATUS_STARTED:
        case WEBKIT_DOWNLOAD_STATUS_ERROR:
        case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
            return; /* these are irrelevant */
        case WEBKIT_DOWNLOAD_STATUS_FINISHED:
        {
            const gchar *dest_uri = webkit_download_get_destination_uri(download);
            const gchar *dest_path = dest_uri + strlen("file://");
            send_event(DOWNLOAD_COMPLETE, NULL, TYPE_STR, dest_path, NULL);
        }
    }
}

gboolean
download_cb(WebKitWebView *web_view, WebKitDownload *download, gpointer user_data) {
    (void) web_view; (void) user_data;

    /* get the URI being downloaded */
    const gchar *uri = webkit_download_get_uri(download);

    /* get the destination path, if specified.
     * this is only intended to be set when this function is trigger by an
     * explicit download using uzbl's 'download' action. */
    const gchar *destination = user_data;

    if (uzbl.state.verbose)
        printf("Download requested -> %s\n", uri);

    if (!uzbl.behave.download_handler) {
        webkit_download_cancel(download);
        return FALSE; /* reject downloads when there's no download handler */
    }

    /* get a reasonable suggestion for a filename */
    const gchar *suggested_filename;
    g_object_get(download, "suggested-filename", &suggested_filename, NULL);

    /* get the mimetype of the download */
    const gchar *content_type = NULL;
    WebKitNetworkResponse *r  = webkit_download_get_network_response(download);
    /* downloads can be initiated from the context menu, in that case there is
       no network response yet and trying to get one would crash. */
    if(WEBKIT_IS_NETWORK_RESPONSE(r)) {
        SoupMessage        *m = webkit_network_response_get_message(r);
        SoupMessageHeaders *h = NULL;
        g_object_get(m, "response-headers", &h, NULL);
        if(h) /* some versions of libsoup don't have "response-headers" here */
            content_type = soup_message_headers_get_one(h, "Content-Type");
    }

    if(!content_type)
        content_type = "application/octet-stream";

    /* get the filesize of the download, as given by the server.
       (this may be inaccurate, there's nothing we can do about that.)  */
    unsigned int total_size = webkit_download_get_total_size(download);

    GArray *a = g_array_new (TRUE, FALSE, sizeof(gchar*));
    const CommandInfo *c = parse_command_parts(uzbl.behave.download_handler, a);
    if(!c) {
        webkit_download_cancel(download);
        g_array_free(a, TRUE);
        return FALSE;
    }

    g_array_append_val(a, uri);
    g_array_append_val(a, suggested_filename);
    g_array_append_val(a, content_type);
    gchar *total_size_s = g_strdup_printf("%d", total_size);
    g_array_append_val(a, total_size_s);

    if(destination)
        g_array_append_val(a, destination);

    GString *result = g_string_new ("");
    run_parsed_command(c, a, result);

    g_free(total_size_s);
    g_array_free(a, TRUE);

    /* no response, cancel the download */
    if(result->len == 0) {
        webkit_download_cancel(download);
        return FALSE;
    }

    /* we got a response, it's the path we should download the file to */
    gchar *destination_path = result->str;
    g_string_free(result, FALSE);

    /* presumably people don't need newlines in their filenames. */
    char *p = strchr(destination_path, '\n');
    if ( p != NULL ) *p = '\0';

    /* set up progress callbacks */
    g_signal_connect(download, "notify::status",   G_CALLBACK(download_status_cb),   NULL);
    g_signal_connect(download, "notify::progress", G_CALLBACK(download_progress_cb), NULL);

    /* convert relative path to absolute path */
    if(destination_path[0] != '/') {
        gchar *rel_path = destination_path;
        gchar *cwd = g_get_current_dir();
        destination_path = g_strconcat(cwd, "/", destination_path, NULL);
        g_free(cwd);
        g_free(rel_path);
    }

    send_event(DOWNLOAD_STARTED, NULL, TYPE_STR, destination_path, NULL);

    /* convert absolute path to file:// URI */
    gchar *destination_uri = g_strconcat("file://", destination_path, NULL);
    g_free(destination_path);

    webkit_download_set_destination_uri(download, destination_uri);
    g_free(destination_uri);

    return TRUE;
}

void
send_scroll_event(int type, GtkAdjustment *adjust) {
    gdouble value = gtk_adjustment_get_value(adjust);
    gdouble min = gtk_adjustment_get_lower(adjust);
    gdouble max = gtk_adjustment_get_upper(adjust);
    gdouble page = gtk_adjustment_get_page_size(adjust);

    send_event (type, NULL,
        TYPE_FLOAT, value,
        TYPE_FLOAT, min,
        TYPE_FLOAT, max,
        TYPE_FLOAT, page,
        NULL);
}

gboolean
scroll_vert_cb(GtkAdjustment *adjust, void *w) {
    (void) w;
    send_scroll_event(SCROLL_VERT, adjust);
    return (FALSE);
}

gboolean
scroll_horiz_cb(GtkAdjustment *adjust, void *w) {
    (void) w;
    send_scroll_event(SCROLL_HORIZ, adjust);
    return (FALSE);
}

void
run_menu_command(GtkWidget *menu, MenuItem *mi) {
    (void) menu;

    if (mi->context & WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE) {
        gchar* uri;
        g_object_get(mi->hittest, "image-uri", &uri, NULL);
        gchar* cmd = g_strdup_printf("%s %s", mi->cmd, uri);

        parse_cmd_line(cmd, NULL);

        g_free(cmd);
        g_free(uri);
        g_object_unref(mi->hittest);
    }
    else {
        parse_cmd_line(mi->cmd, NULL);
    }
}


void
populate_popup_cb(WebKitWebView *v, GtkMenu *m, void *c) {
    (void) c;
    GUI *g = &uzbl.gui;
    GtkWidget *item;
    MenuItem *mi;
    guint i=0;
    gint context, hit=0;

    if(!g->menu_items)
        return;

    /* check context */
    if((context = get_click_context(NULL)) == -1)
        return;

    for(i=0; i < uzbl.gui.menu_items->len; i++) {
        hit = 0;
        mi = g_ptr_array_index(uzbl.gui.menu_items, i);

        if (mi->context & WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE) {
            GdkEventButton ev;
            gint x, y;
            gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(v)), &x, &y, NULL);
            ev.x = x;
            ev.y = y;
            mi->hittest = webkit_web_view_get_hit_test_result(v, &ev);
        }

        if((mi->context > WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT) &&
                (context & mi->context)) {
            if(mi->issep) {
                item = gtk_separator_menu_item_new();
                gtk_menu_shell_append(GTK_MENU_SHELL(m), item);
                gtk_widget_show(item);
            }
            else {
                item = gtk_menu_item_new_with_label(mi->name);
                g_signal_connect(item, "activate",
                        G_CALLBACK(run_menu_command), mi);
                gtk_menu_shell_append(GTK_MENU_SHELL(m), item);
                gtk_widget_show(item);
            }
            hit++;
        }

        if((mi->context == WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT)  &&
                (context <= WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT) &&
                !hit) {
            if(mi->issep) {
                item = gtk_separator_menu_item_new();
                gtk_menu_shell_append(GTK_MENU_SHELL(m), item);
                gtk_widget_show(item);
            }
            else {
                item = gtk_menu_item_new_with_label(mi->name);
                g_signal_connect(item, "activate",
                        G_CALLBACK(run_menu_command), mi);
                gtk_menu_shell_append(GTK_MENU_SHELL(m), item);
                gtk_widget_show(item);
            }
        }
    }
}

gboolean
scrollbars_policy_cb(WebKitWebView *view) {
    (void) view;
    return TRUE;
}

void
window_object_cleared_cb(WebKitWebView *webview, WebKitWebFrame *frame,
    JSGlobalContextRef *context, JSObjectRef *object) {
    (void) frame; (void) context; (void) object;
#if WEBKIT_CHECK_VERSION (1, 3, 13)
    // Take this opportunity to set some callbacks on the DOM
    WebKitDOMDocument *document = webkit_web_view_get_dom_document (webview);
    webkit_dom_event_target_add_event_listener (WEBKIT_DOM_EVENT_TARGET (document),
        "focus", G_CALLBACK(dom_focus_cb), TRUE, NULL);
    webkit_dom_event_target_add_event_listener (WEBKIT_DOM_EVENT_TARGET (document),
        "blur", G_CALLBACK(dom_focus_cb), TRUE, NULL);
#else
	(void) webview;
#endif
}

#if WEBKIT_CHECK_VERSION (1, 3, 13)
void
dom_focus_cb(WebKitDOMEventTarget *target, WebKitDOMEvent *event, gpointer user_data) {
    (void) target; (void) user_data;
    WebKitDOMEventTarget *etarget = webkit_dom_event_get_target (event);
    gchar* name = webkit_dom_node_get_node_name (WEBKIT_DOM_NODE (etarget));
    send_event (FOCUS_ELEMENT, NULL, TYPE_STR, name, NULL);
}

void
dom_blur_cb(WebKitDOMEventTarget *target, WebKitDOMEvent *event, gpointer user_data) {
    (void) target; (void) user_data;
    WebKitDOMEventTarget *etarget = webkit_dom_event_get_target (event);
    gchar* name = webkit_dom_node_get_node_name (WEBKIT_DOM_NODE (etarget));
    send_event (BLUR_ELEMENT, NULL, TYPE_STR, name, NULL);
}
#endif

/* vi: set et ts=4: */

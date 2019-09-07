#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <gio/gio.h>
#include <webkit2/webkit-web-extension.h>
#include <webkitdom/webkitdom.h>
#include <webkitdom/WebKitDOMDOMWindowUnstable.h>

#include "common.h"

#define LENGTH(x)   (sizeof(x) / sizeof(x[0]))

typedef struct Page {
	guint64 id;
	WebKitWebPage *webpage;
	struct Page *next;
} Page;

static int sock;
static Page *pages;

Page *
newpage(WebKitWebPage *page)
{
	Page *p;

	if (!(p = calloc(1, sizeof(Page)))) {
		fputs("Cannot malloc!\n", stderr);
		exit(1);
	}

	p->next = pages;
	pages = p;

	p->id = webkit_web_page_get_id(page);
	p->webpage = page;

	return p;
}

static void
msgsurf(Page *p, const char *s)
{
	static char msg[MSGBUFSZ];
	size_t sln = strlen(s);
	int ret;

	if ((ret = snprintf(msg, sizeof(msg), "%c%s", p ? p->id : 0, s))
	    >= sizeof(msg)) {
		fprintf(stderr, "webext: msg: message too long: %d\n", ret);
		return;
	}

	if (send(sock, msg, ret, 0) < 0)
		fprintf(stderr, "webext: error sending: %s\n", msg+1);
}

static gboolean
readsock(GIOChannel *s, GIOCondition c, gpointer unused)
{
	static char msg[MSGBUFSZ];
	WebKitDOMDOMWindow *view;
	GError *gerr = NULL;
	gsize msgsz;
	glong wh, ww;
	Page *p;

	if (g_io_channel_read_chars(s, msg, LENGTH(msg), &msgsz, &gerr) !=
	    G_IO_STATUS_NORMAL) {
		if (gerr) {
			fprintf(stderr, "webext: error reading socket: %s\n",
			        gerr->message);
			g_error_free(gerr);
		}
		return TRUE;
	}

	if (msgsz < 2) {
		fprintf(stderr, "webext: readsock: message too short: %d\n",
		        msgsz);
		return TRUE;
	}

	for (p = pages; p; p = p->next) {
		if (p->id == msg[0])
			break;
	}
	if (!p || !(view = webkit_dom_document_get_default_view(
	            webkit_web_page_get_dom_document(p->webpage))))
		return TRUE;

	switch (msg[1]) {
	case 'h':
		if (msgsz != 3)
			return TRUE;
		ww = webkit_dom_dom_window_get_inner_width(view);
		webkit_dom_dom_window_scroll_by(view,
		                                (ww / 100) * msg[2], 0);
		break;
	case 'v':
		if (msgsz != 3)
			return TRUE;
		wh = webkit_dom_dom_window_get_inner_height(view);
		webkit_dom_dom_window_scroll_by(view,
		                                0, (wh / 100) * msg[2]);
		break;
	}

	return TRUE;
}

static void
webpagecreated(WebKitWebExtension *e, WebKitWebPage *wp, gpointer unused)
{
	Page *p = newpage(wp);
}

G_MODULE_EXPORT void
webkit_web_extension_initialize_with_user_data(WebKitWebExtension *e, GVariant *gv)
{
	GIOChannel *gchansock;

	g_signal_connect(e, "page-created", G_CALLBACK(webpagecreated), NULL);

	g_variant_get(gv, "i", &sock);

	gchansock = g_io_channel_unix_new(sock);
	g_io_channel_set_encoding(gchansock, NULL, NULL);
	g_io_channel_set_flags(gchansock, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_close_on_unref(gchansock, TRUE);
	g_io_add_watch(gchansock, G_IO_IN, readsock, NULL);
}

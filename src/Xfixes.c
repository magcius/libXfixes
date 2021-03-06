/*
 *
 * Copyright © 2002 Keith Packard, member of The XFree86 Project, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xfixesint.h"

#define ENQUEUE_EVENT	True
#define DONT_ENQUEUE	False

XFixesExtInfo XFixesExtensionInfo;
char XFixesExtensionName[] = XFIXES_NAME;

static int
XFixesCloseDisplay (Display *dpy, XExtCodes *codes);

static Bool
XFixesWireToEvent(Display *dpy, XEvent *event, xEvent *wire);

static Status
XFixesEventToWire(Display *dpy, XEvent *event, xEvent *wire);

static Bool
XFixesWireToCookie(Display *dpy, XGenericEventCookie *cookie, xEvent *wire);

static Bool
XFixesCopyCookie(Display *dpy, XGenericEventCookie *in, XGenericEventCookie *out);

static Bool
wireToBarrierEvent(xXFixesBarrierNotifyEvent *in, XGenericEventCookie *out);

static Bool
copyBarrierEvent(XGenericEventCookie *in, XGenericEventCookie *out);

/*
 * XFixesExtAddDisplay - add a display to this extension. (Replaces
 * XextAddDisplay)
 */
static XFixesExtDisplayInfo *
XFixesExtAddDisplay (XFixesExtInfo *extinfo,
                      Display        *dpy,
                      char           *ext_name)
{
    XFixesExtDisplayInfo    *info;
    int			    ev;

    info = (XFixesExtDisplayInfo *) Xmalloc (sizeof (XFixesExtDisplayInfo));
    if (!info) return NULL;
    info->display = dpy;

    info->codes = XInitExtension (dpy, ext_name);

    /*
     * if the server has the extension, then we can initialize the
     * appropriate function vectors
     */
    if (info->codes) {
	xXFixesQueryVersionReply	rep;
	xXFixesQueryVersionReq	*req;
        XESetCloseDisplay (dpy, info->codes->extension,
                           XFixesCloseDisplay);
	for (ev = info->codes->first_event;
	     ev < info->codes->first_event + XFixesNumberEvents;
	     ev++)
	{
	    XESetWireToEvent (dpy, ev, XFixesWireToEvent);
	    XESetEventToWire (dpy, ev, XFixesEventToWire);
	}

        XESetWireToEventCookie(dpy, info->codes->major_opcode, XFixesWireToCookie);
        XESetCopyEventCookie(dpy, info->codes->major_opcode, XFixesCopyCookie);

	/*
	 * Get the version info
	 */
	LockDisplay (dpy);
	GetReq (XFixesQueryVersion, req);
	req->reqType = info->codes->major_opcode;
	req->xfixesReqType = X_XFixesQueryVersion;
	req->majorVersion = XFIXES_MAJOR;
	req->minorVersion = XFIXES_MINOR;
	if (!_XReply (dpy, (xReply *) &rep, 0, xTrue))
	{
	    UnlockDisplay (dpy);
	    SyncHandle ();
	    Xfree(info);
	    return NULL;
	}
	info->major_version = rep.majorVersion;
	info->minor_version = rep.minorVersion;
	UnlockDisplay (dpy);
	SyncHandle ();
    } else {
	/* The server doesn't have this extension.
	 * Use a private Xlib-internal extension to hang the close_display
	 * hook on so that the "cache" (extinfo->cur) is properly cleaned.
	 * (XBUG 7955)
	 */
	XExtCodes *codes = XAddExtension(dpy);
	if (!codes) {
	    XFree(info);
	    return NULL;
	}
        XESetCloseDisplay (dpy, codes->extension, XFixesCloseDisplay);
    }

    /*
     * now, chain it onto the list
     */
    _XLockMutex(_Xglobal_lock);
    info->next = extinfo->head;
    extinfo->head = info;
    extinfo->cur = info;
    extinfo->ndisplays++;
    _XUnlockMutex(_Xglobal_lock);
    return info;
}


/*
 * XFixesExtRemoveDisplay - remove the indicated display from the
 * extension object. (Replaces XextRemoveDisplay.)
 */
static int
XFixesExtRemoveDisplay (XFixesExtInfo *extinfo, Display *dpy)
{
    XFixesExtDisplayInfo *info, *prev;

    /*
     * locate this display and its back link so that it can be removed
     */
    _XLockMutex(_Xglobal_lock);
    prev = NULL;
    for (info = extinfo->head; info; info = info->next) {
	if (info->display == dpy) break;
	prev = info;
    }
    if (!info) {
	_XUnlockMutex(_Xglobal_lock);
	return 0;		/* hmm, actually an error */
    }

    /*
     * remove the display from the list; handles going to zero
     */
    if (prev)
	prev->next = info->next;
    else
	extinfo->head = info->next;

    extinfo->ndisplays--;
    if (info == extinfo->cur) extinfo->cur = NULL;  /* flush cache */
    _XUnlockMutex(_Xglobal_lock);

    Xfree ((char *) info);
    return 1;
}

/*
 * XFixesExtFindDisplay - look for a display in this extension; keeps a
 * cache of the most-recently used for efficiency. (Replaces
 * XextFindDisplay.)
 */
static XFixesExtDisplayInfo *
XFixesExtFindDisplay (XFixesExtInfo *extinfo,
		      Display	    *dpy)
{
    XFixesExtDisplayInfo *info;

    /*
     * see if this was the most recently accessed display
     */
    if ((info = extinfo->cur) && info->display == dpy)
	return info;

    /*
     * look for display in list
     */
    _XLockMutex(_Xglobal_lock);
    for (info = extinfo->head; info; info = info->next) {
	if (info->display == dpy) {
	    extinfo->cur = info;     /* cache most recently used */
	    _XUnlockMutex(_Xglobal_lock);
	    return info;
	}
    }
    _XUnlockMutex(_Xglobal_lock);

    return NULL;
}

XFixesExtDisplayInfo *
XFixesFindDisplay (Display *dpy)
{
    XFixesExtDisplayInfo *info;

    info = XFixesExtFindDisplay (&XFixesExtensionInfo, dpy);
    if (!info)
	info = XFixesExtAddDisplay (&XFixesExtensionInfo, dpy,
				    XFixesExtensionName);
    return info;
}

static int
XFixesCloseDisplay (Display *dpy, XExtCodes *codes)
{
    return XFixesExtRemoveDisplay (&XFixesExtensionInfo, dpy);
}

static Bool
XFixesWireToEvent(Display *dpy, XEvent *event, xEvent *wire)
{
    XFixesExtDisplayInfo *info = XFixesFindDisplay(dpy);

    XFixesCheckExtension(dpy, info, False);

    switch ((wire->u.u.type & 0x7F) - info->codes->first_event)
    {
    case XFixesSelectionNotify: {
	XFixesSelectionNotifyEvent *aevent;
	xXFixesSelectionNotifyEvent *awire;
	awire = (xXFixesSelectionNotifyEvent *) wire;
	aevent = (XFixesSelectionNotifyEvent *) event;
	aevent->type = awire->type & 0x7F;
	aevent->subtype = awire->subtype;
	aevent->serial = _XSetLastRequestRead(dpy,
					      (xGenericReply *) wire);
	aevent->send_event = (awire->type & 0x80) != 0;
	aevent->display = dpy;
	aevent->window = awire->window;
	aevent->owner = awire->owner;
	aevent->selection = awire->selection;
	aevent->timestamp = awire->timestamp;
	aevent->selection_timestamp = awire->selectionTimestamp;
	return True;
    }
    case XFixesCursorNotify: {
	XFixesCursorNotifyEvent *aevent;
	xXFixesCursorNotifyEvent *awire;
	awire = (xXFixesCursorNotifyEvent *) wire;
	aevent = (XFixesCursorNotifyEvent *) event;
	aevent->type = awire->type & 0x7F;
	aevent->subtype = awire->subtype;
	aevent->serial = _XSetLastRequestRead(dpy,
					      (xGenericReply *) wire);
	aevent->send_event = (awire->type & 0x80) != 0;
	aevent->display = dpy;
	aevent->window = awire->window;
	aevent->cursor_serial = awire->cursorSerial;
	aevent->timestamp = awire->timestamp;
	aevent->cursor_name = awire->name;
	return True;
    }
    }
    return False;
}

static Bool
XFixesWireToCookie(Display *dpy, XGenericEventCookie *cookie, xEvent *event)
{
    XFixesExtDisplayInfo *info = XFixesFindDisplay(dpy);
    xGenericEvent* ge = (xGenericEvent*)event;

    if (ge->extension != info->codes->major_opcode) {
        printf("XFixesWireToCookie: wrong extension opcode %d\n",
               ge->extension);
        return DONT_ENQUEUE;
    }

    cookie->type = event->u.u.type;
    cookie->serial = _XSetLastRequestRead(dpy, (xGenericReply *) event);
    cookie->display = dpy;
    cookie->send_event = ((event->u.u.type & 0x80) != 0);
    cookie->evtype = ge->evtype;
    cookie->extension = ge->extension;

    switch (ge->evtype)
    {
    case XFixesBarrierNotify: {
        if (!wireToBarrierEvent((xXFixesBarrierNotifyEvent*)event, cookie)) {
            printf("XFixesWireToCookie: CONVERSION FAILURE!  evtype=%d\n",
                   ge->evtype);
            break;
        }
        return ENQUEUE_EVENT;
    }
    default:
        printf("XFixesWireToCookie: Unknown generic event. type %d\n", ge->evtype);
    }
    return DONT_ENQUEUE;
}

static Bool
XFixesCopyCookie(Display *dpy, XGenericEventCookie *in, XGenericEventCookie *out)
{
    int ret = True;
    XFixesExtDisplayInfo *info = XFixesFindDisplay(dpy);

    if (in->extension != info->codes->major_opcode) {
        printf("XFixesCopyCookie: wrong extension opcode %d\n",
                in->extension);
        return False;
    }

    *out = *in;
    out->data = NULL;
    out->cookie = 0;

    switch(in->evtype) {
    case XFixesBarrierNotify:
        ret = copyBarrierEvent(in, out);
        break;
    default:
        printf("XFixesWireToCookie: Unknown generic event. type %d\n", in->evtype);
        ret = False;
    }

    if (!ret)
        printf("XFixesCopyCookie: Failed to copy evtype %d", in->evtype);
    return ret;
}

#define FP3232_TO_DOUBLE(x) ((double) (x).integral + (x).frac / (1 << 16) / (1 << 16))

static Bool
wireToBarrierEvent(xXFixesBarrierNotifyEvent *in, XGenericEventCookie *cookie)
{
    XFixesBarrierNotifyEvent *out;

    out = cookie->data = calloc(1, sizeof(XFixesBarrierNotifyEvent));

    out->display = cookie->display;
    out->type = in->type;
    out->extension = in->extension;
    out->evtype = in->evtype;

    out->x = in->x;
    out->y = in->y;

    out->dx = FP3232_TO_DOUBLE (in->dx);
    out->dy = FP3232_TO_DOUBLE (in->dy);
    out->raw_dx = FP3232_TO_DOUBLE (in->raw_dx);
    out->raw_dy = FP3232_TO_DOUBLE (in->raw_dy);
    out->dt = in->dt;
    out->barrier = in->barrier;
    out->event_id = in->event_id;
    out->event_type = in->event_type;
    out->timestamp = in->timestamp;

    return True;
}

static Bool
copyBarrierEvent(XGenericEventCookie *in_cookie,
                 XGenericEventCookie *out_cookie)
{
    XFixesBarrierNotifyEvent *in, *out;

    in = in_cookie->data;

    out = out_cookie->data = calloc(1, sizeof(XFixesBarrierNotifyEvent));
    if (!out)
        return False;
    *out = *in;

    return True;
}

static Status
XFixesEventToWire(Display *dpy, XEvent *event, xEvent *wire)
{
    XFixesExtDisplayInfo *info = XFixesFindDisplay(dpy);

    XFixesCheckExtension(dpy, info, False);

    switch ((event->type & 0x7F) - info->codes->first_event)
    {
    case XFixesSelectionNotify: {
	XFixesSelectionNotifyEvent *aevent;
	xXFixesSelectionNotifyEvent *awire;
	awire = (xXFixesSelectionNotifyEvent *) wire;
	aevent = (XFixesSelectionNotifyEvent *) event;
	awire->type = aevent->type | (aevent->send_event ? 0x80 : 0);
	awire->subtype = aevent->subtype;
	awire->window = aevent->window;
	awire->owner = aevent->owner;
	awire->selection = aevent->selection;
	awire->timestamp = aevent->timestamp;
	awire->selectionTimestamp = aevent->selection_timestamp;
	return True;
    }
    case XFixesCursorNotify: {
	XFixesCursorNotifyEvent *aevent;
	xXFixesCursorNotifyEvent *awire;
	awire = (xXFixesCursorNotifyEvent *) wire;
	aevent = (XFixesCursorNotifyEvent *) event;
	awire->type = aevent->type | (aevent->send_event ? 0x80 : 0);
	awire->subtype = aevent->subtype;
	awire->window = aevent->window;
	awire->timestamp = aevent->timestamp;
	awire->cursorSerial = aevent->cursor_serial;
	awire->name = aevent->cursor_name;
    }
    }
    return False;
}

Bool
XFixesQueryExtension (Display *dpy,
			int *event_base_return,
			int *error_base_return)
{
    XFixesExtDisplayInfo *info = XFixesFindDisplay (dpy);

    if (XFixesHasExtension(info))
    {
	*event_base_return = info->codes->first_event;
	*error_base_return = info->codes->first_error;
	return True;
    }
    else
	return False;
}

Status
XFixesQueryVersion (Display *dpy,
		    int	    *major_version_return,
		    int	    *minor_version_return)
{
    XFixesExtDisplayInfo	*info = XFixesFindDisplay (dpy);

    XFixesCheckExtension (dpy, info, 0);

    *major_version_return = info->major_version;
    *minor_version_return = info->minor_version;
    return 1;
}

int
XFixesVersion (void)
{
    return XFIXES_VERSION;
}

// 12 april 2015
#include "uipriv_windows.h"

struct tab {
	uiTab t;
	HWND hwnd;
	struct ptrArray *pages;
	void (*baseResize)(uiControl *, intmax_t, intmax_t, intmax_t, intmax_t, uiSizing *);
	void (*baseEnable)(uiControl *);
	void (*baseDisable)(uiControl *);
	void (*baseSysFunc)(uiControl *, uiControlSysFuncParams *);
};

// from http://msdn.microsoft.com/en-us/library/windows/desktop/bb226818%28v=vs.85%29.aspx
#define tabMargin 7

static void tabPreferredSize(uiControl *c, uiSizing *d, intmax_t *width, intmax_t *height)
{
	struct tab *t = (struct tab *) c;
	LRESULT current;
	struct tabPage *curpage;
	intmax_t curwid, curht;
	RECT r;

	r.left = 0;
	r.top = 0;
	r.right = 0;
	r.bottom = 0;
	if (t->pages->len != 0) {
		current = SendMessageW(t->hwnd, TCM_GETCURSEL, 0, 0);
		if (current != (LRESULT) (-1)) {
			curpage = ptrArrayIndex(t->pages, struct tabPage *, current);
			uiControlPreferredSize(curpage->control, d, &curwid, &curht);
			r.right = curwid;
			r.bottom = curht;
			// TODO add margins
		}
	}
	// otherwise just use the rect [0 0 0 0]
	// the following will take the tabs themselves into account
	SendMessageW(t->hwnd, TCM_ADJUSTRECT, (WPARAM) TRUE, (LPARAM) (&r));
	*width = r.right - r.left;
	*height = r.bottom - r.top;
}

static void tabSysFunc(uiControl *c, uiControlSysFuncParams *p)
{
	struct tab *t = (struct tab *) c;
	struct tabPage *page;
	uintmax_t i;

	// we handle tab stops specially
	if (p->Func == uiWindowsSysFuncHasTabStops) {
		// if disabled, not a tab stop
		if (IsWindowEnabled(t->hwnd) != 0)
			// if there are no tabs, it is not a tab stop
			if (t->pages->len != 0)
				p->HasTabStops = TRUE;
		return;
	}
	// otherwise distribute it throughout all pages
	(*(t->baseSysFunc))(uiControl(t), p);
	for (i = 0; i < t->pages->len; i++) {
		page = ptrArrayIndex(t->pages, struct tabPage *, i);
		uiControlSysFunc(page->control, p);
	}
}

// this is also partially where tab navigation is handled
static LRESULT CALLBACK tabSubProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	struct tab *t = (struct tab *) dwRefData;
	RECT r;
	LRESULT n;
	uiControlSysFuncParams p;
	struct tabPage *page;

	switch (uMsg) {
	case msgHasTabStops:
		n = SendMessageW(t->hwnd, TCM_GETCURSEL, 0, 0);
		if (n == (LRESULT) (-1))		// no current selection == no tab stops
			return FALSE;
		p.Func = uiWindowsSysFuncHasTabStops;
		p.HasTabStops = FALSE;
		page = ptrArrayIndex(t->pages, struct tabPage *, n);
		uiControlSysFunc(page->control, &p);
		return p.HasTabStops;
	case WM_NCDESTROY:
		if (RemoveWindowSubclass(hwnd, tabSubProc, uIdSubclass) == FALSE)
			logLastError("error removing Tab resize handling subclass in tabSubProc()");
		break;
	}
	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

#define tabCapGrow 32

static void tabAppend(uiTab *tt, const char *name, uiControl *child)
{
	struct tab *t = (struct tab *) tt;
	TCITEMW item;
	LRESULT n;
	struct tabPage *page;
	WCHAR *wname;

	page = uiNew(struct tabPage);
	n = SendMessageW(t->hwnd, TCM_GETITEMCOUNT, 0, 0);

	page->control = child;
	uiControlSetParent(page->control, uiControl(t));
	if (n != 0)		// if this isn't the first page, we have to hide the other controls
		uiControlHide(page->control);

	ptrArrayAppend(t->pages, page);

	ZeroMemory(&item, sizeof (TCITEMW));
	item.mask = TCIF_TEXT;
	wname = toUTF16(name);
	item.pszText = wname;
	// MSDN's example code uses the first invalid index directly for this
	if (SendMessageW(t->hwnd, TCM_INSERTITEM, (WPARAM) n, (LPARAM) (&item)) == (LRESULT) -1)
		logLastError("error adding tab to Tab in uiTabAddPage()");
	uiFree(wname);

	// if this is the first tab, Windows will automatically show it /without/ sending a TCN_SELCHANGE notification
	// so we need to manually resize the tab ourselves
	// don't use uiContainerUpdate() for the same reason as in the TCN_SELCHANGE handler
	uiControlQueueResize(page->control);
}

static void tabInsertAt(uiTab *tt, const char *name, uintmax_t n, uiControl *child)
{
	struct tab *t = (struct tab *) tt;
	TCITEMW item;
	struct tabPage *page;
	WCHAR *wname;

	page = uiNew(struct tabPage);

	page->control = child;
	uiControlSetParent(page->control, uiControl(t));
	// always hide; the current tab doesn't change
	uiControlHide(page->control);

	ptrArrayInsertAt(t->pages, n, page);

	ZeroMemory(&item, sizeof (TCITEMW));
	item.mask = TCIF_TEXT;
	wname = toUTF16(name);
	item.pszText = wname;
	if (SendMessageW(t->hwnd, TCM_INSERTITEM, (WPARAM) n, (LPARAM) (&item)) == (LRESULT) -1)
		logLastError("error adding tab to Tab in uiTabInsertAt()");
	uiFree(wname);
}

static void tabDelete(uiTab *tt, uintmax_t n)
{
	struct tab *t = (struct tab *) tt;
	struct tabPage *page;

	// first delete the tab from the tab control
	// if this is the current tab, no tab will be selected, which is good
	if (SendMessageW(t->hwnd, TCM_DELETEITEM, (WPARAM) n, 0) == FALSE)
		logLastError("error deleting Tab page in tabDelete()");

	// now delete the page itself
	page = ptrArrayIndex(t->pages, struct tabPage *, n);
	ptrArrayDelete(t->pages, n);

	// and keep the page control alive
	uiControlSetParent(page->control, NULL);

	uiFree(page);
}

static uintmax_t tabNumPages(uiTab *tt)
{
	struct tab *t = (struct tab *) tt;

	return t->pages->len;
}

static int tabMargined(uiTab *tt, uintmax_t n)
{
	struct tab *t = (struct tab *) tt;
	struct tabPage *page;

	page = ptrArrayIndex(t->pages, struct tabPage *, n);
	return page->margined;
}

static void tabSetMargined(uiTab *tt, uintmax_t n, int margined)
{
	struct tab *t = (struct tab *) tt;
	struct tabPage *page;

	page = ptrArrayIndex(t->pages, struct tabPage *, n);
	page->margined = margined;
	uiControlQueueResize(page->control);
}

uiTab *uiNewTab(void)
{
	struct tab *t;
	uiWindowsMakeControlParams p;

	t = uiNew(struct tab);
	uiTyped(t)->Type = uiTypeTab();

	p.dwExStyle = 0;		// don't set WS_EX_CONTROLPARENT yet; we do that dynamically in the message loop (see main_windows.c)
	p.lpClassName = WC_TABCONTROLW;
	p.lpWindowName = L"";
	p.dwStyle = TCS_TOOLTIPS | WS_TABSTOP;		// start with this; we will alternate between this and WS_EX_CONTROLPARENT as needed (see main.c and msgHasTabStops above and the toggling functions below)
	p.hInstance = hInstance;
	p.lpParam = NULL;
	p.useStandardControlFont = TRUE;
	p.onWM_COMMAND = onWM_COMMAND;
	p.onWM_NOTIFY = onWM_NOTIFY;
	p.onDestroy = onDestroy;
	p.onDestroyData = t;
	uiWindowsMakeControl(uiControl(t), &p);

	t->hwnd = (HWND) uiControlHandle(uiControl(t));
	t->pages = newPtrArray();

	if (SetWindowSubclass(t->hwnd, tabSubProc, 0, (DWORD_PTR) t) == FALSE)
		logLastError("error subclassing Tab to give it its own resize handler in uiNewTab()");

	uiControl(t)->PreferredSize = tabPreferredSize;
	t->baseResize = uiControl(t)->Resize;
	uiControl(t)->Resize = tabResize;
	uiControl(t)->ComputeChildSize = tabComputeChildSize;
	t->baseEnable = uiControl(t)->Enable;
	uiControl(t)->Enable = tabEnable;
	t->baseDisable = uiControl(t)->Disable;
	uiControl(t)->Disable = tabDisable;
	// TODO ContainerEnable/ContainerDisable
	t->baseSysFunc = uiControl(t)->SysFunc;
	uiControl(t)->SysFunc = tabSysFunc;

	uiTab(t)->Append = tabAppend;
	uiTab(t)->InsertAt = tabInsertAt;
	uiTab(t)->Delete = tabDelete;
	uiTab(t)->NumPages = tabNumPages;
	uiTab(t)->Margined = tabMargined;
	uiTab(t)->SetMargined = tabSetMargined;

	return uiTab(t);
}

// unfortunately WS_TABSTOP and WS_EX_CONTROLPARENT are mutually exclusive, so we have to toggle between them
// see main.c for more details

void tabEnterTabNavigation(HWND hwnd)
{
	setStyle(hwnd, getStyle(hwnd) & ~WS_TABSTOP);
	setExStyle(hwnd, getExStyle(hwnd) | WS_EX_CONTROLPARENT);
}

void tabLeaveTabNavigation(HWND hwnd)
{
	setExStyle(hwnd, getExStyle(hwnd) & ~WS_EX_CONTROLPARENT);
	setStyle(hwnd, getStyle(hwnd) | WS_TABSTOP);
}

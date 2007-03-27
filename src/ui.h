/* File: ui.h */

/*
 * Copyright (c) 2007 Pete Mack and others
 * This code released under the Gnu Public License. See www.fsf.org
 * for current GPL license details. Addition permission granted to
 * incorporate modifications in all Angband variants as defined in the
 * Angband variants FAQ. See rec.games.roguelike.angband for FAQ.
 */


#ifndef UI_H
#define UI_H

/* ============= Constants ============ */


/* Colors for interactive menus */
enum {
	CURS_UNKNOWN = 0,
	CURS_KNOWN = 1
};
static const byte curs_attrs[2][2] =
{
		{TERM_SLATE, TERM_BLUE},		/* Greyed row */
		{TERM_WHITE, TERM_L_BLUE}		/* Valid row */
};

/* Standard menu orderings */
extern const char default_choice[];		/* 1234567890A-Za-z */
extern const char lower_case[];			/* abc..z */




/* ============= Defines a visual grouping ============ */
typedef struct
{
	byte tval;
	cptr name;
} grouper;


/* =================== Events =================== */

typedef struct event_action event_action;
typedef struct menu_item menu_item;
typedef struct menu_type menu_type;
typedef struct region region;
typedef struct menu_skin menu_skin;
typedef struct menu_class menu_class;

/*
 * Performs an action on object with an optional environment label 
 */
typedef void (*action_f)(void *object, const char *name);

/* 
 * Displays a single row in a menu
 * Driver function for populating menu rows on the fly
 */
typedef void (*display_row_f) (menu_type *menu, int oid,
							bool cursor, int row, int col, int width);

/*
 * Driver function for displaying (part of) a menu
 * Mostly internal, except for exotic single-screen menus like spellbooks
 * Arguments:
 *   db - internal data for display_row_f above.
 *   menuID - the global identifier for this menu. (For binding events pref files)
 *   object_list - an optional object reordering or subset.
 *   n - total number of elements in the menu.
 *   top - the current top of the displayed list.
 *   selections - optional list of characters to bind to the menu selections.
 *   cursor - the current cursor position.
 *   top - current menu top (for scrollable menus)
 *   row, col - screen location to root the list
 *   display_func - function for displaying a (short) label for menu item
 */
typedef void (*display_list_f) (
					menu_type *menu, const int object_list[], int n,
					int cursor, int *top,
					region *loc);


struct event_action
{
	int id;
	const char *name;
	action_f action;
	void *data;
};


struct menu_item
{
	event_action evt;
	char sel;
	int flags;
};


/* TODO: menu registry */
/*
  Together, these classes define the constant properties of
  the various menu classes.
  A menu consists of:
   - menu_class, which describes the basic
     class on which the menu is operating
      current classes are:
		MN_EVT- event_action array,
		MN_ACTION - menu_item array,
		MN_DBVIEW - general db iterator
 
    - a menu_skin, which describes the layout of the menu on the screen.
       current skins are: 
		MN_COLUMNS - all rows shown at once, in mult-column output.
		MN_SCROLL - only a limited part of the menu is shown, in a scrollable
					list
		MN_NATIVE - not implemented (OS menu)
	Menus also require data-dependent functions:

 */

 
struct menu_skin {
	int flag;
	int (*get_cursor)(int row, int col, int n, int top, region *loc);
	display_list_f display_list;
};

struct menu_class {
	int flag;
	char (*get_tag)(menu_type *menu, int oid);
	bool (*valid_row)(menu_type *menu, int oid);
	display_row_f display_row;
	bool (*handler)(char cmd, void *db, int oid);
};


typedef enum {
	/* Skins */
	/* TODO: skins are not flags. */
	MN_SCROLL	= 0x0000, /* Assumed -- scrollable list */
	MN_PAGE		= 0x0001, /* page view */
	MN_COLUMNS	= 0x0002, /* multicolumn view */
	MN_NATIVE	= 0x0003, /* Not implemented -- OS menu */
	MN_USER		= 0x0004, /* User-defined menu display -- browse spellbook, etc */
	MN_KEY_ONLY	= 0x0008, /* Fake "menu" for keyboard input */

	MN_MAX_SKIN = 0x00FF, /* Max number of skins */

	/* Appearance & behavior */
	MN_REL_TAGS	= 0x0100, /* Tags are associated with the view, not the element */
	MN_NO_TAGS	= 0x0200, /* No tags -- movement key and mouse browsing only */
	MN_PVT_TAGS = 0x0400, /* Tags to be generated by the display function */

	MN_DBL_TAP	= 0x1000, /* double tap for selection; single tap is cursor movement */
	MN_NO_ACT	= 0x2000, /* Do not invoke the specified action; menu selection only */
	MN_ONCE		= 0x4000, /* Only once through loop - detects motion */
	MN_NO_CURSOR = 0x8000, /* No cursor movement */

	/* Canned class implementations */
	/* TODO: this should be a separate enumeration */
	MN_ACT		= 0x10000, /* selectable menu with per-row flags (see below) */
	MN_EVT		= 0x20000, /* simple event action list */
	
	MN_CLASS    = 0xF0000 , /* Class filter */


	/* Reserved for rows in action_menu structure. */
	MN_DISABLED		= 0x0100000,
	MN_GRAYED		= 0x0200000,
	MN_SELECTED		= 0x0400000,
	MN_SELECTABLE	= 0x0800000
} menu_flags;


/* A menu defines either an action
 * or db row event
 */
struct menu_type
{
	int menuID;
	const char *title;
	const char *prompt;

	/* set of commands that may be performed on a menu item */
	const char *cmd_keys;

	/* Keyboard shortcuts for menu selection */
	/* IMPORTANT: this cannot intersect with cmd_keys */
	const char *selections; 

	/* Flags specifying the behavior of this menu. See enum MENU_FLAGS */
	int flags;
	int count;


	void *menu_data; /* the data used to access rows. */


	/* command action.  Should handle 0xff for selection */
	bool (*handler)(char cmd, void *db, int oid);

	int (*browse_hook)(int oid, region *loc);  /* auxiliary browser help function */

	/* These are "protected" - not visible for canned menu classes, */

	/* but required for rolling your own */
	/* Print a row */
	display_row_f display_label;

	/* optional tagger for this current class */
    char (*get_tag)(menu_type *menu, int oid);
	bool (*valid_row)(menu_type *menu, int cursor);

	/* helper functions for layout information. */
	const menu_skin *skin;  /* Defines menu appearance */
};



struct region {
	int col;			/* x-coordinate of upper right corner */
	int row;			/* y-coord of upper right coordinate */
	int width;			/* width of display area. 1 - use system default. */
						/* non-positive - rel to right of screen */
	int page_rows;	/* non-positive value is relative to the bottom of the screen */
};

/* 
 * Select a row from a menu.
 * 
 * Arguments:
 *  object_list - optional ordered subset of menu OID.  If NULL, cursor is used for OID
 *  cursor - current (absolute) position in menu.  Modified on selection.
 *  loc - location to display the menu.
 * Return: A command key; cursor is set to the corresponding row.
 * (This is a stand-in for a menu event)
 * reserved commands are 0xff for selection and ESCAPE for escape.
 */
key_event menu_select(menu_type *menu, const int object_list[], int n,
							int *cursor, region loc);


/* Helper function - display a menu, no interaction */
void menu_display(menu_type *menu, const int object_list[], int n, const int *cursor,
					region *loc);


/* Helper function - select an item from previously displayed menu */
void menu_choose(menu_type *menu, const int object_list[], int n, int *cursor,
					region *loc);


/* Helper function - perform menu action on currently selected item */
void menu_action(menu_type *menu, const int object_list[], int *cursor);


/* Set up structures for canned menu actions */
bool menu_init(menu_type *menu);



/* This is probably a bad idea */
static const region SCREEN = {0, 0, 0, 0};


#endif UI_H

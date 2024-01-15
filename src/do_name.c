/* NetHack 3.7	do_name.c	$NHDT-Date: 1625885761 2021/07/10 02:56:01 $  $NHDT-Branch: NetHack-3.7 $:$NHDT-Revision: 1.213 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Pasi Kallinen, 2018. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

static char *nextmbuf(void);
static void getpos_help_keyxhelp(winid, const char *, const char *, int);
static void getpos_help(boolean, const char *);
static int QSORTCALLBACK cmp_coord_distu(const void *, const void *);
static int gloc_filter_classify_glyph(int);
static int gloc_filter_floodfill_matcharea(int, int);
static void gloc_filter_floodfill(int, int);
static void gloc_filter_init(void);
static void gloc_filter_done(void);
static boolean gather_locs_interesting(int, int, int);
static void gather_locs(coord **, int *, int);
static void auto_describe(int, int);
static void truncate_to_map(int *, int *, schar, schar);
static void do_mgivenname(void);
static boolean alreadynamed(struct monst *, char *, char *);
static void do_oname(struct obj *);
static int name_ok(struct obj *);
static int call_ok(struct obj *);
static char *docall_xname(struct obj *);
static void namefloorobj(void);

extern const char what_is_an_unknown_object[]; /* from pager.c */

#define NUMMBUF 5

/* manage a pool of BUFSZ buffers, so callers don't have to */
static char *
nextmbuf(void)
{
    static char NEARDATA bufs[NUMMBUF][BUFSZ];
    static int bufidx = 0;

    bufidx = (bufidx + 1) % NUMMBUF;
    return bufs[bufidx];
}

/* function for getpos() to highlight desired map locations.
 * parameter value 0 = initialize, 1 = highlight, 2 = done
 */
static void (*getpos_hilitefunc)(int) = (void (*)(int)) 0;
static boolean (*getpos_getvalid)(int, int) = (boolean (*)(int, int)) 0;

void
getpos_sethilite(void (*gp_hilitef)(int), boolean (*gp_getvalidf)(int, int))
{
    getpos_hilitefunc = gp_hilitef;
    getpos_getvalid = gp_getvalidf;
}

static const char *const gloc_descr[NUM_GLOCS][4] = {
    { "any monsters", "monster", "next/previous monster", "monsters" },
    { "any items", "item", "next/previous object", "objects" },
    { "any doors", "door", "next/previous door or doorway",
      "doors or doorways" },
    { "any unexplored areas", "unexplored area", "unexplored location",
      "locations next to unexplored locations" },
    { "anything interesting", "interesting thing", "anything interesting",
      "anything interesting" },
    { "any valid locations", "valid location", "valid location",
      "valid locations" }
};

static const char *const gloc_filtertxt[NUM_GFILTER] = {
    "",
    " in view",
    " in this area"
};

static void
getpos_help_keyxhelp(winid tmpwin, const char *k1, const char *k2, int gloc)
{
    char sbuf[BUFSZ], fbuf[QBUFSZ];
    const char *move_cursor_to = "move the cursor to ",
               *filtertxt = gloc_filtertxt[iflags.getloc_filter];

    if (gloc == GLOC_EXPLORE) {
        /* default of "move to unexplored location" is inaccurate
           because the position will be one spot short of that */
        move_cursor_to = "move the cursor next to an ";
        if (iflags.getloc_usemenu)
            /* default is too wide for basic 80-column tty so shorten it
               to avoid wrapping */
            filtertxt = strsubst(strcpy(fbuf, filtertxt),
                                 "this area", "area");
    }
    Sprintf(sbuf, "Use '%s'/'%s' to %s%s%s.",
            k1, k2,
            iflags.getloc_usemenu ? "get a menu of " : move_cursor_to,
            gloc_descr[gloc][2 + iflags.getloc_usemenu], filtertxt);
    putstr(tmpwin, 0, sbuf);
}

DISABLE_WARNING_FORMAT_NONLITERAL

/* the response for '?' help request in getpos() */
static void
getpos_help(boolean force, const char *goal)
{
    static const char *const fastmovemode[2] = { "8 units at a time",
                                                 "skipping same glyphs" };
    char sbuf[BUFSZ];
    boolean doing_what_is;
    winid tmpwin = create_nhwindow(NHW_MENU);
    int runkey = iflags.num_pad ? NHKF_RUN2 : NHKF_RUN;
    int rushkey = iflags.num_pad ? NHKF_RUSH2 : NHKF_RUSH;

    Sprintf(sbuf,
            "Use '%s', '%s', '%s', '%s' to move the cursor to %s.", /* hjkl */
            visctrl(g.Cmd.move[DIR_W]), visctrl(g.Cmd.move[DIR_S]),
            visctrl(g.Cmd.move[DIR_N]), visctrl(g.Cmd.move[DIR_E]), goal);
    putstr(tmpwin, 0, sbuf);
    Sprintf(sbuf,
            "Use '%s', '%s', '%s', '%s' to fast-move the cursor, %s.",
            visctrl(g.Cmd.run[DIR_W]), visctrl(g.Cmd.run[DIR_S]),
            visctrl(g.Cmd.run[DIR_N]), visctrl(g.Cmd.run[DIR_E]),
            fastmovemode[iflags.getloc_moveskip]);
    putstr(tmpwin, 0, sbuf);
    Sprintf(sbuf, "(or prefix normal move with '%s' or '%s' to fast-move)",
            visctrl(g.Cmd.spkeys[runkey]), visctrl(g.Cmd.spkeys[rushkey]));
    putstr(tmpwin, 0, sbuf);
    putstr(tmpwin, 0, "Or enter a background symbol (ex. '<').");
    Sprintf(sbuf, "Use '%s' to move the cursor on yourself.",
           visctrl(g.Cmd.spkeys[NHKF_GETPOS_SELF]));
    putstr(tmpwin, 0, sbuf);
    if (!iflags.terrainmode || (iflags.terrainmode & TER_MON) != 0) {
        getpos_help_keyxhelp(tmpwin,
                             visctrl(g.Cmd.spkeys[NHKF_GETPOS_MON_NEXT]),
                             visctrl(g.Cmd.spkeys[NHKF_GETPOS_MON_PREV]),
                             GLOC_MONS);
    }
    if (!iflags.terrainmode || (iflags.terrainmode & TER_OBJ) != 0) {
        getpos_help_keyxhelp(tmpwin,
                             visctrl(g.Cmd.spkeys[NHKF_GETPOS_OBJ_NEXT]),
                             visctrl(g.Cmd.spkeys[NHKF_GETPOS_OBJ_PREV]),
                             GLOC_OBJS);
    }
    if (!iflags.terrainmode || (iflags.terrainmode & TER_MAP) != 0) {
        /* these are primarily useful when choosing a travel
           destination for the '_' command */
        getpos_help_keyxhelp(tmpwin,
                             visctrl(g.Cmd.spkeys[NHKF_GETPOS_DOOR_NEXT]),
                             visctrl(g.Cmd.spkeys[NHKF_GETPOS_DOOR_PREV]),
                             GLOC_DOOR);
        getpos_help_keyxhelp(tmpwin,
                             visctrl(g.Cmd.spkeys[NHKF_GETPOS_UNEX_NEXT]),
                             visctrl(g.Cmd.spkeys[NHKF_GETPOS_UNEX_PREV]),
                             GLOC_EXPLORE);
        getpos_help_keyxhelp(tmpwin,
                          visctrl(g.Cmd.spkeys[NHKF_GETPOS_INTERESTING_NEXT]),
                          visctrl(g.Cmd.spkeys[NHKF_GETPOS_INTERESTING_PREV]),
                             GLOC_INTERESTING);
    }
    Sprintf(sbuf, "Use '%s' to change fast-move mode to %s.",
            visctrl(g.Cmd.spkeys[NHKF_GETPOS_MOVESKIP]),
            fastmovemode[!iflags.getloc_moveskip]);
    putstr(tmpwin, 0, sbuf);
    if (!iflags.terrainmode || (iflags.terrainmode & TER_DETECT) == 0) {
        Sprintf(sbuf, "Use '%s' to toggle menu listing for possible targets.",
                visctrl(g.Cmd.spkeys[NHKF_GETPOS_MENU]));
        putstr(tmpwin, 0, sbuf);
        Sprintf(sbuf,
                "Use '%s' to change the mode of limiting possible targets.",
                visctrl(g.Cmd.spkeys[NHKF_GETPOS_LIMITVIEW]));
        putstr(tmpwin, 0, sbuf);
    }
    if (!iflags.terrainmode) {
        char kbuf[BUFSZ];

        if (getpos_getvalid) {
            Sprintf(sbuf, "Use '%s' or '%s' to move to valid locations.",
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_VALID_NEXT]),
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_VALID_PREV]));
            putstr(tmpwin, 0, sbuf);
        }
        if (getpos_hilitefunc) {
            Sprintf(sbuf, "Use '%s' to display valid locations.",
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_SHOWVALID]));
            putstr(tmpwin, 0, sbuf);
        }
        Sprintf(sbuf, "Use '%s' to toggle automatic description.",
                visctrl(g.Cmd.spkeys[NHKF_GETPOS_AUTODESC]));
        putstr(tmpwin, 0, sbuf);
        if (iflags.cmdassist) { /* assisting the '/' command, I suppose... */
            Sprintf(sbuf,
                    (iflags.getpos_coords == GPCOORDS_NONE)
         ? "(Set 'whatis_coord' option to include coordinates with '%s' text.)"
         : "(Reset 'whatis_coord' option to omit coordinates from '%s' text.)",
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_AUTODESC]));
        }
        /* disgusting hack; the alternate selection characters work for any
           getpos call, but only matter for dowhatis (and doquickwhatis) */
        doing_what_is = (goal == what_is_an_unknown_object);
        if (doing_what_is) {
            Sprintf(kbuf, "'%s' or '%s' or '%s' or '%s'",
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK]),
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK_Q]),
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK_O]),
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK_V]));
        } else {
            Sprintf(kbuf, "'%s'", visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK]));
        }
        Snprintf(sbuf, sizeof(sbuf),
                 "Type a %s when you are at the right place.", kbuf);
        putstr(tmpwin, 0, sbuf);
        if (doing_what_is) {
            Sprintf(sbuf,
       "  '%s' describe current spot, show 'more info', move to another spot.",
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK_V]));
            putstr(tmpwin, 0, sbuf);
            Sprintf(sbuf,
                    "  '%s' describe current spot,%s move to another spot;",
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK]),
                    flags.help ? " prompt if 'more info'," : "");
            putstr(tmpwin, 0, sbuf);
            Sprintf(sbuf,
                    "  '%s' describe current spot, move to another spot;",
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK_Q]));
            putstr(tmpwin, 0, sbuf);
            Sprintf(sbuf,
                    "  '%s' describe current spot, stop looking at things;",
                    visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK_O]));
            putstr(tmpwin, 0, sbuf);
        }
    }
    if (!force)
        putstr(tmpwin, 0, "Type Space or Escape when you're done.");
    putstr(tmpwin, 0, "");
    display_nhwindow(tmpwin, TRUE);
    destroy_nhwindow(tmpwin);
}

RESTORE_WARNING_FORMAT_NONLITERAL

static int QSORTCALLBACK
cmp_coord_distu(const void *a, const void *b)
{
    const coord *c1 = a;
    const coord *c2 = b;
    int dx, dy, dist_1, dist_2;

    dx = u.ux - c1->x;
    dy = u.uy - c1->y;
    dist_1 = max(abs(dx), abs(dy));
    dx = u.ux - c2->x;
    dy = u.uy - c2->y;
    dist_2 = max(abs(dx), abs(dy));

    if (dist_1 == dist_2)
        return (c1->y != c2->y) ? (c1->y - c2->y) : (c1->x - c2->x);

    return dist_1 - dist_2;
}

#define IS_UNEXPLORED_LOC(x,y) \
    (isok((x), (y))                                     \
     && glyph_is_unexplored(levl[(x)][(y)].glyph)   \
     && !levl[(x)][(y)].seenv)

#define GLOC_SAME_AREA(x,y)                                     \
    (isok((x), (y))                                             \
     && (selection_getpoint((x),(y), g.gloc_filter_map)))

static int
gloc_filter_classify_glyph(int glyph)
{
    int c;

    if (!glyph_is_cmap(glyph))
        return 0;

    c = glyph_to_cmap(glyph);

    if (is_cmap_room(c) || is_cmap_furniture(c))
        return 1;
    else if (is_cmap_wall(c) || c == S_tree)
        return 2;
    else if (is_cmap_corr(c))
        return 3;
    else if (is_cmap_water(c))
        return 4;
    else if (is_cmap_lava(c))
        return 5;
    return 0;
}

static int
gloc_filter_floodfill_matcharea(int x, int y)
{
    int glyph = back_to_glyph(x, y);

    if (!levl[x][y].seenv)
        return FALSE;

    if (glyph == g.gloc_filter_floodfill_match_glyph)
        return TRUE;

    if (gloc_filter_classify_glyph(glyph)
        == gloc_filter_classify_glyph(g.gloc_filter_floodfill_match_glyph))
        return TRUE;

    return FALSE;
}

static void
gloc_filter_floodfill(int x, int y)
{
    g.gloc_filter_floodfill_match_glyph = back_to_glyph(x, y);

    set_selection_floodfillchk(gloc_filter_floodfill_matcharea);
    selection_floodfill(g.gloc_filter_map, x, y, FALSE);
}

static void
gloc_filter_init(void)
{
    if (iflags.getloc_filter == GFILTER_AREA) {
        if (!g.gloc_filter_map) {
            g.gloc_filter_map = selection_new();
        }
        /* special case: if we're in a doorway, try to figure out which
           direction we're moving, and use that side of the doorway */
        if (IS_DOOR(levl[u.ux][u.uy].typ)) {
            if (u.dx || u.dy) {
                gloc_filter_floodfill(u.ux + u.dx, u.uy + u.dy);
            } else {
                /* TODO: maybe add both sides of the doorway? */
            }
        } else {
            gloc_filter_floodfill(u.ux, u.uy);
        }
    }
}

static void
gloc_filter_done(void)
{
    if (g.gloc_filter_map) {
        selection_free(g.gloc_filter_map, TRUE);
        g.gloc_filter_map = (struct selectionvar *) 0;

    }
}

DISABLE_WARNING_UNREACHABLE_CODE

static boolean
gather_locs_interesting(int x, int y, int gloc)
{
    int glyph, sym;

    if (iflags.getloc_filter == GFILTER_VIEW && !cansee(x, y))
        return FALSE;
    if (iflags.getloc_filter == GFILTER_AREA && !GLOC_SAME_AREA(x, y)
        && !GLOC_SAME_AREA(x - 1, y) && !GLOC_SAME_AREA(x, y - 1)
        && !GLOC_SAME_AREA(x + 1, y) && !GLOC_SAME_AREA(x, y + 1))
        return FALSE;

    glyph = glyph_at(x, y);
    sym = glyph_is_cmap(glyph) ? glyph_to_cmap(glyph) : -1;
    switch (gloc) {
    default:
    case GLOC_MONS:
        /* unlike '/M', this skips monsters revealed by
           warning glyphs and remembered unseen ones */
        return (glyph_is_monster(glyph)
                && glyph != monnum_to_glyph(PM_LONG_WORM_TAIL));
    case GLOC_OBJS:
        return (glyph_is_object(glyph)
                && glyph != objnum_to_glyph(BOULDER)
                && glyph != objnum_to_glyph(ROCK));
    case GLOC_DOOR:
        return (glyph_is_cmap(glyph)
                && (is_cmap_door(sym)
                    || is_cmap_drawbridge(sym)
                    || sym == S_ndoor));
    case GLOC_EXPLORE:
        return (glyph_is_cmap(glyph)
                && !glyph_is_nothing(glyph_to_cmap(glyph))
                && (is_cmap_door(sym)
                    || is_cmap_drawbridge(sym)
                    || sym == S_ndoor
                    || is_cmap_room(sym)
                    || is_cmap_corr(sym))
                && (IS_UNEXPLORED_LOC(x + 1, y)
                    || IS_UNEXPLORED_LOC(x - 1, y)
                    || IS_UNEXPLORED_LOC(x, y + 1)
                    || IS_UNEXPLORED_LOC(x, y - 1)));
    case GLOC_VALID:
        if (getpos_getvalid)
            return (*getpos_getvalid)(x, y);
        /*FALLTHRU*/
    case GLOC_INTERESTING:
        return (gather_locs_interesting(x, y, GLOC_DOOR)
                || !((glyph_is_cmap(glyph)
                      && (is_cmap_wall(sym)
                          || sym == S_tree
                          || sym == S_dead_tree
                          || sym == S_bars
                          || sym == S_ice
                          || sym == S_bridge
                          || sym == S_air
                          || sym == S_cloud
                          || is_cmap_lava(sym)
                          || is_cmap_water(sym)
                          || sym == S_ndoor
                          || is_cmap_room(sym)
                          || is_cmap_corr(sym)))
                     || glyph_is_nothing(glyph)
                     || glyph_is_unexplored(glyph)));
    }
    /*NOTREACHED*/
    return FALSE;
}

RESTORE_WARNINGS

/* gather locations for monsters or objects shown on the map */
static void
gather_locs(coord **arr_p, int *cnt_p, int gloc)
{
    int x, y, pass, idx;

    /*
     * We always include the hero's location even if there is no monster
     * (invisible hero without see invisible) or object (usual case)
     * displayed there.  That way, the count will always be at least 1,
     * and player has a visual indicator (cursor returns to hero's spot)
     * highlighting when successive 'm's or 'o's have cycled all the way
     * through all monsters or objects.
     *
     * Hero's spot will always sort to array[0] because it will always
     * be the shortest distance (namely, 0 units) away from <u.ux,u.uy>.
     */

    gloc_filter_init();

    *cnt_p = idx = 0;
    for (pass = 0; pass < 2; pass++) {
        for (x = 1; x < COLNO; x++)
            for (y = 0; y < ROWNO; y++) {
                if ((x == u.ux && y == u.uy)
                    || gather_locs_interesting(x, y, gloc)) {
                    if (!pass) {
                        ++*cnt_p;
                    } else {
                        (*arr_p)[idx].x = x;
                        (*arr_p)[idx].y = y;
                        ++idx;
                    }
                }
            }

        if (!pass) /* end of first pass */
            *arr_p = (coord *) alloc(*cnt_p * sizeof (coord));
        else /* end of second pass */
            qsort(*arr_p, *cnt_p, sizeof (coord), cmp_coord_distu);
    } /* pass */

    gloc_filter_done();
}

char *
dxdy_to_dist_descr(int dx, int dy, boolean fulldir)
{
    static char buf[30];
    int dst;

    if (!dx && !dy) {
        Sprintf(buf, "here");
    } else if ((dst = xytod(dx, dy)) != -1) {
        /* explicit direction; 'one step' is implicit */
        Sprintf(buf, "%s", directionname(dst));
    } else {
        static const char *dirnames[4][2] = {
            { "n", "north" },
            { "s", "south" },
            { "w", "west" },
            { "e", "east" } };
        buf[0] = '\0';
        /* 9999: protect buf[] against overflow caused by invalid values */
        if (dy) {
            if (abs(dy) > 9999)
                dy = sgn(dy) * 9999;
            Sprintf(eos(buf), "%d%s%s", abs(dy), dirnames[(dy > 0)][fulldir],
                    dx ? "," : "");
        }
        if (dx) {
            if (abs(dx) > 9999)
                dx = sgn(dx) * 9999;
            Sprintf(eos(buf), "%d%s", abs(dx),
                    dirnames[2 + (dx > 0)][fulldir]);
        }
    }
    return buf;
}

DISABLE_WARNING_FORMAT_NONLITERAL

/* coordinate formatting for 'whatis_coord' option */
char *
coord_desc(int x, int y, char *outbuf, char cmode)
{
    static char screen_fmt[16]; /* [12] suffices: "[%02d,%02d]" */
    int dx, dy;

    outbuf[0] = '\0';
    switch (cmode) {
    default:
        break;
    case GPCOORDS_COMFULL:
    case GPCOORDS_COMPASS:
        /* "east", "3s", "2n,4w" */
        dx = x - u.ux;
        dy = y - u.uy;
        Sprintf(outbuf, "(%s)",
                dxdy_to_dist_descr(dx, dy, cmode == GPCOORDS_COMFULL));
        break;
    case GPCOORDS_MAP: /* x,y */
        /* upper left corner of map is <1,0>;
           with default COLNO,ROWNO lower right corner is <79,20> */
        Sprintf(outbuf, "<%d,%d>", x, y);
        break;
    case GPCOORDS_SCREEN: /* y+2,x */
        /* for normal map sizes, force a fixed-width formatting so that
           /m, /M, /o, and /O output lines up cleanly; map sizes bigger
           than Nx999 or 999xM will still work, but not line up like normal
           when displayed in a column setting */
        if (!*screen_fmt)
            Sprintf(screen_fmt, "[%%%sd,%%%sd]",
                    (ROWNO - 1 + 2 < 100) ? "02" :  "03",
                    (COLNO - 1 < 100) ? "02" : "03");
        /* map line 0 is screen row 2;
           map column 0 isn't used, map column 1 is screen column 1 */
        Sprintf(outbuf, screen_fmt, y + 2, x);
        break;
    }
    return outbuf;
}

RESTORE_WARNING_FORMAT_NONLITERAL

static void
auto_describe(int cx, int cy)
{
    coord cc;
    int sym = 0;
    char tmpbuf[BUFSZ];
    const char *firstmatch = "unknown";

    cc.x = cx;
    cc.y = cy;
    if (do_screen_description(cc, TRUE, sym, tmpbuf, &firstmatch,
                              (struct permonst **) 0)) {
        (void) coord_desc(cx, cy, tmpbuf, iflags.getpos_coords);
        custompline((SUPPRESS_HISTORY | OVERRIDE_MSGTYPE),
                    "%s%s%s%s%s", firstmatch, *tmpbuf ? " " : "", tmpbuf,
                    (iflags.autodescribe
                     && getpos_getvalid && !(*getpos_getvalid)(cx, cy))
                      ? " (illegal)" : "",
                    (iflags.getloc_travelmode && !is_valid_travelpt(cx, cy))
                      ? " (no travel path)" : "");
        curs(WIN_MAP, cx, cy);
        flush_screen(0);
    }
}

boolean
getpos_menu(coord *ccp, int gloc)
{
    coord *garr = DUMMY;
    int gcount = 0;
    winid tmpwin;
    anything any;
    int i, pick_cnt;
    menu_item *picks = (menu_item *) 0;
    char tmpbuf[BUFSZ];

    gather_locs(&garr, &gcount, gloc);

    if (gcount < 2) { /* gcount always includes the hero */
        free((genericptr_t) garr);
        You("cannot %s %s.",
            (iflags.getloc_filter == GFILTER_VIEW) ? "see" : "detect",
            gloc_descr[gloc][0]);
        return FALSE;
    }

    tmpwin = create_nhwindow(NHW_MENU);
    start_menu(tmpwin, MENU_BEHAVE_STANDARD);
    any = cg.zeroany;

    /* gather_locs returns array[0] == you. skip it. */
    for (i = 1; i < gcount; i++) {
        char fullbuf[BUFSZ];
        coord tmpcc;
        const char *firstmatch = "unknown";
        int sym = 0;

        any.a_int = i + 1;
        tmpcc.x = garr[i].x;
        tmpcc.y = garr[i].y;
        if (do_screen_description(tmpcc, TRUE, sym, tmpbuf,
                              &firstmatch, (struct permonst **)0)) {
            (void) coord_desc(garr[i].x, garr[i].y, tmpbuf,
                              iflags.getpos_coords);
            Snprintf(fullbuf, sizeof fullbuf, "%s%s%s", firstmatch,
                    (*tmpbuf ? " " : ""), tmpbuf);
            add_menu(tmpwin, &nul_glyphinfo, &any, 0, 0,
                     ATR_NONE, fullbuf, MENU_ITEMFLAGS_NONE);
        }
    }

    Sprintf(tmpbuf, "Pick %s%s%s",
            an(gloc_descr[gloc][1]),
            gloc_filtertxt[iflags.getloc_filter],
            iflags.getloc_travelmode ? " for travel destination" : "");
    end_menu(tmpwin, tmpbuf);
    pick_cnt = select_menu(tmpwin, PICK_ONE, &picks);
    destroy_nhwindow(tmpwin);
    if (pick_cnt > 0) {
        ccp->x = garr[picks->item.a_int - 1].x;
        ccp->y = garr[picks->item.a_int - 1].y;
        free((genericptr_t) picks);
    }
    free((genericptr_t) garr);
    return (pick_cnt > 0);
}

/* add dx,dy to cx,cy, truncating at map edges */
static void
truncate_to_map(int *cx, int *cy, schar dx, schar dy)
{
    /* diagonal moves complicate this... */
    if (*cx + dx < 1) {
        dy -= sgn(dy) * (1 - (*cx + dx));
        dx = 1 - *cx; /* so that (cx+dx == 1) */
    } else if (*cx + dx > COLNO - 1) {
        dy += sgn(dy) * ((COLNO - 1) - (*cx + dx));
        dx = (COLNO - 1) - *cx;
    }
    if (*cy + dy < 0) {
        dx -= sgn(dx) * (0 - (*cy + dy));
        dy = 0 - *cy; /* so that (cy+dy == 0) */
    } else if (*cy + dy > ROWNO - 1) {
        dx += sgn(dx) * ((ROWNO - 1) - (*cy + dy));
        dy = (ROWNO - 1) - *cy;
    }
    *cx += dx;
    *cy += dy;
}

int
getpos(coord *ccp, boolean force, const char *goal)
{
    const char *cp;
    static struct {
        int nhkf, ret;
    } const pick_chars_def[] = {
        { NHKF_GETPOS_PICK, LOOK_ONCE },
        { NHKF_GETPOS_PICK_Q, LOOK_ONCE },
        { NHKF_GETPOS_PICK_O, LOOK_ONCE },
        { NHKF_GETPOS_PICK_V, LOOK_VERBOSE }
    };
    static const int mMoOdDxX_def[] = {
        NHKF_GETPOS_MON_NEXT,
        NHKF_GETPOS_MON_PREV,
        NHKF_GETPOS_OBJ_NEXT,
        NHKF_GETPOS_OBJ_PREV,
        NHKF_GETPOS_DOOR_NEXT,
        NHKF_GETPOS_DOOR_PREV,
        NHKF_GETPOS_UNEX_NEXT,
        NHKF_GETPOS_UNEX_PREV,
        NHKF_GETPOS_INTERESTING_NEXT,
        NHKF_GETPOS_INTERESTING_PREV,
        NHKF_GETPOS_VALID_NEXT,
        NHKF_GETPOS_VALID_PREV
    };
    char pick_chars[6];
    char mMoOdDxX[13];
    int result = 0;
    int cx, cy, i, c;
    int sidx, tx, ty;
    boolean msg_given = TRUE; /* clear message window by default */
    boolean show_goal_msg = FALSE;
    boolean hilite_state = FALSE;
    coord *garr[NUM_GLOCS] = DUMMY;
    int gcount[NUM_GLOCS] = DUMMY;
    int gidx[NUM_GLOCS] = DUMMY;
    schar udx = u.dx, udy = u.dy, udz = u.dz;
    int dx, dy;
    boolean rushrun = FALSE;
    int runkey = iflags.num_pad ? NHKF_RUN2 : NHKF_RUN;
    int rushkey = iflags.num_pad ? NHKF_RUSH2 : NHKF_RUSH;

    for (i = 0; i < SIZE(pick_chars_def); i++)
        pick_chars[i] = g.Cmd.spkeys[pick_chars_def[i].nhkf];
    pick_chars[SIZE(pick_chars_def)] = '\0';

    for (i = 0; i < SIZE(mMoOdDxX_def); i++)
        mMoOdDxX[i] = g.Cmd.spkeys[mMoOdDxX_def[i]];
    mMoOdDxX[SIZE(mMoOdDxX_def)] = '\0';

    if (!goal)
        goal = "desired location";
    if (flags.verbose) {
        pline("(For instructions type a '%s')",
              visctrl(g.Cmd.spkeys[NHKF_GETPOS_HELP]));
        msg_given = TRUE;
    }
    cx = ccp->x;
    cy = ccp->y;
#ifdef CLIPPING
    cliparound(cx, cy);
#endif
    curs(WIN_MAP, cx, cy);
    flush_screen(0);
#ifdef MAC
    lock_mouse_cursor(TRUE);
#endif
    for (;;) {
        if (show_goal_msg) {
            pline("Move cursor to %s:", goal);
            curs(WIN_MAP, cx, cy);
            flush_screen(0);
            show_goal_msg = FALSE;
        } else if (iflags.autodescribe && !msg_given && !hilite_state) {
            auto_describe(cx, cy);
        }

        rushrun = FALSE;

        g.program_state.getting_a_command = 1;
        c = readchar_poskey(&tx, &ty, &sidx);

        if (hilite_state) {
            (*getpos_hilitefunc)(2);
            hilite_state = FALSE;
            curs(WIN_MAP, cx, cy);
            flush_screen(0);
        }

        if (iflags.autodescribe)
            msg_given = FALSE;

        if (c == g.Cmd.spkeys[NHKF_ESC]) {
            cx = cy = -10;
            msg_given = TRUE; /* force clear */
            result = -1;
            break;
        }
        if (c == g.Cmd.spkeys[runkey] || c == g.Cmd.spkeys[rushkey]) {
            c = readchar_poskey(&tx, &ty, &sidx);
            rushrun = TRUE;
        }
        if (c == 0) {
            if (!isok(tx, ty))
                continue;
            /* a mouse click event, just assign and return */
            cx = tx;
            cy = ty;
            break;
        }
        if ((cp = index(pick_chars, c)) != 0) {
            /* '.' => 0, ',' => 1, ';' => 2, ':' => 3 */
            result = pick_chars_def[(int) (cp - pick_chars)].ret;
            break;
        } else if (movecmd(c, MV_WALK)) {
            if (rushrun)
                goto do_rushrun;
            dx = u.dx;
            dy = u.dy;
            truncate_to_map(&cx, &cy, dx, dy);
            goto nxtc;
        } else if (movecmd(c, MV_RUSH) || movecmd(c, MV_RUN)) {
 do_rushrun:
            if (iflags.getloc_moveskip) {
                /* skip same glyphs */
                int glyph = glyph_at(cx, cy);

                dx = u.dx;
                dy = u.dy;
                while (isok(cx + dx, cy + dy)
                       && glyph == glyph_at(cx + dx, cy + dy)
                       && isok(cx + dx + xdir[i], cy + dy + ydir[i])
                       && glyph == glyph_at(cx + dx + xdir[i],
                                            cy + dy + ydir[i])) {
                    dx += u.dx;
                    dy += u.dy;

                }
            } else {
                dx = 8 * u.dx;
                dy = 8 * u.dy;
            }
            truncate_to_map(&cx, &cy, dx, dy);
            goto nxtc;
        }

        if (c == g.Cmd.spkeys[NHKF_GETPOS_HELP] || redraw_cmd(c)) {
            if (c == g.Cmd.spkeys[NHKF_GETPOS_HELP])
                getpos_help(force, goal);
            else /* ^R */
                docrt(); /* redraw */
            /* update message window to reflect that we're still targetting */
            show_goal_msg = TRUE;
            msg_given = TRUE;
        } else if (c == g.Cmd.spkeys[NHKF_GETPOS_SHOWVALID]
                   && getpos_hilitefunc) {
            if (!hilite_state) {
                (*getpos_hilitefunc)(0);
                (*getpos_hilitefunc)(1);
                hilite_state = TRUE;
            }
            goto nxtc;
        } else if (c == g.Cmd.spkeys[NHKF_GETPOS_AUTODESC]) {
            iflags.autodescribe = !iflags.autodescribe;
            pline("Automatic description %sis %s.",
                  flags.verbose ? "of features under cursor " : "",
                  iflags.autodescribe ? "on" : "off");
            if (!iflags.autodescribe)
                show_goal_msg = TRUE;
            msg_given = TRUE;
            goto nxtc;
        } else if (c == g.Cmd.spkeys[NHKF_GETPOS_LIMITVIEW]) {
            static const char *const view_filters[NUM_GFILTER] = {
                "Not limiting targets",
                "Limiting targets to those in sight",
                "Limiting targets to those in same area"
            };

            iflags.getloc_filter = (iflags.getloc_filter + 1) % NUM_GFILTER;
            for (i = 0; i < NUM_GLOCS; i++) {
                if (garr[i]) {
                    free((genericptr_t) garr[i]);
                    garr[i] = NULL;
                }
                gidx[i] = gcount[i] = 0;
            }
            pline("%s.", view_filters[iflags.getloc_filter]);
            msg_given = TRUE;
            goto nxtc;
        } else if (c == g.Cmd.spkeys[NHKF_GETPOS_MENU]) {
            iflags.getloc_usemenu = !iflags.getloc_usemenu;
            pline("%s a menu to show possible targets%s.",
                  iflags.getloc_usemenu ? "Using" : "Not using",
                  iflags.getloc_usemenu
                      ? " for 'm|M', 'o|O', 'd|D', and 'x|X'" : "");
            msg_given = TRUE;
            goto nxtc;
        } else if (c == g.Cmd.spkeys[NHKF_GETPOS_SELF]) {
            /* reset 'm&M', 'o&O', &c; otherwise, there's no way for player
               to achieve that except by manually cycling through all spots */
            for (i = 0; i < NUM_GLOCS; i++)
                gidx[i] = 0;
            cx = u.ux;
            cy = u.uy;
            goto nxtc;
        } else if (c == g.Cmd.spkeys[NHKF_GETPOS_MOVESKIP]) {
            iflags.getloc_moveskip = !iflags.getloc_moveskip;
            pline("%skipping over similar terrain when fastmoving the cursor.",
                  iflags.getloc_moveskip ? "S" : "Not s");
            msg_given = TRUE;
            goto nxtc;
        } else if ((cp = index(mMoOdDxX, c)) != 0) { /* 'm|M', 'o|O', &c */
            /* nearest or farthest monster or object or door or unexplored */
            int gtmp = (int) (cp - mMoOdDxX), /* 0..7 */
                gloc = gtmp >> 1;             /* 0..3 */

            if (iflags.getloc_usemenu) {
                coord tmpcrd;

                if (getpos_menu(&tmpcrd, gloc)) {
                    cx = tmpcrd.x;
                    cy = tmpcrd.y;
                }
                goto nxtc;
            }

            if (!garr[gloc]) {
                gather_locs(&garr[gloc], &gcount[gloc], gloc);
                gidx[gloc] = 0; /* garr[][0] is hero's spot */
            }
            if (!(gtmp & 1)) {  /* c=='m' || c=='o' || c=='d' || c=='x') */
                gidx[gloc] = (gidx[gloc] + 1) % gcount[gloc];
            } else {            /* c=='M' || c=='O' || c=='D' || c=='X') */
                if (--gidx[gloc] < 0)
                    gidx[gloc] = gcount[gloc] - 1;
            }
            cx = garr[gloc][gidx[gloc]].x;
            cy = garr[gloc][gidx[gloc]].y;
            goto nxtc;
        } else {
            if (!index(quitchars, c)) {
                char matching[MAXPCHARS];
                int pass, lo_x, lo_y, hi_x, hi_y, k = 0;

                (void) memset((genericptr_t) matching, 0, sizeof matching);
                for (sidx = 1; sidx < MAXPCHARS; sidx++) { /* [0] left as 0 */
                    if (IS_DOOR(sidx) || IS_WALL(sidx)
                        || sidx == SDOOR || sidx == SCORR
                        || glyph_to_cmap(k) == S_room
                        || glyph_to_cmap(k) == S_darkroom
                        || glyph_to_cmap(k) == S_corr
                        || glyph_to_cmap(k) == S_litcorr)
                        continue;
                    if (c == defsyms[sidx].sym
                        || c == (int) g.showsyms[sidx]
                        /* have '^' match webs and vibrating square or any
                           other trap that uses something other than '^' */
                        || (c == '^' && (is_cmap_trap(sidx)
                                         || sidx == S_vibrating_square)))
                        matching[sidx] = (char) ++k;
                }
                if (k) {
                    for (pass = 0; pass <= 1; pass++) {
                        /* pass 0: just past current pos to lower right;
                           pass 1: upper left corner to current pos */
                        lo_y = (pass == 0) ? cy : 0;
                        hi_y = (pass == 0) ? ROWNO - 1 : cy;
                        for (ty = lo_y; ty <= hi_y; ty++) {
                            lo_x = (pass == 0 && ty == lo_y) ? cx + 1 : 1;
                            hi_x = (pass == 1 && ty == hi_y) ? cx : COLNO - 1;
                            for (tx = lo_x; tx <= hi_x; tx++) {
                                /* first, look at what is currently visible
                                   (might be monster) */
                                k = glyph_at(tx, ty);
                                if (glyph_is_cmap(k)
                                    && matching[glyph_to_cmap(k)])
                                    goto foundc;
                                /* next, try glyph that's remembered here
                                   (might be trap or object) */
                                if (g.level.flags.hero_memory
                                    /* !terrainmode: don't move to remembered
                                       trap or object if not currently shown */
                                    && !iflags.terrainmode) {
                                    k = levl[tx][ty].glyph;
                                    if (glyph_is_cmap(k)
                                        && matching[glyph_to_cmap(k)])
                                        goto foundc;
                                }
                                /* last, try actual terrain here (shouldn't
                                   we be using g.lastseentyp[][] instead?) */
                                if (levl[tx][ty].seenv) {
                                    k = back_to_glyph(tx, ty);
                                    if (glyph_is_cmap(k)
                                        && matching[glyph_to_cmap(k)])
                                        goto foundc;
                                }
                                continue;
                            foundc:
                                cx = tx, cy = ty;
                                if (msg_given) {
                                    clear_nhwindow(WIN_MESSAGE);
                                    msg_given = FALSE;
                                }
                                goto nxtc;
                            } /* column */
                        }     /* row */
                    }         /* pass */
                    pline("Can't find dungeon feature '%c'.", c);
                    msg_given = TRUE;
                    goto nxtc;
                } else {
                    char note[QBUFSZ];

                    if (!force)
                        Strcpy(note, "aborted");
                    else /* hjkl */
                        Sprintf(note, "use '%c', '%c', '%c', '%c' or '%s'",
                                g.Cmd.move[DIR_W], g.Cmd.move[DIR_S],
                                g.Cmd.move[DIR_N], g.Cmd.move[DIR_E],
                                visctrl(g.Cmd.spkeys[NHKF_GETPOS_PICK]));
                    pline("Unknown direction: '%s' (%s).", visctrl((char) c),
                          note);
                    msg_given = TRUE;
                } /* k => matching */
            }     /* !quitchars */
            if (force)
                goto nxtc;
            pline("Done.");
            msg_given = FALSE; /* suppress clear */
            cx = -1;
            cy = 0;
            result = 0; /* not -1 */
            break;
        }
    nxtc:
        ;
#ifdef CLIPPING
        cliparound(cx, cy);
#endif
        curs(WIN_MAP, cx, cy);
        flush_screen(0);
    }
#ifdef MAC
    lock_mouse_cursor(FALSE);
#endif
    if (msg_given)
        clear_nhwindow(WIN_MESSAGE);
    ccp->x = cx;
    ccp->y = cy;
    for (i = 0; i < NUM_GLOCS; i++)
        if (garr[i])
            free((genericptr_t) garr[i]);
    getpos_hilitefunc = (void (*)(int)) 0;
    getpos_getvalid = (boolean (*)(int, int)) 0;
    u.dx = udx, u.dy = udy, u.dz = udz;
    return result;
}

/* allocate space for a monster's name; removes old name if there is one */
void
new_mgivenname(struct monst *mon,
               int lth) /* desired length (caller handles adding 1
                           for terminator) */
{
    if (lth) {
        /* allocate mextra if necessary; otherwise get rid of old name */
        if (!mon->mextra)
            mon->mextra = newmextra();
        else
            free_mgivenname(mon); /* already has mextra, might also have name */
        MGIVENNAME(mon) = (char *) alloc((unsigned) lth);
    } else {
        /* zero length: the new name is empty; get rid of the old name */
        if (has_mgivenname(mon))
            free_mgivenname(mon);
    }
}

/* release a monster's name; retains mextra even if all fields are now null */
void
free_mgivenname(struct monst *mon)
{
    if (has_mgivenname(mon)) {
        free((genericptr_t) MGIVENNAME(mon));
        MGIVENNAME(mon) = (char *) 0;
    }
}

/* allocate space for an object's name; removes old name if there is one */
void
new_oname(struct obj *obj,
          int lth) /* desired length (caller handles adding 1
                      for terminator) */
{
    if (lth) {
        /* allocate oextra if necessary; otherwise get rid of old name */
        if (!obj->oextra)
            obj->oextra = newoextra();
        else
            free_oname(obj); /* already has oextra, might also have name */
        ONAME(obj) = (char *) alloc((unsigned) lth);
    } else {
        /* zero length: the new name is empty; get rid of the old name */
        if (has_oname(obj))
            free_oname(obj);
    }
}

/* release an object's name; retains oextra even if all fields are now null */
void
free_oname(struct obj *obj)
{
    if (has_oname(obj)) {
        free((genericptr_t) ONAME(obj));
        ONAME(obj) = (char *) 0;
    }
}

/*  safe_oname() always returns a valid pointer to
 *  a string, either the pointer to an object's name
 *  if it has one, or a pointer to an empty string
 *  if it doesn't.
 */
const char *
safe_oname(struct obj *obj)
{
    if (has_oname(obj))
        return ONAME(obj);
    return "";
}

/* historical note: this returns a monster pointer because it used to
   allocate a new bigger block of memory to hold the monster and its name */
struct monst *
christen_monst(struct monst *mtmp, const char *name)
{
    int lth;
    char buf[PL_PSIZ];

    /* g.dogname & g.catname are PL_PSIZ arrays; object names have same limit */
    lth = (name && *name) ? ((int) strlen(name) + 1) : 0;
    if (lth > PL_PSIZ) {
        lth = PL_PSIZ;
        name = strncpy(buf, name, PL_PSIZ - 1);
        buf[PL_PSIZ - 1] = '\0';
    }
    new_mgivenname(mtmp, lth); /* removes old name if one is present */
    if (lth)
        Strcpy(MGIVENNAME(mtmp), name);
    return mtmp;
}

/* check whether user-supplied name matches or nearly matches an unnameable
   monster's name; if so, give an alternate reject message for do_mgivenname() */
static boolean
alreadynamed(struct monst *mtmp, char *monnambuf, char *usrbuf)
{
    char pronounbuf[10], *p;

    if (fuzzymatch(usrbuf, monnambuf, " -_", TRUE)
        /* catch trying to name "the Oracle" as "Oracle" */
        || (!strncmpi(monnambuf, "the ", 4)
            && fuzzymatch(usrbuf, monnambuf + 4, " -_", TRUE))
        /* catch trying to name "invisible Orcus" as "Orcus" */
        || ((p = strstri(monnambuf, "invisible ")) != 0
            && fuzzymatch(usrbuf, p + 10, " -_", TRUE))
        /* catch trying to name "the {priest,Angel} of Crom" as "Crom" */
        || ((p = strstri(monnambuf, " of ")) != 0
            && fuzzymatch(usrbuf, p + 4, " -_", TRUE))) {
        pline("%s is already called %s.",
              upstart(strcpy(pronounbuf, mhe(mtmp))), monnambuf);
        return TRUE;
    } else if (mtmp->data == &mons[PM_JUIBLEX]
               && strstri(monnambuf, "Juiblex")
               && !strcmpi(usrbuf, "Jubilex")) {
        pline("%s doesn't like being called %s.", upstart(monnambuf), usrbuf);
        return TRUE;
    }
    return FALSE;
}

/* allow player to assign a name to some chosen monster */
static void
do_mgivenname(void)
{
    char buf[BUFSZ], monnambuf[BUFSZ], qbuf[QBUFSZ];
    coord cc;
    int cx, cy;
    struct monst *mtmp = 0;

    if (Hallucination) {
        You("would never recognize it anyway.");
        return;
    }
    cc.x = u.ux;
    cc.y = u.uy;
    if (getpos(&cc, FALSE, "the monster you want to name") < 0
        || !isok(cc.x, cc.y))
        return;
    cx = cc.x, cy = cc.y;

    if (cx == u.ux && cy == u.uy) {
        if (u.usteed && canspotmon(u.usteed)) {
            mtmp = u.usteed;
        } else {
            pline("This %s creature is called %s and cannot be renamed.",
                  beautiful(), g.plname);
            return;
        }
    } else
        mtmp = m_at(cx, cy);

    if (!mtmp
        || (!sensemon(mtmp)
            && (!(cansee(cx, cy) || see_with_infrared(mtmp))
                || mtmp->mundetected || M_AP_TYPE(mtmp) == M_AP_FURNITURE
                || M_AP_TYPE(mtmp) == M_AP_OBJECT
                || (mtmp->minvis && !See_invisible)))) {
        pline("I see no monster there.");
        return;
    }
    /* special case similar to the one in lookat() */
    Sprintf(qbuf, "What do you want to call %s?",
            distant_monnam(mtmp, ARTICLE_THE, monnambuf));
    buf[0] = '\0';
#ifdef EDIT_GETLIN
    /* if there's an existing name, make it be the default answer */
    if (has_mgivenname(mtmp))
        Strcpy(buf, MGIVENNAME(mtmp));
#endif
    getlin(qbuf, buf);
    if (!*buf || *buf == '\033')
        return;
    /* strip leading and trailing spaces; unnames monster if all spaces */
    (void) mungspaces(buf);

    /* Unique monsters have their own specific names or titles.
     * Shopkeepers, temple priests and other minions use alternate
     * name formatting routines which ignore any user-supplied name.
     *
     * Don't say the name is being rejected if it happens to match
     * the existing name.
     *
     * TODO: should have an alternate message when the attempt is to
     * remove existing name without assigning a new one.
     */
    if ((mtmp->data->geno & G_UNIQ) && !mtmp->ispriest) {
        if (!alreadynamed(mtmp, monnambuf, buf))
            pline("%s doesn't like being called names!", upstart(monnambuf));
    } else if (mtmp->isshk
               && !(Deaf || mtmp->msleeping || !mtmp->mcanmove
                    || mtmp->data->msound <= MS_ANIMAL)) {
        if (!alreadynamed(mtmp, monnambuf, buf))
            verbalize("I'm %s, not %s.", shkname(mtmp), buf);
    } else if (mtmp->ispriest || mtmp->isminion || mtmp->isshk
               || mtmp->data == &mons[PM_GHOST]) {
        if (!alreadynamed(mtmp, monnambuf, buf))
            pline("%s will not accept the name %s.", upstart(monnambuf), buf);
    } else
        (void) christen_monst(mtmp, buf);
}

/*
 * This routine used to change the address of 'obj' so be unsafe if not
 * used with extreme care.  Applying a name to an object no longer
 * allocates a replacement object, so that old risk is gone.
 */
static void
do_oname(register struct obj *obj)
{
    char *bufp, buf[BUFSZ], bufcpy[BUFSZ], qbuf[QBUFSZ];
    const char *aname;
    short objtyp;

    /* Do this now because there's no point in even asking for a name */
    if (obj->otyp == SPE_NOVEL) {
        pline("%s already has a published name.", Ysimple_name2(obj));
        return;
    }

    Sprintf(qbuf, "What do you want to name %s ",
            is_plural(obj) ? "these" : "this");
    (void) safe_qbuf(qbuf, qbuf, "?", obj, xname, simpleonames, "item");
    buf[0] = '\0';
#ifdef EDIT_GETLIN
    /* if there's an existing name, make it be the default answer */
    if (has_oname(obj))
        Strcpy(buf, ONAME(obj));
#endif
    getlin(qbuf, buf);
    if (!*buf || *buf == '\033')
        return;
    /* strip leading and trailing spaces; unnames item if all spaces */
    (void) mungspaces(buf);

    /*
     * We don't violate illiteracy conduct here, although it is
     * arguable that we should for anything other than "X".  Doing so
     * would make attaching player's notes to hero's inventory have an
     * in-game effect, which may or may not be the correct thing to do.
     *
     * We do violate illiteracy in oname() if player creates Sting or
     * Orcrist, clearly being literate (no pun intended...).
     */

    /* relax restrictions over proper capitalization for artifacts */
    if ((aname = artifact_name(buf, &objtyp)) != 0 && objtyp == obj->otyp)
        Strcpy(buf, aname);

    if (obj->oartifact) {
        pline_The("artifact seems to resist the attempt.");
        return;
    } else if (restrict_name(obj, buf) || exist_artifact(obj->otyp, buf)) {
        /* this used to change one letter, substituting a value
           of 'a' through 'y' (due to an off by one error, 'z'
           would never be selected) and then force that to
           upper case if such was the case of the input;
           now, the hand slip scuffs one or two letters as if
           the text had been trodden upon, sometimes picking
           punctuation instead of an arbitrary letter;
           unfortunately, we have to cover the possibility of
           it targetting spaces so failing to make any change
           (we know that it must eventually target a nonspace
           because buf[] matches a valid artifact name) */
        Strcpy(bufcpy, buf);
        /* for "the Foo of Bar", only scuff "Foo of Bar" part */
        bufp = !strncmpi(bufcpy, "the ", 4) ? (buf + 4) : buf;
        do {
            wipeout_text(bufp, rn2_on_display_rng(2), (unsigned) 0);
        } while (!strcmp(buf, bufcpy));
        pline("While engraving, your %s slips.", body_part(HAND));
        display_nhwindow(WIN_MESSAGE, FALSE);
        You("engrave: \"%s\".", buf);
        /* violate illiteracy conduct since hero attempted to write
           a valid artifact name */
        u.uconduct.literate++;
    }
    ++g.via_naming; /* This ought to be an argument rather than a static... */
    obj = oname(obj, buf);
    --g.via_naming; /* ...but oname() is used in a lot of places, so defer. */
}

/* Confer a grand-sounding name on a weapon. The weapon is assumed to be a
 * non-artifact and have a good enchantment or other good property. */
struct obj *
weapon_oname(struct obj *wpn)
{
    /* If we somehow get here without a weapon, instantly get out */
    if (!wpn)
        return wpn;
    /* Don't randomly name stacks. */
    if (wpn->quan > 1)
        return wpn;

    char basename[BUFSZ];
    int skill = weapon_type(wpn);
    if (skill == P_SABER)
        Strcpy(basename, "Sabre");
    else if (wpn->otyp == TWO_HANDED_SWORD)
        Strcpy(basename, "Claymore");
    else if (is_sword(wpn))
        Strcpy(basename, "Sword");
    else if (wpn->otyp == LUCERN_HAMMER)
        Strcpy(basename, "Lucern Hammer");
    else if (wpn->otyp == MORNING_STAR)
        Strcpy(basename, "Morning Star");
    else if (wpn->otyp == RUBBER_HOSE)
        Strcpy(basename, "Hose");
    else if ((skill == P_POLEARMS) || (skill == P_KNIFE)
             || (wpn->otyp == ATHAME))
        Strcpy(basename, OBJ_NAME(objects[wpn->otyp]));
    else
        Strcpy(basename, weapon_descr(wpn));

    /* Possible names. Any names with a %s in them must be sprintf'd with
     * basename. */
    const char* wpn_names[] = {
        "%s of Justice",  "%s of Honor",   "%s of Glory",    "%s of Revenge",
        "%s of Sorrow",   "%s of Yendor",  "%s of Victory",  "%s of Carnage",
        "%s of Serenity", "%s of Fable",   "%s of Legend",   "%s of Integrity",
        "%s of Redress",  "%s of Fate",    "%s of Punition", "%s of Reckoning",
        "%s of Omens",    "%s of Truth",   "%s of Virtue",   "%s of Bloodlust",
        "%s of Disaster", "%s of Torment", "%s of the Horde","%s of the Gods",
        "%s of Harm",     "%s of Mercy",   "%s of the Godless",
        "%s of the Peerless", "%s of the End", "%s of the Beginning",
        "%s of Protection",   "%s of the Infinite", "%s of Swift Defeat",
        "%s of the Planes",   "%s of Insanity", "%s of Death",
        "Righteous %s",   "Mighty %s",     "Unstoppable %s", "Unlimited %s",
        "Lucky %s",       "Unlucky %s",    "Hungry %s",      "Desecrated %s",
        "Death %s",       "Dudley's %s",   "Gilgamesh's %s", "Punished %s",
        "Chaos %s",       "Void %s",       "Hungry %s",      "Infinite %s",
        "Due Process",    "Puddingbane",
        "Newtsbane",      "Aggressive Negotiation",
        "Orphan Maker",   "Monster Slayer",
        "Astral Caller",   "Danger"
    };
    char buf[BUFSZ];
    if (!rn2(max(2, 25 - (2 * wpn->spe)))) {
        char nbuf[PL_NSIZ+1];
        const char* ttname = tt_name();
        if (ttname) {
            Strcpy(nbuf, ttname);
            Sprintf(buf, "%s of %s", upstart(basename), upstart(nbuf));
            return oname(wpn, buf);
        }
        /* if a name couldn't be found, fall through to default */
    }
    const char* name = wpn_names[rn2(SIZE(wpn_names))];
    if (strstri(name, "%s")) {
        Sprintf(buf, name, upstart(basename));
        return oname(wpn, buf);
    } else {
        return oname(wpn, name);
    }
}

struct obj *
oname(struct obj *obj, const char *name)
{
    int lth;
    char buf[PL_PSIZ];

    lth = *name ? (int) (strlen(name) + 1) : 0;
    if (lth > PL_PSIZ) {
        lth = PL_PSIZ;
        name = strncpy(buf, name, PL_PSIZ - 1);
        buf[PL_PSIZ - 1] = '\0';
    }
    /* If named artifact exists in the game, do not create another.
     * Also trying to create an artifact shouldn't de-artifact
     * it (e.g. Excalibur from prayer). In this case the object
     * will retain its current name. */
    if (obj->oartifact || (lth && exist_artifact(obj->otyp, name)))
        return obj;

    new_oname(obj, lth); /* removes old name if one is present */
    if (lth)
        Strcpy(ONAME(obj), name);

    if (lth)
        artifact_exists(obj, name, TRUE);
    if (obj->oartifact) {
        /* can't dual-wield with artifact as secondary weapon */
        if (obj == uswapwep)
            untwoweapon();
        /* activate warning if you've just named your weapon "Sting" */
        if (obj == uwep)
            set_artifact_intrinsic(obj, TRUE, W_WEP);
        /* if obj is owned by a shop, increase your bill */
        if (obj->unpaid)
            alter_cost(obj, 0L);
        if (g.via_naming) {
            /* violate illiteracy conduct since successfully wrote arti-name */
            if (!u.uconduct.literate++)
                livelog_printf(LL_CONDUCT | LL_ARTIFACT, "became literate by naming %s", bare_artifactname(obj));
            else
                livelog_printf(LL_ARTIFACT, "chose %s to be named \"%s\"", ansimpleoname(obj), bare_artifactname(obj));
        }
        /* set up specific materials for the artifact */
        switch(obj->oartifact) {
        case ART_END:
        case ART_WAR_S_SWORD:
            obj->material = BONE;
            break;
        case ART_CIRCE_S_WITCHSTAFF:
            obj->material = COPPER;
            break;
        case ART_TROLLSBANE:
        case ART_MORTALITY_DIAL:
            obj->material = COLD_IRON;
            break;
        case ART_SUNSWORD:
            obj->material = GEMSTONE;
            break;
        case ART_SHARUR:
        case ART_DRAGONBANE:
            obj->material = GOLD;
            break;
        case ART_LONGBOW_OF_DIANA:
        case ART_WEREBANE:
        case ART_DEMONBANE:
        case ART_GRAYSWANDIR:
        case ART_SWORD_OF_BALANCE:
        case ART_MITRE_OF_HOLINESS:
            obj->material = SILVER;
            break;
        case ART_YENDORIAN_EXPRESS_CARD:
            obj->material = PLATINUM;
            break;
        case ART_IRON_BALL_OF_LIBERATION:
            obj->material = IRON;
            break;
        /* Magicbane is of course made of the most magically conductive material. */
        case ART_MAGICBANE:
            obj->material = ORICHALCUM;
            break;
        default:
            /* prevent any wishes for materials on an artifact */
            obj->material = objects[obj->otyp].oc_material;
        }
    }
    if (carried(obj))
        update_inventory();
    return obj;
}

boolean
objtyp_is_callable(int i)
{
    if (objects[i].oc_uname)
        return TRUE;

    switch(objects[i].oc_class) {
    case SCROLL_CLASS:
    case POTION_CLASS:
    case WAND_CLASS:
    case RING_CLASS:
    case AMULET_CLASS:
    case GEM_CLASS:
    case SPBOOK_CLASS:
    case ARMOR_CLASS:
    case TOOL_CLASS:
    case VENOM_CLASS:
        if (OBJ_DESCR(objects[i]))
            return TRUE;
        break;
    default:
        break;
    }
    return FALSE;
}

/* getobj callback for object to name (specific item) - anything but gold */
static int
name_ok(struct obj *obj)
{
    if (!obj || obj->oclass == COIN_CLASS)
        return GETOBJ_EXCLUDE;

    return GETOBJ_SUGGEST;
}

/* getobj callback for object to call (name its type) */
static int
call_ok(struct obj *obj)
{
    if (!obj || !objtyp_is_callable(obj->otyp))
        return GETOBJ_EXCLUDE;

    return GETOBJ_SUGGEST;
}

/* C and #name commands - player can name monster or object or type of obj */
int
docallcmd(void)
{
    struct obj *obj;
    winid win;
    anything any;
    menu_item *pick_list = 0;
    char ch;
    /* if player wants a,b,c instead of i,o when looting, do that here too */
    boolean abc = flags.lootabc;

    win = create_nhwindow(NHW_MENU);
    start_menu(win, MENU_BEHAVE_STANDARD);
    any = cg.zeroany;
    any.a_char = 'm'; /* group accelerator 'C' */
    add_menu(win, &nul_glyphinfo, &any, abc ? 0 : any.a_char, 'C',
             ATR_NONE, "a monster", MENU_ITEMFLAGS_NONE);
    if (g.invent) {
        /* we use y and n as accelerators so that we can accept user's
           response keyed to old "name an individual object?" prompt */
        any.a_char = 'i'; /* group accelerator 'y' */
        add_menu(win, &nul_glyphinfo, &any, abc ? 0 : any.a_char, 'y',
                 ATR_NONE, "a particular object in inventory",
                 MENU_ITEMFLAGS_NONE);
        any.a_char = 'o'; /* group accelerator 'n' */
        add_menu(win, &nul_glyphinfo, &any, abc ? 0 : any.a_char, 'n',
                 ATR_NONE, "the type of an object in inventory",
                 MENU_ITEMFLAGS_NONE);
    }
    any.a_char = 'f'; /* group accelerator ',' (or ':' instead?) */
    add_menu(win, &nul_glyphinfo, &any, abc ? 0 : any.a_char, ',',
             ATR_NONE, "the type of an object upon the floor",
             MENU_ITEMFLAGS_NONE);
    any.a_char = 'd'; /* group accelerator '\' */
    add_menu(win, &nul_glyphinfo, &any, abc ? 0 : any.a_char, '\\',
             ATR_NONE, "the type of an object on discoveries list",
             MENU_ITEMFLAGS_NONE);
    any.a_char = 'a'; /* group accelerator 'l' */
    add_menu(win, &nul_glyphinfo, &any, abc ? 0 : any.a_char, 'l',
             ATR_NONE, "record an annotation for the current level",
             MENU_ITEMFLAGS_NONE);
    end_menu(win, "What do you want to name?");
    if (select_menu(win, PICK_ONE, &pick_list) > 0) {
        ch = pick_list[0].item.a_char;
        free((genericptr_t) pick_list);
    } else
        ch = 'q';
    destroy_nhwindow(win);

    switch (ch) {
    default:
    case 'q':
        break;
    case 'm': /* name a visible monster */
        do_mgivenname();
        break;
    case 'i': /* name an individual object in inventory */
        obj = getobj("name", name_ok, GETOBJ_PROMPT);
        if (obj)
            do_oname(obj);
        break;
    case 'o': /* name a type of object in inventory */
        obj = getobj("call", call_ok, GETOBJ_NOFLAGS);
        if (obj) {
            /* behave as if examining it in inventory;
               this might set dknown if it was picked up
               while blind and the hero can now see */
            (void) xname(obj);

            if (!obj->dknown) {
                You("would never recognize another one.");
#if 0
            } else if (!call_ok(obj)) {
                You("know those as well as you ever will.");
#endif
            } else {
                docall(obj);
            }
        }
        break;
    case 'f': /* name a type of object visible on the floor */
        namefloorobj();
        break;
    case 'd': /* name a type of object on the discoveries list */
        rename_disco();
        break;
    case 'a': /* annotate level */
        donamelevel();
        break;
    }
    return 0;
}

/* for use by safe_qbuf() */
static char *
docall_xname(struct obj *obj)
{
    struct obj otemp;

    otemp = *obj;
    otemp.oextra = (struct oextra *) 0;
    otemp.quan = 1L;
    /* in case water is already known, convert "[un]holy water" to "water" */
    otemp.blessed = otemp.cursed = 0;
    /* remove attributes that are doname() caliber but get formatted
       by xname(); most of these fixups aren't really needed because the
       relevant type of object isn't callable so won't reach this far */
    if (otemp.oclass == WEAPON_CLASS)
        otemp.opoisoned = 0; /* not poisoned */
    else if (otemp.oclass == POTION_CLASS)
        otemp.odiluted = 0; /* not diluted */
    else if (otemp.otyp == TOWEL || otemp.otyp == STATUE)
        otemp.spe = 0; /* not wet or historic */
    else if (otemp.otyp == TIN)
        otemp.known = 0; /* suppress tin type (homemade, &c) and mon type */
    else if (otemp.otyp == FIGURINE || otemp.otyp == MASK
            || otemp.oclass == SCROLL_CLASS)
        otemp.corpsenm = NON_PM; /* suppress mon type */
    else if (otemp.otyp == HEAVY_IRON_BALL)
        otemp.owt = objects[HEAVY_IRON_BALL].oc_weight; /* not "very heavy" */
    else if (otemp.oclass == FOOD_CLASS && otemp.globby)
        otemp.owt = 120; /* 6*20, neither a small glob nor a large one */

    return an(xname(&otemp));
}

void
docall(struct obj *obj)
{
    char buf[BUFSZ], qbuf[QBUFSZ];
    char **str1;

    if (!obj->dknown)
        return; /* probably blind */
    flush_screen(1); /* buffered updates might matter to player's response */

    if (obj->oclass == POTION_CLASS && obj->fromsink)
        /* kludge, meaning it's sink water */
        Sprintf(qbuf, "Call a stream of %s fluid:",
                OBJ_DESCR(objects[obj->otyp]));
    else
        (void) safe_qbuf(qbuf, "Call ", ":", obj,
                         docall_xname, simpleonames, "thing");
    /* pointer to old name */
    str1 = &(objects[obj->otyp].oc_uname);
    buf[0] = '\0';
#ifdef EDIT_GETLIN
    /* if there's an existing name, make it be the default answer */
    if (*str1)
        Strcpy(buf, *str1);
#endif
    getlin(qbuf, buf);
    if (!*buf || *buf == '\033')
        return;

    /* clear old name */
    if (*str1)
        free((genericptr_t) *str1);

    /* strip leading and trailing spaces; uncalls item if all spaces */
    (void) mungspaces(buf);
    if (!*buf) {
        if (*str1) { /* had name, so possibly remove from disco[] */
            /* strip name first, for the update_inventory() call
               from undiscover_object() */
            *str1 = (char *) 0;
            undiscover_object(obj->otyp);
        }
    } else {
        *str1 = dupstr(buf);
        discover_object(obj->otyp, FALSE, TRUE); /* possibly add to disco[] */
    }
}

static void
namefloorobj(void)
{
    coord cc;
    int glyph;
    char buf[BUFSZ];
    struct obj *obj = 0;
    boolean fakeobj = FALSE, use_plural;

    cc.x = u.ux, cc.y = u.uy;
    /* "dot for under/over you" only makes sense when the cursor hasn't
       been moved off the hero's '@' yet, but there's no way to adjust
       the help text once getpos() has started */
    Sprintf(buf, "object on map (or '.' for one %s you)",
            (u.uundetected && hides_under(g.youmonst.data)) ? "over" : "under");
    if (getpos(&cc, FALSE, buf) < 0 || cc.x <= 0)
        return;
    if (cc.x == u.ux && cc.y == u.uy) {
        obj = vobj_at(u.ux, u.uy);
    } else {
        glyph = glyph_at(cc.x, cc.y);
        if (glyph_is_object(glyph))
            fakeobj = object_from_map(glyph, cc.x, cc.y, &obj);
        /* else 'obj' stays null */
    }
    if (!obj) {
        /* "under you" is safe here since there's no object to hide under */
        pline("There doesn't seem to be any object %s.",
              (cc.x == u.ux && cc.y == u.uy) ? "under you" : "there");
        return;
    }
    /* note well: 'obj' might be an instance of STRANGE_OBJECT if target
       is a mimic; passing that to xname (directly or via simpleonames)
       would yield "glorkum" so we need to handle it explicitly; it will
       always fail the Hallucination test and pass the !callable test,
       resulting in the "can't be assigned a type name" message */
    Strcpy(buf, (obj->otyp != STRANGE_OBJECT)
                 ? simpleonames(obj)
                 : obj_descr[STRANGE_OBJECT].oc_name);
    use_plural = (obj->quan > 1L);
    if (Hallucination) {
        const char *unames[6];
        char tmpbuf[BUFSZ];

        /* straight role name */
        unames[0] = Upolyd ? rolename_gender(u.mfemale) : rolename_gender(flags.female);
        /* random rank title for hero's role

           note: the 30 is hardcoded in xlev_to_rank, so should be
           hardcoded here too */
        unames[1] = rank_of(rn2_on_display_rng(30) + 1,
                            Role_switch, flags.female);
        /* random fake monster */
        unames[2] = bogusmon(tmpbuf, (char *) 0);
        /* increased chance for fake monster */
        unames[3] = unames[2];
        /* traditional */
        unames[4] = roguename();
        /* silly */
        unames[5] = "Wibbly Wobbly";
        pline("%s %s to call you \"%s.\"",
              The(buf), use_plural ? "decide" : "decides",
              unames[rn2_on_display_rng(SIZE(unames))]);
    } else if (!call_ok(obj)) {
        pline("%s %s can't be assigned a type name.",
              use_plural ? "Those" : "That", buf);
    } else if (!obj->dknown) {
        You("don't know %s %s well enough to name %s.",
            use_plural ? "those" : "that", buf, use_plural ? "them" : "it");
    } else {
        docall(obj);
    }
    if (fakeobj) {
        obj->where = OBJ_FREE; /* object_from_map() sets it to OBJ_FLOOR */
        dealloc_obj(obj);
    }
}

static const char *const ghostnames[] = {
    /* these names should have length < PL_NSIZ */
    /* Capitalize the names for aesthetics -dgk */
    "Adri",    "Andries",       "Andreas",     "Bert",    "David",  "Dirk",
    "Emile",   "Frans",         "Fred",        "Greg",    "Hether", "Jay",
    "John",    "Jon",           "Karnov",      "Kay",     "Kenny",  "Kevin",
    "Maud",    "Michiel",       "Mike",        "Peter",   "Robert", "Ron",
    "Tom",     "Wilmar",        "Nick Danger", "Phoenix", "Jiro",   "Mizue",
    "Stephan", "Lance Braccus", "Shadowhawk"
};

static const char *const malhumnames[] = {
    "Samuel",    "John",   "Alejandro",  "Mickey",  "Bert",      "Ernie",
    "Larry",    "Curly",  "Shemp",      "Moe",      "Jake",      "Jackson",
    "Liam",     "Lucas",  "Oliver",     "Jayden",   "Sebastian", "Wyatt",
    "Connor",   "Owen",   "Ben",        "Levi",     "Hikaru",    "Timmy",
    "Johnny",   "Spike",  "Josiah",     "Justin",   "Erin",      "Sarah",
    "Jimbo",    "Bob",    "Shadow",     "Dimitri",  "Harry",     "Donald",
    "Jerry",    "Dale",   "Arthur",     "Drew",     "Neo",       "Leo",
    "Trevor",   "Pike",   "Barret",     "Red",      "Roy",       "Hugh",
    "Juan",
};

static const char *const femhumnames[] = {
    "Tiffany",  "Sally",    "Amanda",  "Jane",   "Erin",   "Emma",   "Olivia",
    "Sofia",    "Wendy",    "Astrid",  "Sylva",  "Terra",  "Kyrie",  "Savannah",
    "Kennedy",  "Autumn",   "Bella",   "Ivy",    "Freya",  "Kate",   "Zena",
    "Regina",   "Cosette",  "Chloe",   "Lyra",   "John",   "Alexa",  "Brooke",
    "Siri",     "Violet",   "Pell",    "Sam",    "Wanda",  "Lois",   "Bianca",
    "Trinity",  "Carmen",   "Megan",   "Sky",    "Rose",   "Roxy",   "Julie",
    "Fran",     "Frieda",   "Lana",    "Tanya",  "Sally",  "Rika",   "Sil",
    "Valeska",  "Yellow",   "Lucy",    "Patty",  "Celia",  "Erika",  "Kestrel"
};

/* ghost names formerly set by x_monnam(), now by makemon() instead */
const char *
rndghostname(void)
{
    return rn2(7) ? ghostnames[rn2(SIZE(ghostnames))] : (const char *) g.plname;
}

const char *
rndhumname(feminine)
boolean feminine;
{
    if (feminine)
        return femhumnames[rn2(SIZE(femhumnames))];
    return malhumnames[rn2(SIZE(malhumnames))];
}

static const char *const dragonages[] = {
    "young ", "very young", "juvenile ",
    "adult ", "mature ", "old ", "very old ", 
    "venerable ", "eternal ", "antediluvian ",
    "primeval "
};

/*
 * Monster naming functions:
 * x_monnam is the generic monster-naming function.
 *                seen        unseen       detected               named
 * mon_nam:     the newt        it      the invisible orc       Fido
 * noit_mon_nam:the newt (as if detected) the invisible orc     Fido
 * l_monnam:    newt            it      invisible orc           dog called Fido
 * Monnam:      The newt        It      The invisible orc       Fido
 * noit_Monnam: The newt (as if detected) The invisible orc     Fido
 * Adjmonnam:   The poor newt   It      The poor invisible orc  The poor Fido
 * Amonnam:     A newt          It      An invisible orc        Fido
 * a_monnam:    a newt          it      an invisible orc        Fido
 * m_monnam:    newt            xan     orc                     Fido
 * y_monnam:    your newt     your xan  your invisible orc      Fido
 * noname_monnam(mon,article):
 *              article newt    art xan art invisible orc       art dog
 */

/*
 * article
 *
 * ARTICLE_NONE, ARTICLE_THE, ARTICLE_A: obvious
 * ARTICLE_YOUR: "your" on pets, "the" on everything else
 *
 * If the monster would be referred to as "it" or if the monster has a name
 * _and_ there is no adjective, "invisible", "saddled", etc., override this
 * and always use no article.
 *
 * suppress
 *
 * SUPPRESS_IT, SUPPRESS_INVISIBLE, SUPPRESS_HALLUCINATION, SUPPRESS_SADDLE.
 * EXACT_NAME: combination of all the above
 * SUPPRESS_NAME: omit monster's assigned name (unless uniq w/ pname).
 *
 * Bug: if the monster is a priest or shopkeeper, not every one of these
 * options works, since those are special cases.
 */
char *
x_monnam(
    struct monst *mtmp,
    int article,
    const char *adjective,
    int suppress,
    boolean called)
{
    char *buf = nextmbuf();
    struct permonst *mdat = mtmp->data;
    const char *pm_name = mon_pmname(mtmp);
    const char *age_category;
    boolean do_hallu, do_invis, do_it, do_saddle, do_name;
    boolean name_at_start, has_adjectives,
            falseCap = (*pm_name != lowc(*pm_name));
    char *bp;

    if (g.program_state.gameover)
        suppress |= SUPPRESS_HALLUCINATION;
    if (article == ARTICLE_YOUR && !mtmp->mtame)
        article = ARTICLE_THE;

    do_hallu = Hallucination && !(suppress & SUPPRESS_HALLUCINATION);
    do_invis = mtmp->minvis && !(suppress & SUPPRESS_INVISIBLE);
    do_it = !canspotmon(mtmp) && article != ARTICLE_YOUR
            && !g.program_state.gameover && mtmp != u.usteed
            && !(u.uswallow && mtmp == u.ustuck) && !(suppress & SUPPRESS_IT);
    do_saddle = !(suppress & SUPPRESS_SADDLE);
    do_name = !(suppress & SUPPRESS_NAME) || type_is_pname(mdat);

    buf[0] = '\0';

    /* unseen monsters, etc.  Use "it" */
    if (do_it) {
        Strcpy(buf, "it");
        return buf;
    }

    /* priests and minions: don't even use this function */
    if (mtmp->ispriest || mtmp->isminion) {
        char priestnambuf[BUFSZ];
        char *name;
        long save_prop = EHalluc_resistance;
        unsigned save_invis = mtmp->minvis;

        /* when true name is wanted, explicitly block Hallucination */
        if (!do_hallu)
            EHalluc_resistance = 1L;
        if (!do_invis)
            mtmp->minvis = 0;
        name = priestname(mtmp, article, priestnambuf);
        EHalluc_resistance = save_prop;
        mtmp->minvis = save_invis;
        if (article == ARTICLE_NONE && !strncmp(name, "the ", 4))
            name += 4;
        return strcpy(buf, name);
    }
#if 0
    /* an "aligned priest" not flagged as a priest or minion should be
       "priest" or "priestess" (normally handled by priestname()) */
    if (mdat == &mons[PM_ALIGNED_CLERIC])
        pm_name = mtmp->female ? "priestess" : "priest";
    else if (mdat == &mons[PM_HIGH_CLERIC] && mtmp->female)
        pm_name = "high priestess";
#endif

    /* Shopkeepers: use shopkeeper name.  For normal shopkeepers, just
     * "Asidonhopo"; for unusual ones, "Asidonhopo the invisible
     * shopkeeper" or "Asidonhopo the blue dragon".  If hallucinating,
     * none of this applies.
     */
    if (mtmp->isshk && !do_hallu) {
        if (adjective && article == ARTICLE_THE) {
            /* pathological case: "the angry Asidonhopo the blue dragon"
               sounds silly */
            Strcpy(buf, "the ");
            Strcat(strcat(buf, adjective), " ");
            Strcat(buf, shkname(mtmp));
            return buf;
        }
        Strcat(buf, shkname(mtmp));
        if (is_shopkeeper(mdat) && !do_invis)
            return buf;
        Strcat(buf, " the ");
        if (do_invis)
            Strcat(buf, "invisible ");
        if (has_etemplate(mtmp))
            Sprintf(eos(buf), "%s ", montemplates[ETEMPLATE(mtmp)->template_index].pmnames[NEUTRAL]);
        Strcat(buf, pm_name);
        return buf;
    }

    /* Put the adjectives in the buffer */
    if (adjective)
        Strcat(strcat(buf, adjective), " ");
    if (do_invis)
        Strcat(buf, "invisible ");
    if (do_saddle && (mtmp->misc_worn_check & W_SADDLE) && !Blind
        && !Hallucination)
        Strcat(buf, "saddled ");
    if (monsndx(mdat) <= PM_YELLOW_DRAGON && monsndx(mdat) >= PM_GRAY_DRAGON
        && do_name && (!has_mgivenname(mtmp) || article == ARTICLE_NONE)) {
        age_category = dragonages[max(0, min((int) mtmp->m_lev - (int) mdat->mlevel + 3, SIZE(dragonages) -1))];
        Sprintf(buf, "%s", age_category);
    }
    if (has_etemplate(mtmp)) {
        Sprintf(eos(buf), "%s ", montemplates[ETEMPLATE(mtmp)->template_index].pmnames[NEUTRAL]);
    }
    has_adjectives = (buf[0] != '\0');

    /* Put the actual monster name or type into the buffer now.
       Remember whether the buffer starts with a personal name. */
    if (do_hallu) {
        char rnamecode;
        char *rname = rndmonnam(&rnamecode);

        Strcat(buf, rname);
        name_at_start = bogon_is_pname(rnamecode);
    } else if (do_name && has_mgivenname(mtmp)) {
        char *name = MGIVENNAME(mtmp);

        if (is_bones_monster(mdat)) {
            if (mdat == &mons[PM_GHOST]) {
                Sprintf(eos(buf), "%s ghost", s_suffix(name));
                name_at_start = TRUE;
            } else {
                Sprintf(eos(buf), "%s the %s", name, pm_name);
                name_at_start = TRUE;
            }
        } else if (called) {
            Sprintf(eos(buf), "%s called %s", pm_name, name);
            name_at_start = (boolean) type_is_pname(mdat);
        } else if (is_mplayer(mdat) && (bp = strstri(name, " the ")) != 0) {
            /* <name> the <adjective> <invisible> <saddled> <rank> */
            char pbuf[BUFSZ];

            Strcpy(pbuf, name);
            pbuf[bp - name + 5] = '\0'; /* adjectives right after " the " */
            if (has_adjectives)
                Strcat(pbuf, buf);
            Strcat(pbuf, bp + 5); /* append the rest of the name */
            Strcpy(buf, pbuf);
            article = ARTICLE_NONE;
            name_at_start = TRUE;
        } else {
            Strcat(buf, name);
            name_at_start = TRUE;
        }
    } else if (mdat == &mons[PM_HYDRA] && mtmp->m_lev - mtmp->data->mlevel > -1) {
        Sprintf(eos(buf), "%d-headed hydra",
            mtmp->m_lev - mtmp->data->mlevel + 2);
        name_at_start = FALSE;
    } else if (is_mplayer(mdat) && !In_endgame(&u.uz)) {
        char pbuf[BUFSZ];

        Strcpy(pbuf, rank_of((int) mtmp->m_lev, monsndx(mdat),
                             (boolean) mtmp->female));
        Strcat(buf, lcase(pbuf));
        name_at_start = FALSE;
    } else if (is_unkdemon(mdat) && (g.mvitals[monsndx(mdat)].mvflags & G_KNOWN) == 0) {
        Strcat(buf, "demon");
        name_at_start = (boolean) type_is_pname(mdat);
    } else {
        Strcat(buf, pm_name);
        name_at_start = (boolean) type_is_pname(mdat);
    }

    if (name_at_start && (article == ARTICLE_YOUR || !has_adjectives)) {
        if (mdat == &mons[PM_WIZARD_OF_YENDOR])
            article = ARTICLE_THE;
        else
            article = ARTICLE_NONE;
    } else if ((mdat->geno & G_UNIQ) != 0 && article == ARTICLE_A) {
        article = ARTICLE_THE;
    }

    if (article == ARTICLE_A && falseCap && !name_at_start) {
        char buf2[BUFSZ], buf3[BUFSZ];

        /* some type names like "Archon", "Green-elf", and "Uruk-hai" fool
           an() because of the capitalization and would result in "the " */
        Strcpy(buf3, buf);
        *buf3 = lowc(*buf3);
        (void) just_an(buf2, buf3);
        Strcat(buf2, buf);
        return strcpy(buf, buf2);
    } else {
        char buf2[BUFSZ];

        switch (article) {
        case ARTICLE_YOUR:
            Strcpy(buf2, "your ");
            Strcat(buf2, buf);
            Strcpy(buf, buf2);
            return buf;
        case ARTICLE_THE:
            Strcpy(buf2, "the ");
            Strcat(buf2, buf);
            Strcpy(buf, buf2);
            return buf;
        case ARTICLE_A:
            return an(buf);
        case ARTICLE_NONE:
        default:
            return buf;
        }
    }
}

char *
l_monnam(struct monst *mtmp)
{
    return x_monnam(mtmp, ARTICLE_NONE, (char *) 0,
                    (has_mgivenname(mtmp)) ? SUPPRESS_SADDLE : 0, TRUE);
}

char *
mon_nam(struct monst *mtmp)
{
    return x_monnam(mtmp, ARTICLE_THE, (char *) 0,
                    (has_mgivenname(mtmp)) ? SUPPRESS_SADDLE : 0, FALSE);
}

/* print the name as if mon_nam() was called, but assume that the player
 * can always see the monster--used for probing and for monsters aggravating
 * the player with a cursed potion of invisibility
 */
char *
noit_mon_nam(struct monst *mtmp)
{
    return x_monnam(mtmp, ARTICLE_THE, (char *) 0,
                    (has_mgivenname(mtmp)) ? (SUPPRESS_SADDLE | SUPPRESS_IT)
                                      : SUPPRESS_IT,
                    FALSE);
}

char *
Monnam(struct monst *mtmp)
{
    register char *bp = mon_nam(mtmp);

    *bp = highc(*bp);
    return  bp;
}

char *
noit_Monnam(struct monst *mtmp)
{
    register char *bp = noit_mon_nam(mtmp);

    *bp = highc(*bp);
    return  bp;
}

/* return "a dog" rather than "Fido", honoring hallucination and visibility */
char *
noname_monnam(struct monst *mtmp, int article)
{
    return x_monnam(mtmp, article, (char *) 0, SUPPRESS_NAME, FALSE);
}

/* monster's own name -- overrides hallucination and [in]visibility
   so shouldn't be used in ordinary messages (mainly for disclosure) */
char *
m_monnam(struct monst *mtmp)
{
    return x_monnam(mtmp, ARTICLE_NONE, (char *) 0, EXACT_NAME, FALSE);
}

/* pet name: "your little dog" */
char *
y_monnam(struct monst *mtmp)
{
    int prefix, suppression_flag;

    prefix = mtmp->mtame ? ARTICLE_YOUR : ARTICLE_THE;
    suppression_flag = (has_mgivenname(mtmp)
                        /* "saddled" is redundant when mounted */
                        || mtmp == u.usteed)
                           ? SUPPRESS_SADDLE
                           : 0;

    return x_monnam(mtmp, prefix, (char *) 0, suppression_flag, FALSE);
}

char *
Adjmonnam(struct monst *mtmp, const char *adj)
{
    char *bp = x_monnam(mtmp, ARTICLE_THE, adj,
                        has_mgivenname(mtmp) ? SUPPRESS_SADDLE : 0, FALSE);

    *bp = highc(*bp);
    return  bp;
}

char *
a_monnam(struct monst *mtmp)
{
    return x_monnam(mtmp, ARTICLE_A, (char *) 0,
                    has_mgivenname(mtmp) ? SUPPRESS_SADDLE : 0, FALSE);
}

char *
Amonnam(struct monst *mtmp)
{
    char *bp = a_monnam(mtmp);

    *bp = highc(*bp);
    return  bp;
}

/* used for monster ID by the '/', ';', and 'C' commands to block remote
   identification of the endgame altars via their attending priests */
char *
distant_monnam(struct monst *mon,
               int article, /* only ARTICLE_NONE and ARTICLE_THE
                               are handled here */
               char *outbuf)
{
    /* high priest(ess)'s identity is concealed on the Astral Plane,
       unless you're adjacent (overridden for hallucination which does
       its own obfuscation) */
    if (mon->data == &mons[PM_HIGH_CLERIC] && !Hallucination
        && Is_astralevel(&u.uz) && distu(mon->mx, mon->my) > 2) {
        Strcpy(outbuf, article == ARTICLE_THE ? "the " : "");
        Strcat(outbuf, mon->female ? "high priestess" : "high priest");
    } else {
        Strcpy(outbuf, x_monnam(mon, article, (char *) 0, 0, TRUE));
    }
    return outbuf;
}

/* returns mon_nam(mon) relative to other_mon; normal name unless they're
   the same, in which case the reference is to {him|her|it} self */
char *
mon_nam_too(struct monst *mon, struct monst *other_mon)
{
    char *outbuf;

    if (mon != other_mon) {
        outbuf = mon_nam(mon);
    } else {
        outbuf = nextmbuf();
        switch (pronoun_gender(mon, PRONOUN_HALLU)) {
        case 0:
            Strcpy(outbuf, "himself");
            break;
        case 1:
            Strcpy(outbuf, "herself");
            break;
        default:
        case 2:
            Strcpy(outbuf, "itself");
            break;
        case 3: /* could happen when hallucinating */
            Strcpy(outbuf, "themselves");
            break;
        }
    }
    return outbuf;
}

/* construct "<monnamtext> <verb> <othertext> {him|her|it}self" which might
   be distorted by Hallu; if that's plural, adjust monnamtext and verb */
char *
monverbself(struct monst *mon,
            char *monnamtext, /* modifiable 'mbuf' with adequare room at end */
            const char *verb, const char *othertext)
{
    char *verbs, selfbuf[40]; /* sizeof "themselves" suffices */

    /* "himself"/"herself"/"itself", maybe "themselves" if hallucinating */
    Strcpy(selfbuf, mon_nam_too(mon, mon));
    /* verb starts plural; this will yield singular except for "themselves" */
    verbs = vtense(selfbuf, verb);
    if (!strcmp(verb, verbs)) { /* a match indicates that it stayed plural */
        monnamtext = makeplural(monnamtext);
        /* for "it", makeplural() produces "them" but we want "they" */
        if (!strcmpi(monnamtext, genders[3].he)) {
            boolean capitaliz = (monnamtext[0] == highc(monnamtext[0]));

            Strcpy(monnamtext, genders[3].him);
            if (capitaliz)
                monnamtext[0] = highc(monnamtext[0]);
        }
    }
    Strcat(strcat(monnamtext, " "), verbs);
    if (othertext && *othertext)
        Strcat(strcat(monnamtext, " "), othertext);
    Strcat(strcat(monnamtext, " "), selfbuf);
    return monnamtext;
}

/* for debugging messages, where data might be suspect and we aren't
   taking what the hero does or doesn't know into consideration */
char *
minimal_monnam(struct monst *mon, boolean ckloc)
{
    struct permonst *ptr;
    char *outbuf = nextmbuf();

    if (!mon) {
        Strcpy(outbuf, "[Null monster]");
    } else if ((ptr = mon->data) == 0) {
        Strcpy(outbuf, "[Null mon->data]");
    } else if (ptr < &mons[0]) {
        Sprintf(outbuf, "[Invalid mon->data %s < %s]",
                fmt_ptr((genericptr_t) mon->data),
                fmt_ptr((genericptr_t) &mons[0]));
    } else if (ptr >= &mons[NUMMONS]) {
        Sprintf(outbuf, "[Invalid mon->data %s >= %s]",
                fmt_ptr((genericptr_t) mon->data),
                fmt_ptr((genericptr_t) &mons[NUMMONS]));
    } else if (ckloc && ptr == &mons[PM_LONG_WORM]
               && g.level.monsters[mon->mx][mon->my] != mon) {
        Sprintf(outbuf, "%s <%d,%d>",
                pmname(&mons[PM_LONG_WORM_TAIL], Mgender(mon)),
                mon->mx, mon->my);
    } else {
        Sprintf(outbuf, "%s%s <%d,%d>",
                mon->mtame ? "tame " : mon->mpeaceful ? "peaceful " : "",
                mon_pmname(mon), mon->mx, mon->my);
        if (mon->cham != NON_PM)
            Sprintf(eos(outbuf), "{%s}",
                    pmname(&mons[mon->cham], Mgender(mon)));
    }
    return outbuf;
}

#ifndef PMNAME_MACROS
int
Mgender(struct monst *mtmp)
{
    int mgender = MALE;

    if (mtmp == &g.youmonst) {
        if (Upolyd ? u.mfemale : flags.female)
            mgender = FEMALE;
    } else if (mtmp->female) {
        mgender = FEMALE;
    }
    return mgender;
}

const char *
pmname(struct permonst *pm, int mgender)
{
    if (mgender < MALE || mgender >= NUM_MGENDERS || !pm->pmnames[mgender])
        mgender = NEUTRAL;
    return pm->pmnames[mgender];
}
#endif /* PMNAME_MACROS */

/* mons[]->pmname for a monster */
const char *
mon_pmname(struct monst *mon)
{
    /* for neuter, mon->data->pmnames[MALE] will be Null and use [NEUTRAL] */
    return pmname(mon->data, mon->female ? FEMALE : MALE);
}

/* mons[]->pmname for a corpse or statue or figurine */
const char *
obj_pmname(struct obj *obj)
{
#if 0   /* ignore saved montraits even when they're available; they determine
         * what a corpse would revive as if resurrected (human corpse from
         * slain vampire revives as vampire rather than as human, for example)
         * and don't necessarily reflect the state of the corpse itself */
    if (has_omonst(obj)) {
        struct monst *m = OMONST(obj);

        /* obj->oextra->omonst->data is Null but ...->mnum is set */
        if (m->mnum >= LOW_PM)
            return pmname(&mons[m->mnum], m->female ? FEMALE : MALE);
    }
#endif
    if ((obj->otyp == CORPSE || obj->otyp == STATUE || obj->otyp == FIGURINE)
        && obj->corpsenm >= LOW_PM) {
        int cgend = (obj->spe & CORPSTAT_GENDER),
            mgend = ((cgend == CORPSTAT_MALE) ? MALE
                     : (cgend == CORPSTAT_FEMALE) ? FEMALE
                       : NEUTRAL);

        return pmname(&mons[obj->corpsenm], mgend);
    }
    return "";
}

/* fake monsters used to be in a hard-coded array, now in a data file */
char *
bogusmon(char *buf, char *code)
{
    static const char bogon_codes[] = "-_+|="; /* see dat/bonusmon.txt */
    char *mnam = buf;

    if (code)
        *code = '\0';
    /* might fail (return empty buf[]) if the file isn't available */
    get_rnd_text(BOGUSMONFILE, buf, rn2_on_display_rng);
    if (!*mnam) {
        Strcpy(buf, "bogon");
    } else if (index(bogon_codes, *mnam)) { /* strip prefix if present */
        if (code)
            *code = *mnam;
        ++mnam;
    }
    return mnam;
}

/* return a random monster name, for hallucination */
char *
rndmonnam(char *code)
{
    static char buf[BUFSZ];
    char *mnam;
    int name;
#define BOGUSMONSIZE 100 /* arbitrary */

    if (code)
        *code = '\0';

    do {
        name = rn2_on_display_rng(SPECIAL_PM + BOGUSMONSIZE - LOW_PM) + LOW_PM;
    } while (name < SPECIAL_PM
             && (type_is_pname(&mons[name]) || (mons[name].geno & G_NOGEN)));

    if (name >= SPECIAL_PM) {
        mnam = bogusmon(buf, code);
    } else {
        mnam = strcpy(buf, pmname(&mons[name], rn2_on_display_rng(2)));
    }
    return mnam;
#undef BOGUSMONSIZE
}

/* check bogusmon prefix to decide whether it's a personal name */
boolean
bogon_is_pname(char code)
{
    if (!code)
        return FALSE;
    return index("-+=", code) ? TRUE : FALSE;
}

/* name of a Rogue player */
const char *
roguename(void)
{
    char *i, *opts;

    if ((opts = nh_getenv("ROGUEOPTS")) != 0) {
        for (i = opts; *i; i++)
            if (!strncmp("name=", i, 5)) {
                char *j;
                if ((j = index(i + 5, ',')) != 0)
                    *j = (char) 0;
                return i + 5;
            }
    }
    return rn2(3) ? (rn2(2) ? "Michael Toy" : "Kenneth Arnold")
                  : "Glenn Wichman";
}

static NEARDATA const char *const hcolors[] = {
    "ultraviolet", "infrared", "bluish-orange", "reddish-green", "dark white",
    "light black", "sky blue-pink", "pinkish-cyan", "indigo-chartreuse",
    "salty", "sweet", "sour", "bitter", "umami", /* basic tastes */
    "striped", "spiral", "swirly", "plaid", "checkered", "argyle", "paisley",
    "blotchy", "guernsey-spotted", "polka-dotted", "square", "round",
    "triangular", "cabernet", "sangria", "fuchsia", "wisteria", "lemon-lime",
    "strawberry-banana", "peppermint", "romantic", "incandescent",
    "octarine", /* Discworld: the Colour of Magic */
    "excitingly dull", "mauve", "electric",
    "neon", "fluorescent", "phosphorescent", "translucent", "opaque",
    "psychedelic", "iridescent", "rainbow-colored", "polychromatic",
    "colorless", "colorless green",
    "dancing", "singing", "loving", "loudy", "noisy", "clattery", "silent",
    "apocyan", "infra-pink", "opalescent", "violant", "tuneless",
    "viridian", "aureolin", "cinnabar", "purpurin", "gamboge", "madder",
};

const char *
hcolor(const char *colorpref)
{
    return (Hallucination || !colorpref)
        ? hcolors[rn2_on_display_rng(SIZE(hcolors))]
        : colorpref;
}

/* return a random real color unless hallucinating */
const char *
rndcolor(void)
{
    int k = rn2(CLR_MAX);

    return Hallucination ? hcolor((char *) 0)
                         : (k == NO_COLOR) ? "colorless"
                                           : c_obj_colors[k];
}

static NEARDATA const char *const hliquids[] = {
    "yoghurt", "oobleck", "clotted blood", "diluted water", "purified water",
    "instant coffee", "tea", "herbal infusion", "liquid rainbow",
    "creamy foam", "mulled wine", "bouillon", "nectar", "grog", "flubber",
    "ketchup", "slow light", "oil", "vinaigrette", "liquid crystal", "honey",
    "caramel sauce", "ink", "aqueous humour", "milk substitute",
    "fruit juice", "glowing lava", "gastric acid", "mineral water",
    "cough syrup", "quicksilver", "sweet vitriol", "grey goo", "pink slime",
    /* "new coke (tm)", --better not */
};

/* if hallucinating, return a random liquid instead of 'liquidpref' */
const char *
hliquid(const char *liquidpref) /* use as-is when not hallucinating (unless empty) */
{
    if (Hallucination || !liquidpref || !*liquidpref) {
        int indx, count = SIZE(hliquids);

        /* if we have a non-hallucinatory default value, include it
           among the choices */
        if (liquidpref && *liquidpref)
            ++count;
        indx = rn2_on_display_rng(count);
        if (indx < SIZE(hliquids))
            return hliquids[indx];
    }
    return liquidpref;
}

/* Aliases for road-runner nemesis
 */
static const char *const coynames[] = {
    "Carnivorous Vulgaris", "Road-Runnerus Digestus", "Eatibus Anythingus",
    "Famishus-Famishus", "Eatibus Almost Anythingus", "Eatius Birdius",
    "Famishius Fantasticus", "Eternalii Famishiis", "Famishus Vulgarus",
    "Famishius Vulgaris Ingeniusi", "Eatius-Slobbius", "Hardheadipus Oedipus",
    "Carnivorous Slobbius", "Hard-Headipus Ravenus", "Evereadii Eatibus",
    "Apetitius Giganticus", "Hungrii Flea-Bagius", "Overconfidentii Vulgaris",
    "Caninus Nervous Rex", "Grotesques Appetitus", "Nemesis Ridiculii",
    "Canis latrans"
};

char *
coyotename(struct monst *mtmp, char *buf)
{
    if (mtmp && buf) {
        Sprintf(buf, "%s - %s",
                x_monnam(mtmp, ARTICLE_NONE, (char *) 0, 0, TRUE),
                mtmp->mcan ? coynames[SIZE(coynames) - 1]
                           : coynames[mtmp->m_id % (SIZE(coynames) - 1)]);
    }
    return buf;
}

char *
rnddragonname(char *s)
{
    static const char *b[] = { "shr", "gr", "h", "b", "z", "rh", "r", "s" };
    static const char *m[] = { "y", "ae", "i", "au", "al", "itha", "u", "a" };
    static const char *e[] = { "gar", "zar", "ken", "seth", "lax", "thon", 
                               "thos", "taur", "thrax", "mon", "n" };

    int i;
    int j = 1 + rn2(3);

    if (s) {
        *s = '\0';
        for (i = 0; i < j; i++) {
            Sprintf(eos(s), "%s%s", b[rn2(SIZE(b))], m[rn2(SIZE(m))]);
        }
        Sprintf(eos(s), "%s", e[rn2(SIZE(e))]);
    }
    return s;
}

struct monst *
christen_dragon(struct monst *mtmp)
{
    char buf[BUFSZ], *dragonname;

    dragonname = rnddragonname(buf);
    christen_monst(mtmp, upstart(dragonname));
    return mtmp;
}

char *
rndorcname(char *s)
{
    static const char *v[] = { "a", "ai", "og", "u" };
    static const char *snd[] = { "gor", "gris", "un", "bane", "ruk",
                                 "oth","ul", "z", "thos","akh","hai" };
    int i, iend = rn1(2, 3), vstart = rn2(2);

    if (s) {
        *s = '\0';
        for (i = 0; i < iend; ++i) {
            vstart = 1 - vstart;                /* 0 -> 1, 1 -> 0 */
            Sprintf(eos(s), "%s%s", (i > 0 && !rn2(30)) ? "-" : "",
                    vstart ? v[rn2(SIZE(v))] : snd[rn2(SIZE(snd))]);
        }
    }
    return s;
}

struct monst *
christen_orc(struct monst *mtmp, const char *gang, const char *other)
{
    int sz = 0;
    char buf[BUFSZ], buf2[BUFSZ], *orcname;

    orcname = rndorcname(buf2);
    sz = (int) strlen(orcname);
    if (gang)
        sz += (int) (strlen(gang) + sizeof " of " - sizeof "");
    else if (other)
        sz += (int) strlen(other);

    if (sz < BUFSZ) {
        char gbuf[BUFSZ];
        boolean nameit = FALSE;

        if (gang && orcname) {
            Sprintf(buf, "%s of %s", upstart(orcname),
                    upstart(strcpy(gbuf, gang)));
            nameit = TRUE;
        } else if (other && orcname) {
            Sprintf(buf, "%s%s", upstart(orcname), other);
            nameit = TRUE;
        }
        if (nameit)
            mtmp = christen_monst(mtmp, buf);
    }
    return mtmp;
}

static const char *const encyclopedias[] = {
    /* Alliteration */
    "Index of Items Moste Interesting", "De Dungeons of Doom for Dummies",
    "The Adventurer\'s Almanac", "The Implausibility of Identification",
    "The Big Book of Bats", "Mystickism and Magick",
    "Monsters Most Malevolent", "The Voluminous Volume Vol. V",
    "Catacomps and Creeps", "The Encyclopedia of Enchantments",
    "Mummies, Mimics, and More", "On the Magic of Markers",
    /* Misc */
    "A Treatise on Yendor", "Collected Essays of Og",
    /* Sir Terry, Night Watch */
    "Anecdotes of the Great Accountants, Vol. III",
    "Some Observations on the Art of Invisibility"
};

/* make sure "The Colour of Magic" remains the first entry in here */
static const char *const sir_Terry_novels[] = {
    "The Colour of Magic", "The Light Fantastic", "Equal Rites", "Mort",
    "Sourcery", "Wyrd Sisters", "Pyramids", "Guards! Guards!", "Eric",
    "Moving Pictures", "Reaper Man", "Witches Abroad", "Small Gods",
    "Lords and Ladies", "Men at Arms", "Soul Music", "Interesting Times",
    "Maskerade", "Feet of Clay", "Hogfather", "Jingo", "The Last Continent",
    "Carpe Jugulum", "The Fifth Elephant", "The Truth", "Thief of Time",
    "The Last Hero", "The Amazing Maurice and His Educated Rodents",
    "Night Watch", "The Wee Free Men", "Monstrous Regiment",
    "A Hat Full of Sky", "Going Postal", "Thud!", "Wintersmith",
    "Making Money", "Unseen Academicals", "I Shall Wear Midnight", "Snuff",
    "Raising Steam", "The Shepherd's Crown"
};

const char *
noveltitle(int *novidx, boolean encyclopedia)
{
    int j = 0;
    int k = encyclopedia ? SIZE(encyclopedias) : SIZE(sir_Terry_novels);

    j = rn2(k);
    if (novidx) {
        if (*novidx == -1)
            *novidx = j;
        else if (*novidx >= 0 && *novidx < k)
            j = *novidx;
    }
    return encyclopedia ? encyclopedias[j] : sir_Terry_novels[j];
}

/* figure out canonical novel title from player-specified one */
const char *
lookup_novel(const char *lookname, int *idx)
{
    int k;

    /*
     * Accept variant spellings:
     * _The_Colour_of_Magic_ uses British spelling, and American
     * editions keep that, but we also recognize American spelling;
     * _Sourcery_ is a joke rather than British spelling of "sorcery".
     */
    if (!strcmpi(The(lookname), "The Color of Magic"))
        lookname = sir_Terry_novels[0];
    else if (!strcmpi(lookname, "Sorcery"))
        lookname = "Sourcery"; /* [4] */
    else if (!strcmpi(lookname, "Masquerade"))
        lookname = "Maskerade"; /* [17] */
    else if (!strcmpi(The(lookname), "The Amazing Maurice"))
        lookname = "The Amazing Maurice and His Educated Rodents"; /* [27] */
    else if (!strcmpi(lookname, "Thud"))
        lookname = "Thud!"; /* [33] */

    for (k = 0; k < SIZE(sir_Terry_novels); ++k) {
        if (!strcmpi(lookname, sir_Terry_novels[k])
            || !strcmpi(The(lookname), sir_Terry_novels[k])) {
            if (idx)
                *idx = k;
            return sir_Terry_novels[k];
        }
    }
    /* name not found; if novelidx is already set, override the name */
    if (idx && *idx >= 0 && *idx < SIZE(sir_Terry_novels))
        return sir_Terry_novels[*idx];

    return (const char *) 0;
}

const char *
lookup_encyclopedia(lookname, idx)
const char *lookname;
int *idx;
{
    int k;

    for (k = 0; k < SIZE(encyclopedias); ++k) {
        if (!strcmpi(lookname, encyclopedias[k])
            || !strcmpi(The(lookname), encyclopedias[k])) {
            if (idx)
                *idx = k;
            return encyclopedias[k];
        }
    }
    /* name not found; if novelidx is already set, override the name */
    if (idx && *idx >= 0 && *idx < SIZE(encyclopedias))
        return encyclopedias[*idx];

    return (const char *) 0;
}

/* Most of these are actual names of nymphs from mythology. */
const char* nymphnames[] = {
    "Erythea", "Hesperia", "Arethusa", "Pasithea", "Thaleia", "Halimede",
    "Actaea", "Electra", "Maia", "Nesaea", "Alcyone", "Asterope",
    "Callianeira", "Nausithoe", "Dione", "Thetis", "Ephyra", "Eulimene",
    "Nerea", "Laomedeia", "Echo", "Maera", "Eurydice", "Lysianassa", "Phoebe",
    "Daphnis", "Daphnae", "Melinoe", "Othreis", "Polychrome"
};

const char* maldemonnames[] = {
    "Agiel", "Kali", "Amon", "Foras", "Armaros", "Orias", "Malthus", "Asag",
    "Raum", "Iblis", "Vanth", "Bael", "Leonard", "Barbas", "Charun", "Ishmael",
    "Balthamel", "Rahvin"
};

const char* femdemonnames[] = {
    "Mara", "Lamia", "Meraxes", "Daeva", "Amy", "Lilith", "Aliss", "Berith",
    "Euryale", "Zorya", "Rhaenyra", "Bellatrix", "Rusalka", "Messaana",
    "Jadis", "Anzu", "Eve", "Bilquis", "Cyndane", "Vanessa", "Graendal"
};
#define rnd_name(list) list[rn2(SIZE(list))]

/* Monster introduces themselves. If they're not currently named, give them a
 * random name from the specified list. */
void
mintroduce(mtmp)
struct monst *mtmp;
{
    char buf[BUFSZ];

    if (!has_mgivenname(mtmp) && !(mtmp->data->geno & G_UNIQ)) {
        const char* name;
        if (mtmp->data->mlet == S_NYMPH || mtmp->data == &mons[PM_THRIAE]) {
            name = rnd_name(nymphnames);
        } else if (is_demon(mtmp->data)) {
            if (mtmp->female)
                name = rnd_name(femdemonnames);
            else
                name = rnd_name(maldemonnames);
        } else if (mtmp->data == &mons[PM_GHOST]) {
            name = rndghostname();
        } else if (is_orc(mtmp->data)) {
            name = rndorcname(buf);
        } else if (is_human(mtmp->data)) {
            name = rndhumname(mtmp->female);
        } else {
            return;
        }
        const char* pronoun =
            genders[is_neuter(mtmp->data) ? 2 : mtmp->female].him;
        if (!Deaf) {
            pline("%s introduces %sself to you as %s.", Monnam(mtmp), pronoun,
                  name);
            christen_monst(mtmp, name);
        } else {
            pline("%s seems to be introducing %sself, but you can't hear %s.",
                  Monnam(mtmp), pronoun, pronoun);
        }
    }
    return;
}

/*do_name.c*/
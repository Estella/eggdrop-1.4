/* 
 * userchan.c - part of channels.mod
 *
 */
/* 
 * This file is part of the eggdrop source code
 * copyright (c) 1997 Robey Pointer
 * and is distributed according to the GNU general public license.
 * For full details, read the top of 'main.c' or the file called
 * COPYING that was distributed with this code.
 */

struct chanuserrec *get_chanrec(struct userrec *u, char *chname)
{
  struct chanuserrec *ch = u->chanrec;

  while (ch != NULL) {
    if (!rfc_casecmp(ch->channel, chname))
      return ch;
    ch = ch->next;
  }
  return NULL;
}

static struct chanuserrec *add_chanrec(struct userrec *u, char *chname)
{
  struct chanuserrec *ch = 0;

  if (findchan(chname)) {
    ch = user_malloc(sizeof(struct chanuserrec));

    ch->next = u->chanrec;
    u->chanrec = ch;
    ch->info = NULL;
    ch->flags = 0;
    ch->flags_udef = 0;
    ch->laston = 0;
    strncpy(ch->channel, chname, 80);
    ch->channel[80] = 0;
    if (!noshare && !(u->flags & USER_UNSHARED))
      shareout(findchan(chname), "+cr %s %s\n", u->handle, chname);
  }
  return ch;
}

static void add_chanrec_by_handle(struct userrec *bu, char *hand, char *chname)
{
  struct userrec *u;

  u = get_user_by_handle(bu, hand);
  if (!u)
    return;
  if (!get_chanrec(u, chname))
    add_chanrec(u, chname);
}

static void get_handle_chaninfo(char *handle, char *chname, char *s)
{
  struct userrec *u;
  struct chanuserrec *ch;

  u = get_user_by_handle(userlist, handle);
  if (u == NULL) {
    s[0] = 0;
    return;
  }
  ch = get_chanrec(u, chname);
  if (ch == NULL) {
    s[0] = 0;
    return;
  }
  if (ch->info == NULL) {
    s[0] = 0;
    return;
  }
  strcpy(s, ch->info);
  return;
}

static void set_handle_chaninfo(struct userrec *bu, char *handle,
				char *chname, char *info)
{
  struct userrec *u;
  struct chanuserrec *ch;
  char *p;
  struct chanset_t *cst;

  u = get_user_by_handle(bu, handle);
  if (!u)
    return;
  ch = get_chanrec(u, chname);
  if (!ch) {
    add_chanrec_by_handle(bu, handle, chname);
    ch = get_chanrec(u, chname);
  }
  if (info) {
    if (strlen(info) > 80)
      info[80] = 0;
    for (p = info; *p;) {
      if ((*p < 32) || (*p == 127))
	strcpy(p, p + 1);
      else
	p++;
    }
  }
  if (ch->info != NULL)
    nfree(ch->info);
  if (info && info[0]) {
    ch->info = (char *) nmalloc(strlen(info) + 1);
    strcpy(ch->info, info);
  } else
    ch->info = NULL;
  cst = findchan(chname);
  if ((!noshare) && (bu == userlist) &&
      !(u->flags & (USER_UNSHARED | USER_BOT)) && share_greet) {
    shareout(cst, "chchinfo %s %s %s\n", handle, chname, info ? info : "");
  }
}

static void del_chanrec(struct userrec *u, char *chname)
{
  struct chanuserrec *ch, *lst;

  lst = NULL;
  ch = u->chanrec;
  while (ch) {
    if (!rfc_casecmp(chname, ch->channel)) {
      if (lst == NULL)
	u->chanrec = ch->next;
      else
	lst->next = ch->next;
      if (ch->info != NULL)
	nfree(ch->info);
      nfree(ch);
      if (!noshare && !(u->flags & USER_UNSHARED))
	shareout(findchan(chname), "-cr %s %s\n", u->handle, chname);
      return;
    }
    lst = ch;
    ch = ch->next;
  }
}

static void set_handle_laston(char *chan, struct userrec *u, time_t n)
{
  struct chanuserrec *ch;

  if (!u)
    return;
  touch_laston(u, chan, n);
  ch = get_chanrec(u, chan);
  if (!ch)
    return;
  ch->laston = n;
}

/* is this ban sticky? */
static int u_sticky_ban(struct banrec *u, char *uhost)
{
  for (; u; u = u->next)
    if (!rfc_casecmp(u->banmask, uhost))
      return (u->flags & BANREC_STICKY);
  return 0;
}

/* set sticky attribute for a ban */
static int u_setsticky_ban(struct chanset_t *chan, char *uhost, int sticky)
{
  int j;
  struct banrec *u = chan ? chan->bans : global_bans;

  j = atoi(uhost);
  if (!j)
    j = (-1);
  for (; u; u = u->next) {
    if (j >= 0)
      j--;
    if (!j || ((j < 0) && !rfc_casecmp(u->banmask, uhost))) {
      if (sticky > 0)
	u->flags |= BANREC_STICKY;
      else if (!sticky)
	u->flags &= ~BANREC_STICKY;
      else			/* we don't actually want to change,
				 * just skip over */
	return 0;
      if (!j)
	strcpy(uhost, u->banmask);
      if (!noshare) {
	shareout(chan, "s %s %d %s\n", uhost, sticky,
		 chan ? chan->name : "");
      }
      return 1;
    }
  }
  if (j >= 0)
    return -j;
  else
    return 0;
}

/* returns 1 if temporary ban, 2 if permban, 0 if not a ban at all */
static int u_equals_ban(struct banrec *u, char *uhost)
{
  for (; u; u = u->next)
    if (!rfc_casecmp(u->banmask, uhost)) {
      if (u->flags & BANREC_PERM)
	return 2;
      else
	return 1;
    }
  return 0;			/* not equal */
}

static int u_match_ban(struct banrec *u, char *uhost)
{
  for (; u; u = u->next)
    if (wild_match(u->banmask, uhost))
      return 1;
  return 0;
}

static int u_delban(struct chanset_t *c, char *who, int doit)
{
  int j, i = 0;
  struct banrec *t;
  struct banrec **u = c ? &(c->bans) : &global_bans;

  if (!strchr(who, '!') && (j = atoi(who))) {
    j--;
    for (; (*u) && j; u = &((*u)->next), j--);
    if (*u) {
      strcpy(who, (*u)->banmask);
      i = 1;
    } else
      return -j - 1;
  } else {
    /* find matching host, if there is one */
    for (; *u && !i; u = &((*u)->next))
      if (!rfc_casecmp((*u)->banmask, who)) {
	i = 1;
	break;
      }
    if (!*u)
      return 0;
  }
  if (i && doit) {
    if (!noshare) {
      /* distribute chan bans differently */
      if (c)
	shareout(c, "-bc %s %s\n", c->name, who);
      else
	shareout(NULL, "-b %s\n", who);
    }
    if (!c)
      gban_total--;
    nfree((*u)->banmask);
    if ((*u)->desc)
      nfree((*u)->desc);
    if ((*u)->user)
      nfree((*u)->user);
    t = *u;
    *u = (*u)->next;
    nfree(t);
  }
  return i;
}

/* new method of creating bans */
/* if first char of note is '*' it's a sticky ban */
static int u_addban(struct chanset_t *chan, char *ban, char *from, char *note,
		    time_t expire_time, int flags)
{
  struct banrec *p;
  struct banrec **u = chan ? &chan->bans : &global_bans;
  char host[1024], s[1024];
  module_entry *me;

  strcpy(host, ban);
  /* choke check: fix broken bans (must have '!' and '@') */
  if ((strchr(host, '!') == NULL) && (strchr(host, '@') == NULL))
    strcat(host, "!*@*");
  else if (strchr(host, '@') == NULL)
    strcat(host, "@*");
  else if (strchr(host, '!') == NULL) {
    char *i = strchr(host, '@');

    strcpy(s, i);
    *i = 0;
    strcat(host, "!*");
    strcat(host, s);
  }
  if ((me = module_find("server", 0, 0)) && me->funcs)
    simple_sprintf(s, "%s!%s", me->funcs[4], me->funcs[5]);
  else
    simple_sprintf(s, "%s!%s@%s", origbotname, botuser, hostname);
  if (wild_match(host, s)) {
    putlog(LOG_MISC, "*", IRC_IBANNEDME);
    return 0;
  }
  if (u_equals_ban(*u, host))
    u_delban(chan, host, 1);	/* remove old ban */
  /* it shouldn't expire and be sticky also */
  if (note[0] == '*') {
    flags |= BANREC_STICKY;
    note++;
  }
  if (expire_time != 0L)
    flags &= ~BANREC_STICKY;
  else
    flags |= BANREC_PERM;
  /* new format: */
  p = user_malloc(sizeof(struct banrec));

  p->next = *u;
  *u = p;
  p->expire = expire_time;
  p->added = now;
  p->lastactive = 0;
  p->flags = flags;
  p->banmask = user_malloc(strlen(host) + 1);
  strcpy(p->banmask, host);
  p->user = user_malloc(strlen(from) + 1);
  strcpy(p->user, from);
  p->desc = user_malloc(strlen(note) + 1);
  strcpy(p->desc, note);
  if (!noshare) {
    if (!chan)
      shareout(NULL, "+b %s %lu %s%s %s %s\n", host, expire_time - now,
	       (flags & BANREC_STICKY) ? "s" : "",
	       (flags & BANREC_PERM) ? "p" : "-", from, note);
    else
      shareout(chan, "+bc %s %lu %s %s%s %s %s\n", host, expire_time - now,
	       chan->name, (flags & BANREC_STICKY) ? "s" : "",
	       (flags & BANREC_PERM) ? "p" : "-", from, note);
  }
  return 1;
}

/* take host entry from ban list and display it ban-style */
static void display_ban(int idx, int number, struct banrec *ban,
			struct chanset_t *chan, int show_inact)
{
  char dates[81], s[41];

  if (ban->added) {
    daysago(now, ban->added, s);
    sprintf(dates, "%s %s", BANS_CREATED, s);
    if (ban->added < ban->lastactive) {
      strcat(dates, ", ");
      strcat(dates, BANS_LASTUSED);
      strcat(dates, " ");
      daysago(now, ban->lastactive, s);
      strcat(dates, s);
    }
  } else
    dates[0] = 0;
  if (ban->flags & BANREC_PERM)
    strcpy(s, "(perm)");
  else {
    char s1[41];

    days(ban->expire, now, s1);
    sprintf(s, "(expires %s)", s1);
  }
  if (ban->flags & BANREC_STICKY)
    strcat(s, " (sticky)");
  if (!chan || isbanned(chan, ban->banmask)) {
    if (number >= 0) {
      dprintf(idx, "  [%3d] %s %s\n", number, ban->banmask, s);
    } else {
      dprintf(idx, "BAN: %s %s\n", ban->banmask, s);
    }
  } else if (show_inact) {
    if (number >= 0) {
      dprintf(idx, "! [%3d] %s %s\n", number, ban->banmask, s);
    } else {
      dprintf(idx, "BAN (%s): %s %s\n", BANS_INACTIVE, ban->banmask, s);
    }
  } else
    return;
  dprintf(idx, "        %s: %s\n", ban->user, ban->desc);
  if (dates[0])
    dprintf(idx, "        %s\n", dates);
}

static void tell_bans(int idx, int show_inact, char *match)
{
  int k = 1;
  char *chname;
  struct chanset_t *chan = NULL;
  struct banrec *u;

  /* was channel given? */
  context;
  if (match[0]) {
    chname = newsplit(&match);
    if (strchr(CHANMETA, chname[0]) != NULL) {
      chan = findchan(chname);
      if (!chan) {
	dprintf(idx, "%s.\n", CHAN_NOSUCH);
	return;
      }
    } else
      match = chname;
  }
  if (!chan && !(chan = findchan(dcc[idx].u.chat->con_chan)) &&
      !(chan = chanset))
    return;
  if (show_inact)
    dprintf(idx, "%s:   (! = %s %s)\n", BANS_GLOBAL,
	    BANS_NOTACTIVE, chan->name);
  else
    dprintf(idx, "%s:\n", BANS_GLOBAL);
  context;
  u = global_bans;
  for (; u; u = u->next) {
    if (match[0]) {
      if ((wild_match(match, u->banmask)) ||
	  (wild_match(match, u->desc)) ||
	  (wild_match(match, u->user)))
	display_ban(idx, k, u, chan, 1);
      k++;
    } else
      display_ban(idx, k++, u, chan, show_inact);
  }
  if (show_inact)
    dprintf(idx, "%s %s:   (! = %s, * = %s)\n",
	    BANS_BYCHANNEL, chan->name,
	    BANS_NOTACTIVE2, BANS_NOTBYBOT);
  else
    dprintf(idx, "%s %s:  (* = %s)\n",
	    BANS_BYCHANNEL, chan->name,
	    BANS_NOTBYBOT);
  u = chan->bans;
  for (; u; u = u->next) {
    if (match[0]) {
      if ((wild_match(match, u->banmask)) ||
	  (wild_match(match, u->desc)) ||
	  (wild_match(match, u->user)))
	display_ban(idx, k, u, chan, 1);
      k++;
    } else
      display_ban(idx, k++, u, chan, show_inact);
  }
  if (chan->status & CHAN_ACTIVE) {
    banlist *b = chan->channel.ban;
    char s[UHOSTLEN], *s1, *s2, fill[256];
    int min, sec;

    while (b->ban[0]) {
      if ((!u_equals_ban(global_bans, b->ban)) &&
	  (!u_equals_ban(chan->bans, b->ban))) {
	strcpy(s, b->who);
	s2 = s;
	s1 = splitnick(&s2);
	if (s1[0])
	  sprintf(fill, "%s (%s!%s)", b->ban, s1, s2);
	else if (!strcasecmp(s, "existant"))
	  sprintf(fill, "%s (%s)", b->ban, s2);
	else
	  sprintf(fill, "%s (server %s)", b->ban, s2);
	if (b->timer != 0) {
	  min = (now - b->timer) / 60;
	  sec = (now - b->timer) - (min * 60);
	  sprintf(s, " (active %02d:%02d)", min, sec);
	  strcat(fill, s);
	}
	if ((!match[0]) || (wild_match(match, b->ban)))
	  dprintf(idx, "* [%3d] %s\n", k, fill);
	k++;
      }
      b = b->next;
    }
  }
  if (k == 1)
    dprintf(idx, "(There are no bans, permanent or otherwise.)\n");
  if ((!show_inact) && (!match[0]))
    dprintf(idx, "%s.\n", BANS_USEBANSALL);
}

/* write channel's local banlist to a file */
static int write_bans(FILE * f, int idx)
{
  struct chanset_t *chan;
  struct banrec *b;
  struct igrec *i;

  if (global_ign)
    if (fprintf(f, IGNORE_NAME " - -\n") == EOF)	/* Daemus */
      return 0;
  for (i = global_ign; i; i = i->next)
    if (fprintf(f, "- %s:%s%lu:%s:%lu:%s\n", i->igmask,
		(i->flags & IGREC_PERM) ? "+" : "", i->expire,
		i->user ? i->user : botnetnick, i->added, i->msg ? i->msg : "") == EOF)
      return 0;
  if (global_bans)
    if (fprintf(f, BAN_NAME " - -\n") == EOF)	/* Daemus */
      return 0;
  for (b = global_bans; b; b = b->next)
    if (fprintf(f, "- %s:%s%lu%s:+%lu:%lu:%s:%s\n", b->banmask,
		(b->flags & BANREC_PERM) ? "+" : "", b->expire,
		(b->flags & BANREC_STICKY) ? "*" : "", b->added,
		b->lastactive, b->user ? b->user : botnetnick,
		b->desc ? b->desc : "requested") == EOF)
      return 0;
  for (chan = chanset; chan; chan = chan->next)
    if ((idx < 0) || (chan->status & CHAN_SHARED)) {
      struct flag_record fr =
      {FR_CHAN | FR_GLOBAL | FR_BOT, 0, 0, 0, 0, 0};

      if ((idx >= 0) && !(fr.bot & BOT_GLOBAL))
	get_user_flagrec(dcc[idx].user, &fr, chan->name);
      else
	fr.chan = BOT_SHARE;
      if (fr.chan & BOT_SHARE) {
	if (fprintf(f, "::%s bans\n", chan->name) == EOF)
	  return 0;
	for (b = chan->bans; b; b = b->next)
	  if (fprintf(f, "- %s:%s%lu%s:+%lu:%lu:%s:%s\n", b->banmask,
		      (b->flags & BANREC_PERM) ? "+" : "", b->expire,
		      (b->flags & BANREC_STICKY) ? "*" : "", b->added,
		      b->lastactive, b->user ? b->user : botnetnick,
		      b->desc ? b->desc : "requested") == EOF)
	    return 0;
      }
    }
  return 1;
}

static void channels_writeuserfile()
{
  char s[1024];
  FILE *f;

  simple_sprintf(s, "%s~new", userfile);
  f = fopen(s, "a");
  write_bans(f, -1);
  fclose(f);
  write_channels();
}

/* check for expired timed-bans */
static void check_expired_bans()
{
  struct banrec **u;
  struct chanset_t *chan;

  u = &global_bans;
  while (*u) {
    if (!((*u)->flags & BANREC_PERM) && (now >= (*u)->expire)) {
      putlog(LOG_MISC, "*", "%s %s (%s)", BANS_NOLONGER,
	     (*u)->banmask, MISC_EXPIRED);
      chan = chanset;
      while (chan != NULL) {
	add_mode(chan, '-', 'b', (*u)->banmask);
	chan = chan->next;
      }
      u_delban(NULL, (*u)->banmask, 1);
    } else
      u = &((*u)->next);
  }
  /* check for specific channel-domain bans expiring */
  for (chan = chanset; chan; chan = chan->next) {
    u = &chan->bans;
    while (*u) {
      if (!((*u)->flags & BANREC_PERM) && (now >= (*u)->expire)) {
	putlog(LOG_MISC, "*", "%s %s %s %s (%s)", BANS_NOLONGER,
	       (*u)->banmask, MISC_ONLOCALE, chan->name, MISC_EXPIRED);
	add_mode(chan, '-', 'b', (*u)->banmask);
	u_delban(chan, (*u)->banmask, 1);
      } else
	u = &((*u)->next);
    }
  }
}
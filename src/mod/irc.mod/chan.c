/* 
 * chan.c -- handles:
 * almost everything to do with channel manipulation
 * telling channel status
 * 'who' response
 * user kickban, kick, op, deop
 * idle kicking
 * dprintf'ized, 27oct1995
 * multi-channel, 8feb1996
 */
/* 
 * This file is part of the eggdrop source code
 * copyright (c) 1997 Robey Pointer
 * and is distributed according to the GNU general public license.
 * For full details, read the top of 'main.c' or the file called
 * COPYING that was distributed with this code.
 */

/* new ctcp stuff */
static time_t last_ctcp = (time_t) 0L;
static int count_ctcp = 0;

/* new gotinvite stuff */
static time_t last_invtime = (time_t) 0L;
static char last_invchan[300] = "";

/* returns a pointer to a new channel member structure */
static memberlist *newmember(struct chanset_t *chan)
{
  memberlist *x;

  x = chan->channel.member;
  while (x->nick[0])
    x = x->next;
  x->next = (memberlist *) channel_malloc(sizeof(memberlist));
  x->next->next = NULL;
  x->next->nick[0] = 0;
  x->next->split = 0L;
  x->next->last = 0L;
  x->next->delay = 0L;
  chan->channel.members++;
  return x;
}

static void update_idle(char *chname, char *nick)
{
  memberlist *m;
  struct chanset_t *chan;

  chan = findchan(chname);
  if (chan) {
    m = ismember(chan, nick);
    if (m)
      m->last = now;
  }
}

/* what the channel's mode CURRENTLY is */
static char *getchanmode(struct chanset_t *chan)
{
  static char s[121];
  int atr, i;

  s[0] = '+';
  i = 1;
  atr = chan->channel.mode;
  if (atr & CHANINV)
    s[i++] = 'i';
  if (atr & CHANPRIV)
    s[i++] = 'p';
  if (atr & CHANSEC)
    s[i++] = 's';
  if (atr & CHANMODER)
    s[i++] = 'm';
  if (atr & CHANTOPIC)
    s[i++] = 't';
  if (atr & CHANNOMSG)
    s[i++] = 'n';
  if (atr & CHANANON)
    s[i++] = 'a';
  if (atr & CHANKEY)
    s[i++] = 'k';
  if (chan->channel.maxmembers > -1)
    s[i++] = 'l';
  s[i] = 0;
  if (chan->channel.key[0])
    i += sprintf(s + i, " %s", chan->channel.key);
  if (chan->channel.maxmembers > -1)
    sprintf(s + i, " %d", chan->channel.maxmembers);
  return s;
}

/*        Check a channel and clean-out any more-specific matching masks.
 *      Moved all do_ban(), do_exempt() and do_invite() into this single
 *      function as the code bloat is starting to get rediculous <cybah>
 */
static void do_mask(struct chanset_t *chan, masklist *m, char *mask, char Mode)
{
  while(m && m->mask[0]) {
    if (wild_match(mask, m->mask) && rfc_casecmp(mask, m->mask)) {
      add_mode(chan, '-', Mode, m->mask);
    }
    
    m = m->next;
  }
  
  add_mode(chan, '+', Mode, mask);
  flush_mode(chan, QUICK);
}

/* this is a clone of detect_flood, but works for channel specificity now
 * and handles kick & deop as well */
static int detect_chan_flood(char *floodnick, char *floodhost, char *from,
			     struct chanset_t *chan, int which, char *victim)
{
  char h[UHOSTLEN], ftype[12], *p;
  struct userrec *u;
  memberlist *m;
  int thr = 0, lapse = 0;
  struct flag_record fr =
  {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  if (!chan || (which < 0) || (which >= FLOOD_CHAN_MAX))
    return 0;
  m = ismember(chan, floodnick); 
  get_user_flagrec(get_user_by_host(from), &fr, chan->name);
  context;
  if (glob_bot(fr) ||
      ((which == FLOOD_DEOP) && (glob_master(fr) || chan_master(fr))) ||
      ((which != FLOOD_DEOP) && (glob_friend(fr) || chan_friend(fr))) ||
      (channel_dontkickops(chan) &&
       (chan_op(fr) || (glob_op(fr) && !chan_deop(fr)))))	/* arthur2 */
    return 0;
  /* determine how many are necessary to make a flood */
  switch (which) {
  case FLOOD_PRIVMSG:
  case FLOOD_NOTICE:
    thr = chan->flood_pub_thr;
    lapse = chan->flood_pub_time;
    strcpy(ftype, "pub");
    break;
  case FLOOD_CTCP:
    thr = chan->flood_ctcp_thr;
    lapse = chan->flood_ctcp_time;
    strcpy(ftype, "pub");
    break;
  case FLOOD_JOIN:
  case FLOOD_NICK:
    thr = chan->flood_join_thr;
    lapse = chan->flood_join_time;
    if (which == FLOOD_JOIN)
      strcpy(ftype, "join");
    else
      strcpy(ftype, "nick");
    break;
  case FLOOD_DEOP:
    thr = chan->flood_deop_thr;
    lapse = chan->flood_deop_time;
    strcpy(ftype, "deop");
    break;
  case FLOOD_KICK:
    thr = chan->flood_kick_thr;
    lapse = chan->flood_kick_time;
    strcpy(ftype, "kick");
    break;
  }
  if ((thr == 0) || (lapse == 0))
    return 0;			/* no flood protection */
  /* okay, make sure i'm not flood-checking myself */
  if (match_my_nick(floodnick))
    return 0;
  if (!strcasecmp(floodhost, botuserhost))
    return 0;
  /* my user@host (?) */
  if ((which == FLOOD_KICK) || (which == FLOOD_DEOP))
    p = floodnick;
  else {
    p = strchr(floodhost, '@');
    if (p) {
      p++;
    }
    if (!p)
      return 0;
  }
  if (rfc_casecmp(chan->floodwho[which], p)) {	/* new */
    strncpy(chan->floodwho[which], p, 81);
    chan->floodwho[which][81] = 0;
    chan->floodtime[which] = now;
    chan->floodnum[which] = 1;
    return 0;
  }
  if (chan->floodtime[which] < now - lapse) {
    /* flood timer expired, reset it */
    chan->floodtime[which] = now;
    chan->floodnum[which] = 1;
    return 0;
  }
  /* deop'n the same person, sillyness ;) - so just ignore it */
  context;
  if (which == FLOOD_DEOP) {
    if (!rfc_casecmp(chan->deopd, victim))
      return 0;
    else
      strcpy(chan->deopd, victim);
  }
  chan->floodnum[which]++;
  context;
  if (chan->floodnum[which] >= thr) {	/* FLOOD */
    /* reset counters */
    chan->floodnum[which] = 0;
    chan->floodtime[which] = 0;
    chan->floodwho[which][0] = 0;
    if (which == FLOOD_DEOP)
      chan->deopd[0] = 0;
    u = get_user_by_host(from);
    if (check_tcl_flud(floodnick, from, u, ftype, chan->name))
      return 0;
    switch (which) {
    case FLOOD_PRIVMSG:
    case FLOOD_NOTICE:
    case FLOOD_CTCP:
      /* flooding chan! either by public or notice */
      if (m && me_op(chan)) {
	putlog(LOG_MODES, chan->name, IRC_FLOODKICK, floodnick);
	dprintf(DP_MODE, "KICK %s %s :%s\n", chan->name, floodnick,
		CHAN_FLOOD);
	m->flags |= SENTKICK;
      }
      return 1;
    case FLOOD_JOIN:
    case FLOOD_NICK:
      simple_sprintf(h, "*!*@%s", p);
      if (!isbanned(chan, h) && me_op(chan)) {
/*      add_mode(chan, '-', 'o', splitnick(&from)); */  /* useless - arthur2 */
        do_mask(chan, chan->channel.ban, h, 'b');
      }
      if ((u_match_mask(global_bans, from))
	  || (u_match_mask(chan->bans, from)))
	return 1;		/* already banned */
      if (which == FLOOD_JOIN)
	putlog(LOG_MISC | LOG_JOIN, chan->name, IRC_FLOODIGNORE3, p);
      else
	putlog(LOG_MISC | LOG_JOIN, chan->name, IRC_FLOODIGNORE4, p);
      strcpy(ftype + 4, " flood");
      u_addban(chan, h, origbotname, ftype, now + (60 * ban_time), 0);
      context;
      /* don't kick user if exempted */
      if (!channel_enforcebans(chan) && me_op(chan) && !isexempted(chan, h))
	{
	  char s[UHOSTLEN];
	  m = chan->channel.member;
	  
	  while (m->nick[0]) {
	    sprintf(s, "%s!%s", m->nick, m->userhost);
	    if (wild_match(h, s) &&
		(m->joined >= chan->floodtime[which]) &&
		   !chan_sentkick(m) && !match_my_nick(m->nick)) {
	      m->flags |= SENTKICK;
	      dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, m->nick,
		      IRC_LEMMINGBOT);
	    }
	    m = m->next;
	  }
	}
      return 1;
    case FLOOD_KICK:
      if (me_op(chan)) {
	putlog(LOG_MODES, chan->name, "Kicking %s, for mass kick.", floodnick);
	dprintf(DP_MODE, "KICK %s %s :%s\n", chan->name, floodnick,
		IRC_MASSKICK);
	m->flags |= SENTKICK;
      }
    return 1;
    case FLOOD_DEOP:
      if (me_op(chan)) {
	putlog(LOG_MODES, chan->name,
	       CHAN_MASSDEOP, CHAN_MASSDEOP_ARGS); 
	dprintf(DP_MODE, "KICK %s %s :%s\n",
		chan->name, floodnick, CHAN_MASSDEOP_KICK);
	m->flags |= SENTKICK;
      }
      return 1;
    }
  }
  context;
  return 0;
}

/* given a [nick!]user@host, place a quick ban on them on a chan */
static char *quickban(struct chanset_t *chan, char *uhost)
{
  static char s1[512];

  maskhost(uhost, s1);
  if ((strlen(s1) != 1) && (strict_host == 0))
    s1[2] = '*';		/* arthur2 */
  do_mask(chan, chan->channel.ban, s1, 'b');
  return s1;
}

/* kicks any user (except friends/masters) with certain mask from channel
 * with a specified comment.  Ernst 18/3/1998 */
static void kick_all(struct chanset_t *chan, char *hostmask, char *comment)
{
  memberlist *m;
  char kicknick[512], s[UHOSTLEN];

  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  int k, l, flushed;

  context;
  if (!me_op(chan))
    return;
  k = 0;
  flushed = 0;
  kicknick[0] = 0;
  m = chan->channel.member;
  while (m->nick[0]) {
    get_user_flagrec(m->user, &fr, chan->name);
    sprintf(s, "%s!%s", m->nick, m->userhost);
    if (!chan_sentkick(m) && wild_match(hostmask, s) &&
	!match_my_nick(m->nick) &&
	!glob_friend(fr) && !chan_friend(fr) &&
	!isexempted(chan, s) &&	/* Crotale - don't kick +e users */
	!(channel_dontkickops(chan) &&
	  (chan_op(fr) || (glob_op(fr) && !chan_deop(fr))))) {	/* arthur2 */
      if (!flushed) {
	/* we need to kick someone, flush eventual bans first */
	context;
	flush_mode(chan, QUICK);
	flushed += 1;
      }
      m->flags |= SENTKICK;	/* mark as pending kick */
      if (kicknick[0])
	strcat(kicknick, ",");
      strcat(kicknick, m->nick);
      k += 1;
      l = strlen(chan->name) + strlen(kicknick) + strlen(comment) + 5;
      if ((kick_method != 0 && k == kick_method) || (l > 480)) {
	dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, kicknick, comment);
	k = 0;
	kicknick[0] = 0;
      }
    }
    m = m->next;
  }
  if (k > 0)
    dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, kicknick, comment);
  context;
}

/* if any bans match this wildcard expression, refresh them on the channel */
static void refresh_ban_kick(struct chanset_t *chan, char *user, char *nick)
{
  maskrec *u;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  memberlist *m;
  char c[512];			/* the ban comment */

  int cycle;

  for (cycle = 0; cycle < 2; cycle++) {
    if (cycle)
      u = chan->bans;
    else
      u = global_bans;
    for (; u; u = u->next) {
      if (wild_match(u->mask, user)) {
	m = ismember(chan, nick);
	if (!m)
	  continue;				/* skip non-existant nick */
	get_user_flagrec(m->user, &fr, chan->name);
	if (!glob_friend(fr) && !chan_friend(fr))
	  add_mode(chan, '-', 'o', nick);	/* guess it can't hurt */
        do_mask(chan, chan->channel.ban, u->mask, 'b');
	c[0] = 0;
	if (u->desc && (u->desc[0] != '@')) {
	  if (strcmp(IRC_PREBANNED, ""))
	    sprintf(c, "%s: %s", IRC_PREBANNED, u->desc);
	  else
	    sprintf(c, "%s", u->desc);
	}
	kick_all(chan, u->mask, c[0] ? c : IRC_YOUREBANNED);
	return;		/* drop out on 1st ban */
      }
    }
  }
}

/* This is a bit cumbersome at the moment, but it works... Any improvements then 
 * feel free to have a go.. Jason */
static void refresh_exempt (struct chanset_t * chan, char * user) {
  maskrec *e;
  masklist *b;
  int cycle;
  
  for (cycle = 0;cycle < 2;cycle++) {
    e = (cycle) ? chan->exempts : global_exempts;

    while (e) {
      if (wild_match(user, e->mask) || wild_match(e->mask,user)) {
        b = chan->channel.ban;
        while (b && b->mask[0]) {
          if (wild_match(b->mask, user) || wild_match(user, b->mask)) {
            if (e->lastactive < now - 60 && !isexempted(chan, e->mask)) {
              do_mask(chan, chan->channel.exempt, e->mask, 'e');
              e->lastactive = now;
              return;
            }
          }
          b = b->next;
        }
      }
      e = e->next;
    }
  }
}

static void refresh_invite (struct chanset_t * chan, char * user) {
  maskrec *i;
  int cycle;
  for (cycle = 0;cycle < 2;cycle++) {
    i = (cycle) ? chan->invites : global_invites;

    while (i) {
      if (wild_match(i->mask, user) && 
	      (i->flags & MASKREC_STICKY || (chan->channel.mode & CHANINV))) {
        if (i->lastactive < now - 60 && !isinvited(chan, i->mask)) {
              do_mask(chan, chan->channel.invite, i->mask, 'I');
	      i->lastactive = now;
	      return;
	    }
      }
      
      i = i->next;
    }
  }
}

/* enforce all channel bans in a given channel.  Ernst 18/3/1998 */
static void enforce_bans(struct chanset_t *chan)
{
  char me[UHOSTLEN];
  masklist *b = chan->channel.ban;

  context;
  if (!me_op(chan))
    return;			/* can't do it */
  simple_sprintf(me, "%s!%s", botname, botuserhost);
  /* go through all bans, kicking the users */
  while (b && b->mask[0]) {
    if (!wild_match(b->mask, me))
      if (!isexempted(chan, b->mask))
	kick_all(chan, b->mask, IRC_YOUREBANNED);
    b = b->next;
  }
}

/* since i was getting a ban list, i assume i'm chop */
/* recheck_bans makes sure that all who are 'banned' on the userlist are
 * actually in fact banned on the channel */
static void recheck_bans(struct chanset_t *chan)
{
  maskrec *u;
  int i;

  for (i = 0; i < 2; i++)
    for (u = i ? chan->bans : global_bans; u; u = u->next)
      if (!isbanned(chan, u->mask) && (!channel_dynamicbans(chan) ||
					  (u->flags & MASKREC_STICKY)))
	add_mode(chan, '+', 'b', u->mask);
}

/* recheck_exempts makes sure that all who are exempted on the userlist are
   actually in fact exempted on the channel */
static void recheck_exempts(struct chanset_t * chan) {
  maskrec *e;
  masklist *b;
  int i;
  
  for (i = 0; i < 2; i++) {
    for (e = i ? chan->exempts : global_exempts; e; e = e->next) {
      if (!isexempted(chan, e->mask) && 
           (!channel_dynamicexempts(chan) ||  e->flags & MASKREC_STICKY))
        add_mode(chan, '+', 'e', e->mask);
      b = chan->channel.ban;
      while (b && b->mask[0]) {
        if ((wild_match(b->mask, e->mask) || wild_match(e->mask, b->mask)) &&
            !isexempted(chan, e->mask))
          do_mask(chan, chan->channel.exempt, e->mask, 'e');
        b = b->next;
      }
    }
  }
}

/* recheck_invite */
static void recheck_invites(struct chanset_t * chan) {
  maskrec *ir;
  int i;
  
  for (i = 0; i < 2; i++)  {
    for (ir = i ? chan->invites : global_invites; ir; ir = ir->next) {
      /* if invite isn't set and (channel is not dynamic invites and not invite
       * only) or invite is sticky */
      if (!isinvited(chan, ir->mask) && ((!channel_dynamicinvites(chan) &&
          !(chan->channel.mode & CHANINV)) || ir->flags & MASKREC_STICKY))
        do_mask(chan, chan->channel.invite, ir->mask, 'I');
    }
  }
}

/*        Resets the masks on the channel. This is resetbans(), resetexempts()
 *      and resetinvites() all merged together for less bloat. <cybah>
 */
static void resetmasks(struct chanset_t *chan, masklist *m, maskrec *mrec, maskrec *global_masks, char Mode)
{
  if (!me_op(chan))
    return;                     /* can't do it */
    
  /* remove masks we didn't put there */
  while (m && m->mask[0]) {
    if (!u_equals_mask(global_masks, m->mask) && !u_equals_mask(mrec, m->mask))
      add_mode(chan, '-', Mode, m->mask);
      
    m = m->next;
  }
  
  /* make sure the intended masks are still there */
  switch(Mode) {
        case 'b':       recheck_bans(chan);     break;
        case 'e':       recheck_exempts(chan);  break;
        case 'I':       recheck_invites(chan);  break;
        default:
          putlog(LOG_MISC, "*", "Invalid mode '%c' in resetmasks()", Mode);
        break;
  }
}

/* things to do when i just became a chanop: */
static void recheck_channel(struct chanset_t *chan, int dobans)
{
  memberlist *m;
  char s[UHOSTLEN], *p;
  struct flag_record fr =
  {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  int cur, pls, mns;
  static int stacking = 0;

  if (stacking)
    return;			/* wewps */
  if (!userlist)                /* bot doesnt know anybody */
    return;                     /* it's better not to deop everybody */
  stacking++;
  /* okay, sort through who needs to be deopped. */
  context;
  m = chan->channel.member;
  while (m->nick[0]) {
    sprintf(s, "%s!%s", m->nick, m->userhost);
    if (!m->user)
      m->user = get_user_by_host(s);
    get_user_flagrec(m->user, &fr, chan->name);
    /* ignore myself */
    if (!match_my_nick(m->nick)) {
      /* if channel user is current a chanop */
      if (chan_hasop(m)) {
	/* if user is channel deop */
	if (chan_deop(fr) ||
	/* OR global deop and NOT channel op */
	    (glob_deop(fr) && !chan_op(fr))) {
	  /* de-op! */
	  add_mode(chan, '-', 'o', m->nick);
	/* if channel mode is bitch */
	} else if (channel_bitch(chan) &&
	  /* AND the user isnt a channel op */
		 (!chan_op(fr) &&
	  /* AND the user isnt a global op, (or IS a chan deop) */
		  !(glob_op(fr) && !chan_deop(fr)))) {
	  /* de-op! mmmbop! */
	  add_mode(chan, '-', 'o', m->nick);
	    }
      }
      /* now lets look at de-op'd ppl */
      if (!chan_hasop(m) &&
      /* if they're an op, channel or global (without channel +d) */
	  (chan_op(fr) || (glob_op(fr) && !chan_deop(fr))) &&
      /* and the channel is op on join, or they are auto-opped */
	  (channel_autoop(chan) || (glob_autoop(fr) || chan_autoop(fr)))) {
	/* op them! */
	add_mode(chan, '+', 'o', m->nick);
      }
      /* now lets check 'em vs bans */
      /* if we're enforcing bans */
      if (channel_enforcebans(chan) &&
      /* & they match a ban */
	  (u_match_mask(global_bans, s) || u_match_mask(chan->bans, s))) {
	/* bewm */
	refresh_ban_kick(chan, s, m->nick);
      }
      /* ^ will use the ban comment */
      if (u_match_mask(global_exempts,s) || u_match_mask(chan->exempts, s)){
	refresh_exempt(chan, s);
      }      
      /* check vs invites */
      if (u_match_mask(global_invites,s) || u_match_mask(chan->invites, s))
	refresh_invite(chan, s);
      /* are they +k ? */
      if (chan_kick(fr) || glob_kick(fr)) {
	quickban(chan, m->userhost);
	p = get_user(&USERENTRY_COMMENT, m->user);
	dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, m->nick,
		p ? p : IRC_POLITEKICK);
	m->flags |= SENTKICK;
	/* otherwise, lets check +v stuff if the llamas want it */
      } else if (!chan_hasvoice(m) && !chan_hasop(m)) {
	if ((channel_autovoice(chan) && !chan_quiet(fr) && 
	    (chan_voice(fr) || glob_voice(fr))) || 
	    (!chan_quiet(fr) && (glob_gvoice(fr) || chan_gvoice(fr)))) {
	  add_mode(chan, '+', 'v', m->nick);
	  }
	/* do they have a voice on the channel */
	if (chan_hasvoice(m) &&
	/* do they have the +q & no +v */
	    (chan_quiet(fr) || (glob_quiet(fr) && !chan_voice(fr)))) {
	  add_mode(chan, '-', 'v', m->nick);
	   }
      }
    }
    m = m->next;
  }
  if (dobans) {
      recheck_bans(chan);
      recheck_invites(chan);
      recheck_exempts(chan);
  }
  if (dobans && channel_enforcebans(chan))
    enforce_bans(chan);
  pls = chan->mode_pls_prot;
  mns = chan->mode_mns_prot;
  cur = chan->channel.mode;
  if (!(chan->status & CHAN_ASKEDMODES)) {
    if (pls & CHANINV && !(cur & CHANINV))
      add_mode(chan, '+', 'i', "");
    else if (mns & CHANINV && cur & CHANINV)
      add_mode(chan, '-', 'i', "");
    if (pls & CHANPRIV && !(cur & CHANPRIV))
      add_mode(chan, '+', 'p', "");
    else if (mns & CHANPRIV && cur & CHANPRIV)
      add_mode(chan, '-', 'p', "");
    if (pls & CHANSEC && !(cur & CHANSEC))
      add_mode(chan, '+', 's', "");
    else if (mns & CHANSEC && cur & CHANSEC)
      add_mode(chan, '-', 's', "");
    if (pls & CHANMODER && !(cur & CHANMODER))
      add_mode(chan, '+', 'm', "");
    else if (mns & CHANMODER && cur & CHANMODER)
      add_mode(chan, '-', 'm', "");
    if (pls & CHANTOPIC && !(cur & CHANTOPIC))
      add_mode(chan, '+', 't', "");
    else if (mns & CHANTOPIC && cur & CHANTOPIC)
      add_mode(chan, '-', 't', "");
    if (pls & CHANNOMSG && !(cur & CHANNOMSG))
      add_mode(chan, '+', 'n', "");
    else if ((mns & CHANNOMSG) && (cur & CHANNOMSG))
      add_mode(chan, '-', 'n', "");
    if ((pls & CHANANON) && !(cur & CHANANON))
      add_mode(chan, '+', 'a', "");
    else if ((mns & CHANANON) && (cur & CHANANON))
      add_mode(chan, '-', 'a', "");
    if ((pls & CHANQUIET) && !(cur & CHANQUIET))
      add_mode(chan, '+', 'q', "");
    else if ((mns & CHANQUIET) && (cur & CHANQUIET))
      add_mode(chan, '-', 'q', "");
    if ((chan->limit_prot != (-1)) && (chan->channel.maxmembers == -1)) {
      sprintf(s, "%d", chan->limit_prot);
      add_mode(chan, '+', 'l', s);
    } else if ((mns & CHANLIMIT) && (chan->channel.maxmembers >= 0))
      add_mode(chan, '-', 'l', "");
    if (chan->key_prot[0]) {
      if (rfc_casecmp(chan->channel.key, chan->key_prot) != 0) {
        if (chan->channel.key[0])
	  add_mode(chan, '-', 'k', chan->channel.key);
        add_mode(chan, '+', 'k', chan->key_prot);
      }
    } else if ((mns & CHANKEY) && (chan->channel.key))
      add_mode(chan, '-', 'k', chan->channel.key);
  }
  if ((chan->status & CHAN_ASKEDMODES) && dobans &&
     !channel_inactive(chan)) /* spot on guppy, this just keeps the
 	                       * checking sane */
    dprintf(DP_SERVER, "MODE %s\n", chan->name);
  stacking--;
}

/* got 324: mode status */
/* <server> 324 <to> <channel> <mode> */
static int got324(char *from, char *msg)
{
  int i = 1, ok =0;
  char *p, *q, *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (!chan) {
    putlog(LOG_MISC, "*", "%s: %s", IRC_UNEXPECTEDMODE, chname);
    dprintf(DP_SERVER, "PART %s\n", chname);
    return 0;
  }
  if (chan->status & CHAN_ASKEDMODES)
     ok = 1;
  chan->status &= ~CHAN_ASKEDMODES;
  chan->channel.mode = 0;
  while (msg[i] != 0) {
    if (msg[i] == 'i')
      chan->channel.mode |= CHANINV;
    if (msg[i] == 'p')
      chan->channel.mode |= CHANPRIV;
    if (msg[i] == 's')
      chan->channel.mode |= CHANSEC;
    if (msg[i] == 'm')
      chan->channel.mode |= CHANMODER;
    if (msg[i] == 't')
      chan->channel.mode |= CHANTOPIC;
    if (msg[i] == 'n')
      chan->channel.mode |= CHANNOMSG;
    if (msg[i] == 'a')
      chan->channel.mode |= CHANANON;
    if (msg[i] == 'q')
      chan->channel.mode |= CHANQUIET;
    if (msg[i] == 'k') {
      chan->channel.mode |= CHANKEY;
      p = strchr(msg, ' ');
      if (p != NULL) {		/* test for null key assignment */
	p++;
	q = strchr(p, ' ');
	if (q != NULL) {
	  *q = 0;
	  set_key(chan, p);
	  strcpy(p, q + 1);
	} else {
	  set_key(chan, p);
	  *p = 0;
	}
      }
      if ((chan->channel.mode & CHANKEY) && !(chan->channel.key[0])) 
        chan->status |= CHAN_ASKEDMODES;
    }
    if (msg[i] == 'l') {
      p = strchr(msg, ' ');
      if (p != NULL) {		/* test for null limit assignment */
	p++;
	q = strchr(p, ' ');
	if (q != NULL) {
	  *q = 0;
	  chan->channel.maxmembers = atoi(p);
	  strcpy(p, q + 1);
	} else {
	  chan->channel.maxmembers = atoi(p);
	  *p = 0;
	}
      }
    }
    i++;
  }
  if (ok)
    recheck_channel(chan, 0);
  return 0;
}

static int got352or4(struct chanset_t *chan, char *user, char *host,
		     char *nick, char *flags)
{
  struct flag_record fr =
  {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  char userhost[UHOSTLEN], *p;
  memberlist *m;
  int waschanop;

  m = ismember(chan, nick);	/* In my channel list copy? */
  if (!m) {			/* Nope, so update */
    m = newmember(chan);	/* Get a new channel entry */
    m->joined = m->split = m->delay = 0L;	/* Don't know when he joined */
    m->flags = 0;		/* No flags for now */
    m->last = now;		/* Last time I saw him */
  }
  strcpy(m->nick, nick);	/* Store the nick in list */
  /* Store the userhost */
  simple_sprintf(m->userhost, "%s@%s", user, host);
  fixfrom(m->userhost);		/* Dump non-identd ~ */
  simple_sprintf(userhost, "%s!%s", nick, m->userhost);
  /* Combine n!u@h */
  m->user = NULL;		/* No handle match (yet) */
  if (match_my_nick(nick)) {	/* Is it me? */
    strcpy(botuserhost, m->userhost);	/* Yes, save my own userhost */
    m->joined = now;		/* set this to keep the whining masses happy */
  }
  waschanop = me_op(chan);	/* Am I opped here? */
  if (strchr(flags, '@') != NULL)	/* Flags say he's opped? */
    m->flags |= CHANOP;		/* Yes, so flag in my table */
  else
    m->flags &= ~CHANOP;
  if (strchr(flags, '+') != NULL)	/* Flags say he's voiced? */
    m->flags |= CHANVOICE;	/* Yes */
  else
    m->flags &= ~CHANVOICE;
  if (!(m->flags & (CHANVOICE | CHANOP)))
    m->flags |= STOPWHO;
  if (match_my_nick(nick) && !waschanop && me_op(chan))
    recheck_channel(chan, 1);
  if (match_my_nick(nick) && any_ops(chan) && !me_op(chan) &&
      chan->need_op[0])
    do_tcl("need-op", chan->need_op);
  m->user = get_user_by_host(userhost);
  get_user_flagrec(m->user, &fr, chan->name);
  /* are they a chanop, and me too */
  if (chan_hasop(m) && me_op(chan) &&
  /* are they a channel or global de-op */
      ((chan_deop(fr) || (glob_deop(fr) && !chan_op(fr))) ||
  /* or is it bitch mode & they're not an op anywhere */
       (channel_bitch(chan) && !chan_op(fr) &&
	!(glob_op(fr) && !chan_deop(fr)))) &&
  /* and of course it's not me */
      !match_my_nick(nick)) {
    add_mode(chan, '-', 'o', nick);
  }
  /* if channel is enforce bans */
  if (channel_enforcebans(chan) &&
  /* and user matches a ban */
      (u_match_mask(global_bans, userhost) ||
       u_match_mask(chan->bans, userhost)) &&
  /* and it's not me, and i'm an op */
      !match_my_nick(nick) && me_op(chan) &&
      !chan_friend(fr) && !glob_friend(fr) &&
      !isexempted(chan, userhost) &&
      !(channel_dontkickops(chan) &&
	(chan_op(fr) || (glob_op(fr) && !chan_deop(fr))))) {	/* arthur2 */
    /* *bewm* */
    dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, nick, IRC_BANNED);
    m->flags |= SENTKICK;
  }
  /* if the user is a +k */
  else if ((chan_kick(fr) || glob_kick(fr)) &&
    /* and it's not me :) who'd set me +k anyway, a sicko? */
    /* and if im an op */
	   !match_my_nick(nick) && me_op(chan)) {
    /* cya later! */
    p = get_user(&USERENTRY_COMMENT, m->user);
    quickban(chan, userhost);
    dprintf(DP_SERVER, "KICK %s %s :%s\n", chan->name, nick,
	    p ? p : IRC_POLITEKICK);
    m->flags |= SENTKICK;
  }
  return 0;
}

/* got a 352: who info! */
static int got352(char *from, char *msg)
{
  char *nick, *user, *host, *chname, *flags;
  struct chanset_t *chan;

  context;
  newsplit(&msg);		/* Skip my nick - effeciently */
  chname = newsplit(&msg);	/* Grab the channel */
  chan = findchan(chname);	/* See if I'm on channel */
  if (chan) {			/* Am I? */
    user = newsplit(&msg);	/* Grab the user */
    host = newsplit(&msg);	/* Grab the host */
    newsplit(&msg);		/* Skip the server */
    nick = newsplit(&msg);	/* Grab the nick */
    flags = newsplit(&msg);	/* Grab the flags */
    got352or4(chan, user, host, nick, flags);
  }
  return 0;
}

/* got a 354: who info! - iru style */
static int got354(char *from, char *msg)
{
  char *nick, *user, *host, *chname, *flags;
  struct chanset_t *chan;

  context;
  if (use_354) {
    newsplit(&msg);		/* Skip my nick - effeciently */
    if (msg[0] && (strchr(CHANMETA, msg[0]) != NULL)) {
      chname = newsplit(&msg);	/* Grab the channel */
      chan = findchan(chname);	/* See if I'm on channel */
      if (chan) {		/* Am I? */
	user = newsplit(&msg);	/* Grab the user */
	host = newsplit(&msg);	/* Grab the host */
	nick = newsplit(&msg);	/* Grab the nick */
	flags = newsplit(&msg);	/* Grab the flags */
	got352or4(chan, user, host, nick, flags);
      }
    }
  }
  return 0;
}

/* got 315: end of who */
/* <server> 315 <to> <chan> :End of /who */
static int got315(char *from, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  /* may have left the channel before the who info came in */
  if (!chan || !channel_pending(chan))
    return 0;
  /* finished getting who list, can now be considered officially ON CHANNEL */
  chan->status |= CHAN_ACTIVE;
  chan->status &= ~CHAN_PEND;
  /* am *I* on the channel now? if not, well d0h. */
  if (!ismember(chan, botname)) {
    putlog(LOG_MISC | LOG_JOIN, chan->name, "Oops, I'm not really on %s",
	   chan->name);
    clear_channel(chan, 1);
    chan->status &= ~CHAN_ACTIVE;
    dprintf(DP_MODE, "JOIN %s %s\n", chan->name, chan->key_prot);
  }
  /* do not check for i-lines here. */
  return 0;
}

/* got 367: ban info */
/* <server> 367 <to> <chan> <ban> [placed-by] [timestamp] */
static int got367(char *from, char *msg)
{
  char s[UHOSTLEN], *ban, *who, *chname;
  struct chanset_t *chan;
  struct userrec *u;
  struct flag_record fr =
  {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (!chan || !(channel_pending(chan) || channel_active(chan)))
    return 0;
  ban = newsplit(&msg);
  who = newsplit(&msg);
  /* extended timestamp format? */
  if (who[0])
    newban(chan, ban, who);
  else
    newban(chan, ban, "existent");
  simple_sprintf(s, "%s!%s", botname, botuserhost);
  if (wild_match(ban, s))
    add_mode(chan, '-', 'b', ban);
  u = get_user_by_host(ban);
  if (u) {			/* why bother check no-user :) - of if Im not
				 * an op */
    get_user_flagrec(u, &fr, chan->name);
    if (chan_op(fr) || (glob_op(fr) && !chan_deop(fr)))
      add_mode(chan, '-', 'b', ban);
    /* these will be flushed by 368: end of ban info */
  }
  return 0;
}

/* got 368: end of ban list */
/* <server> 368 <to> <chan> :etc */
static int got368(char *from, char *msg)
{
  struct chanset_t *chan;
  char *chname;

  /* ok, now add bans that i want, which aren't set yet */
  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan) {
    chan->status &= ~CHAN_ASKEDBANS;
    if (channel_clearbans(chan))
      resetbans(chan);
    else {
      masklist *b = chan->channel.ban;
      int bogus;
      char *p;

      if (me_op(chan))
	while (b && b->mask[0]) {
	  bogus = 0;
	  for (p = b->mask; *p; p++)
	    if ((*p < 32) || (*p > 126))
	      bogus = 1;
	  if (bogus)
	    add_mode(chan, '-', 'b', b->mask);
	  b = b->next;
	}
      recheck_bans(chan);
    }
  }
  /* if i sent a mode -b on myself (deban) in got367, either */
  /* resetbans() or recheck_bans() will flush that */
  return 0;
}

/* got 348: ban exemption info */
/* <server> 348 <to> <chan> <exemption>  */
static int got348(char *from, char *msg)
{
  char * exempt, * who, *chname;
  struct chanset_t *chan;
  if (use_exempts == 0)
    return 0;
  newsplit(&msg);
  chname = newsplit(&msg);
  
  chan = findchan(chname);
  if (!chan || !(channel_pending(chan) || channel_active(chan)))
    return 0;
  exempt = newsplit(&msg);
  who = newsplit(&msg);
  /* extended timestamp format? */
  if (who[0])
    newexempt(chan, exempt, who);
  else
    newexempt(chan, exempt, "existent");
  return 0;
}

/* got 349: end of ban exemption list */
/* <server> 349 <to> <chan> :etc */
static int got349(char *from, char *msg)
{
  struct chanset_t *chan;
  char *chname;

  if (use_exempts == 1) {
    newsplit(&msg);
    chname = newsplit(&msg);
    chan = findchan(chname);
    if (chan) {
      chan->ircnet_status &= ~CHAN_ASKED_EXEMPTS;
      if (channel_clearbans(chan))
	resetexempts(chan);
      else {
	masklist *e = chan->channel.exempt;
	int bogus;
	char * p;
	
	if (me_op(chan))
	  while (e->mask[0]) {
	    bogus = 0;
	    for (p = e->mask; *p; p++)
	      if ((*p < 32) || (*p > 126))
		bogus = 1;
	    if (bogus)
	      add_mode(chan, '-', 'e', e->mask);
	    e = e->next;
	  }
	recheck_exempts(chan);
      }
    }  
    
  }
  return 0;
}

/* got 346: invite exemption info */
/* <server> 346 <to> <chan> <exemption> */
static int got346(char *from, char *msg)
{
  char * invite, * who, *chname;
  struct chanset_t *chan;
  if (use_invites == 0)
    return 0;
  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (!chan || !(channel_pending(chan) || channel_active(chan)))
    return 0;
  invite = newsplit(&msg);
  who = newsplit(&msg);
  /* extended timestamp format? */
  if (who[0])
    newinvite(chan, invite, who);
  else
    newinvite(chan, invite, "existent");
  return 0;
}

/* got 347: end of invite exemption list */
/* <server> 347 <to> <chan> :etc */
static int got347(char *from, char *msg)
{
  struct chanset_t *chan;
  char *chname;

  if (use_invites == 1) {
    newsplit(&msg);
    chname = newsplit(&msg);
    chan = findchan(chname);
    if (chan) {
      chan->ircnet_status &= ~CHAN_ASKED_INVITED;
      if (channel_clearbans(chan))
	resetinvites(chan);
      else {
	masklist *inv = chan->channel.invite;
	int bogus;
	char * p;
	
	if (me_op(chan))
	  while (inv && inv->mask[0]) {
	    bogus = 0;
	    for (p = inv->mask; *p; p++)
	      if ((*p < 32) || (*p > 126))
		bogus = 1;
	    if (bogus)
	      add_mode(chan, '-', 'I', inv->mask);
	    inv = inv->next;
	  }
	recheck_invites(chan);
      }
    }
  }
  return 0;
}

/* too many channels */
static int got405(char *from, char *msg)
{
  char *chname;

  newsplit(&msg);
  chname = newsplit(&msg);
  putlog(LOG_MISC, "*", IRC_TOOMANYCHANS, chname);
  return 0;
}

/* got 471: can't join channel, full */
static int got471(char *from, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  putlog(LOG_JOIN, chname, IRC_CHANFULL, chname);
  chan = findchan(chname);
  if (chan && chan->need_limit[0])
    do_tcl("need-limit", chan->need_limit);
  return 0;
}

/* got 473: can't join channel, invite only */
static int got473(char *from, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  putlog(LOG_JOIN, chname, IRC_CHANINVITEONLY, chname);
  chan = findchan(chname);
  if (chan && chan->need_invite[0])
    do_tcl("need-invite", chan->need_invite);
  return 0;
}

/* got 474: can't join channel, banned */
static int got474(char *from, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  putlog(LOG_JOIN, chname, IRC_BANNEDFROMCHAN, chname);
  chan = findchan(chname);
  if (chan && chan->need_unban[0])
    do_tcl("need-unban", chan->need_unban);
  return 0;
}

/* got 442: not on channel */
static int got442(char *from, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan) {
    if (!channel_inactive(chan)) {
      putlog(LOG_MISC, chname, IRC_SERVNOTONCHAN, chname);
      clear_channel(chan, 1);
      chan->status &= ~CHAN_ACTIVE;
      dprintf(DP_MODE, "JOIN %s %s\n", chan->name, chan->key_prot);
    }  
  }
  return 0;
}

/* got 475: can't goin channel, bad key */
static int got475(char *from, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  putlog(LOG_JOIN, chname, IRC_BADCHANKEY, chname);
  chan = findchan(chname);
  if (chan && chan->need_key[0])
    do_tcl("need-key", chan->need_key);
  return 0;
}

/* got invitation */
static int gotinvite(char *from, char *msg)
{
  char *nick;
  struct chanset_t *chan;

  newsplit(&msg);
  fixcolon(msg);
  nick = splitnick(&from);
  if (!rfc_casecmp(last_invchan, msg)) {
    if (now - last_invtime < 30)
      return 0;			/* two invites to the same channel in
				 * 30 seconds? */
  }
  putlog(LOG_MISC, "*", "%s!%s invited me to %s", nick, from, msg);
  strncpy(last_invchan, msg, 299);
  last_invtime = now;
  chan = findchan(msg);
  if (chan && (channel_pending(chan) || channel_active(chan)))
    dprintf(DP_HELP, "NOTICE %s :I'm already here.\n", nick);
  else if (chan && !channel_inactive(chan))
    dprintf(DP_MODE, "JOIN %s %s\n", chan->name, chan->key_prot);
  return 0;
}

/* set the topic */
static void set_topic(struct chanset_t *chan, char *k)
{
  if (chan->channel.topic)
    nfree(chan->channel.topic);
  if (k && k[0]) {
    chan->channel.topic = (char *) channel_malloc(strlen(k) + 1);
    strcpy(chan->channel.topic, k);
  } else
    chan->channel.topic = NULL;
}

/* topic change */
static int gottopic(char *from, char *msg)
{
  char *nick, *chname;
  memberlist *m;
  struct chanset_t *chan;
  struct userrec *u;

  chname = newsplit(&msg);
  fixcolon(msg);
  u = get_user_by_host(from);
  nick = splitnick(&from);
  chan = findchan(chname);
  if (chan) {
    putlog(LOG_JOIN, chname, "Topic changed on %s by %s!%s: %s", chname,
	   nick, from, msg);
    m = ismember(chan, nick);
    if (m != NULL)
      m->last = now;
    set_topic(chan, msg);
    check_tcl_topc(nick, from, u, chname, msg);
  }
  return 0;
}

/* 331: no current topic for this channel */
/* <server> 331 <to> <chname> :etc */
static int got331(char *from, char *msg)
{
  char *chname;
  struct chanset_t *chan;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan) {
    set_topic(chan, NULL);
    check_tcl_topc("*", "*", NULL, chname, "");
  }
  return 0;
}

/* 332: topic on a channel i've just joined */
/* <server> 332 <to> <chname> :topic goes here */
static int got332(char *from, char *msg)
{
  struct chanset_t *chan;
  char *chname;

  newsplit(&msg);
  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan) {
    fixcolon(msg);
    set_topic(chan, msg);
    check_tcl_topc("*", "*", NULL, chname, msg);
  }
  return 0;
}

static void do_embedded_mode(struct chanset_t *chan, char *nick,
			     memberlist * m, char *mode)
{
  struct flag_record fr =
  {0, 0, 0, 0, 0, 0};
  int servidx = findanyidx(serv);

  while (*mode) {
    switch (*mode) {
    case 'o':
      check_tcl_mode(dcc[servidx].host, "", NULL, chan->name, "+o", nick);
      got_op(chan, "", dcc[servidx].host, nick, &fr);
      break;
    case 'v':
      check_tcl_mode(dcc[servidx].host, "", NULL, chan->name, "+v", nick);
      m->flags |= CHANVOICE;
      break;
    }
    mode++;
  }
}

/* join */
static int gotjoin(char *from, char *chname)
{
  char *nick, *p, *newmode, buf[UHOSTLEN], *uhost = buf;
  int ok = 1;
  struct chanset_t *chan;
  memberlist *m;
  masklist *b, *e;
  struct userrec *u;
  struct flag_record fr =
  {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  context;
  fixcolon(chname);
  /* ircd 2.9 sometimes does '#chname^Gmodes' when returning from splits */
  newmode = NULL;
  p = strchr(chname, 7);
  if (p != NULL) {
    *p = 0;
    newmode = (++p);
  }
  chan = findchan(chname);
  if (!chan || channel_inactive(chan)) {
    putlog(LOG_MISC, "*", "joined %s but didn't want to!", chname);
    dprintf(DP_MODE, "PART %s\n", chname);
  } else if (!channel_pending(chan)) {
    strcpy(uhost, from);
    nick = splitnick(&uhost);
    detect_chan_flood(nick, uhost, from, chan, FLOOD_JOIN, NULL);
    /* grab last time joined before we update it */
    u = get_user_by_host(from);
    get_user_flagrec(u, &fr, chname);
    if (!channel_active(chan) && !match_my_nick(nick)) {
      /* uh, what?!  i'm on the channel?! */
      putlog(LOG_MISC, chname,
	  "confused bot: guess I'm on %s and didn't realize it", chname);
      chan->status |= CHAN_ACTIVE;
      chan->status &= ~CHAN_PEND;
      reset_chan_info(chan);
    } else {
      m = ismember(chan, nick);
      if (m && m->split && !strcasecmp(m->userhost, uhost)) {
	check_tcl_rejn(nick, uhost, u, chan->name);
	m->split = 0;
	m->last = now;
	m->delay = 0L;
        m->flags = (chan_hasop(m) ? WASOP : 0);
	m->user = u;
	set_handle_laston(chname, u, now);
	/* had ops before split, Im an op */
	if (chan_wasop(m) && me_op(chan) &&
	/* and the user is a valid op... */
	    (chan_op(fr) || (glob_op(fr) && !chan_deop(fr))) &&
	/* channel is +autoop... */
	    (channel_autoop(chan)
	/* OR user is maked autoop */
	     || glob_autoop(fr) || chan_autoop(fr))) {
	  /* give them a special surprise */
          m->delay = now;
	  /* also prevent +stopnethack automatically de-opping them */
	  m->flags |= WASOP;
	}
	m->flags |= STOPWHO;
	if (newmode) {
	  putlog(LOG_JOIN, chname, "%s (%s) returned to %s (with +%s).",
		 nick, uhost, chname, newmode);
	  do_embedded_mode(chan, nick, m, newmode);
	} else
	  putlog(LOG_JOIN, chname, "%s (%s) returned to %s.", nick, uhost,
		 chname);
      } else {
	if (m)
	  killmember(chan, nick);
	m = newmember(chan);
	m->joined = now;
	m->split = 0L;
	m->flags = 0;
	m->last = now;
	m->delay = 0L;
        strcpy(m->nick, nick);
	strcpy(m->userhost, uhost);
	m->user = u;
	m->flags |= STOPWHO;
	check_tcl_join(nick, uhost, u, chname);
	if (newmode)
	  do_embedded_mode(chan, nick, m, newmode);
	if (match_my_nick(nick)) {
	  /* it was me joining! */
	  if (newmode)
	    putlog(LOG_JOIN | LOG_MISC, chname,
		   "%s joined %s (with +%s).",
		   nick, chname, newmode);
	  else
	    putlog(LOG_JOIN | LOG_MISC, chname,
		   "%s joined %s.", nick, chname);
	  reset_chan_info(chan);
	} else {
	  struct chanuserrec *cr;
	  char *p;

	  if (newmode)
	    putlog(LOG_JOIN, chname,
		   "%s (%s) joined %s (with +%s).", nick,
		   uhost, chname, newmode);
	  else
	    putlog(LOG_JOIN, chname,
		   "%s (%s) joined %s.", nick, uhost, chname);
	  if (me_op(chan))
	    for (p = m->userhost; *p; p++)
	      if (((unsigned char) *p) < 32) {
		if (ban_bogus)
		  u_addban(chan, quickban(chan, uhost), origbotname,
			   CHAN_BOGUSUSERNAME, now + (60 * ban_time), 0);
		if (kick_bogus) {
		  dprintf(DP_MODE, "KICK %s %s :%s\n",
			  chname, nick, CHAN_BOGUSUSERNAME);
		  m->flags |= SENTKICK;
		}
		return 0;
	      }
	  /* ok, the op-on-join,etc, tests...first only both if Im opped */
	  if (me_op(chan)) {
	    /* Check for and reset exempts and invites
	     * this will require further checking to account for when
	     * to use the various modes */
	    if (u_match_mask(global_invites,from) || 
		u_match_mask(chan->invites, from))
	      refresh_invite(chan, from);
	    /* are they a chan op, or global op without chan deop */
	    if ((chan_op(fr) || (glob_op(fr) && !chan_deop(fr))) &&
		/* is it op-on-join or is the use marked auto-op */
		(channel_autoop(chan) || glob_autoop(fr) || chan_autoop(fr))) {
	      /* yes! do the honors */
              m->delay = now;
	      m->flags |= WASOP;	/* nethack sanity */
	      /* if it matches a ban, dispose of them */
	    } else {
	      if (u_match_mask(global_bans, from) ||
		  u_match_mask(chan->bans, from)) {
		refresh_ban_kick(chan, from, nick);
		refresh_exempt(chan, from);
		/* likewise for kick'ees */
	      } else if (glob_kick(fr) || chan_kick(fr)) {
		quickban(chan, from);
		refresh_exempt(chan, from);
		p = get_user(&USERENTRY_COMMENT, m->user);
		dprintf(DP_MODE, "KICK %s %s :%s\n", chname, nick,
			(p && (p[0] != '@')) ? p : IRC_COMMENTKICK);
		m->flags |= SENTKICK;
	      } else if ((channel_autovoice(chan) &&
		 (chan_voice(fr) || (glob_voice(fr) && !chan_quiet(fr)))) ||
		 ((glob_gvoice(fr) || chan_gvoice(fr)) && !chan_quiet(fr))) {
		add_mode(chan, '+', 'v', nick);
		 }
	    }
	  }
	  /* don't re-display greeting if they've been on the channel
	   * recently */
	  if (u) {
	    struct laston_info *li = 0;

	    cr = get_chanrec(m->user, chan->name);
	    if (!cr && no_chanrec_info)
	      li = get_user(&USERENTRY_LASTON, m->user);
	    if (channel_greet(chan) && use_info &&
		((cr && (now - cr->laston > wait_info)) ||
		 (no_chanrec_info &&
		  (!li || (now - li->laston > wait_info))))) {
	      char s1[512], *s;

	      if (!(u->flags & USER_BOT)) {
		s = get_user(&USERENTRY_INFO, u);
		get_handle_chaninfo(u->handle, chan->name, s1);
		/* locked info line overides non-locked channel specific
		 * info line */
		if (!s || (s1[0] && (s[0] != '@' || s1[0] == '@')))
		  s = s1;
		if (s[0] == '@')
		  s++;
		if (s && s[0])
		  dprintf(DP_HELP, "PRIVMSG %s :[%s] %s\n",
			  chan->name, nick, s);
	      }
	    }
	  }
	  set_handle_laston(chname, u, now);
	}
      }
      if (channel_enforcebans(chan) && me_op(chan) &&
          !chan_op(fr) && !glob_op(fr) && !glob_friend(fr) && !chan_friend(fr)) {
        for (b = chan->channel.ban; b->mask[0]; b = b->next) { 
          if (wild_match(b->mask, from)) {
   	    if (use_exempts)
	      for (e = chan->channel.exempt; e->mask[0]; e = e->next)
	        if (wild_match(e->mask, from))
	          ok = 0;
	    if (ok && !chan_sentkick(m)) {
	      dprintf(DP_SERVER, "KICK %s %s :%s\n", chname, m->nick,
		      IRC_YOUREBANNED);
	      m->flags |= SENTKICK;    
            }
	  }
	}
      }
    }
  }
  return 0;
}

/* part */
static int gotpart(char *from, char *chname)
{
  char *nick;
  struct chanset_t *chan;
  struct userrec *u;

  fixcolon(chname);
  chan = findchan(chname);
  if (chan && channel_inactive(chan)) {
    clear_channel(chan, 1);  
    chan->status &= ~(CHAN_ACTIVE | CHAN_PEND);
    return 0;
  }
  if (chan && !channel_pending(chan)) {
    u = get_user_by_host(from);
    nick = splitnick(&from);
    if (!channel_active(chan)) {
      /* whoa! */
      putlog(LOG_MISC, chname,
	  "confused bot: guess I'm on %s and didn't realize it", chname);
      chan->status |= CHAN_ACTIVE;
      chan->status &= ~CHAN_PEND;
      reset_chan_info(chan);
    }
    check_tcl_part(nick, from, u, chname);
    set_handle_laston(chname, u, now);
    killmember(chan, nick);
    putlog(LOG_JOIN, chname, "%s (%s) left %s.", nick, from, chname);
    /* if it was me, all hell breaks loose... */
    if (match_my_nick(nick)) {
      clear_channel(chan, 1);
      chan->status &= ~(CHAN_ACTIVE | CHAN_PEND);
      if (!channel_inactive(chan))
	dprintf(DP_MODE, "JOIN %s %s\n", chan->name, chan->key_prot);
    } else
      check_lonely_channel(chan);
  }
  return 0;
}

/* kick */
static int gotkick(char *from, char *msg)
{
  char *nick, *whodid, *chname, s1[UHOSTLEN], buf[UHOSTLEN], *uhost = buf;
  memberlist *m;
  struct chanset_t *chan;
  struct userrec *u;
  struct flag_record fr = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  chname = newsplit(&msg);
  chan = findchan(chname);
  if (chan && !channel_pending(chan)) {
    nick = newsplit(&msg);
    fixcolon(msg);
    u = get_user_by_host(from);
    strcpy(uhost, from);
    whodid = splitnick(&uhost);
    detect_chan_flood(whodid, uhost, from, chan, FLOOD_KICK, nick);
    m = ismember(chan, whodid);
    if (m)
      m->last = now;
    check_tcl_kick(whodid, uhost, u, chname, nick, msg);
    get_user_flagrec(u, &fr, chname);
    m = ismember(chan, nick);
    if (m) {
      struct userrec *u2;

      simple_sprintf(s1, "%s!%s", m->nick, m->userhost);
      u2 = get_user_by_host(s1);
      set_handle_laston(chname, u2, now);
      maybe_revenge(chan, from, s1, REVENGE_KICK);
    }
    putlog(LOG_MODES, chname, "%s kicked from %s by %s: %s", s1, chname,
	   from, msg);
    /* kicked ME?!? the sods! */
    if (match_my_nick(nick)) {
      chan->status &= ~(CHAN_ACTIVE | CHAN_PEND);
      dprintf(DP_MODE, "JOIN %s %s\n", chan->name, chan->key_prot);
      clear_channel(chan, 1);
    } else {
      killmember(chan, nick);
      check_lonely_channel(chan);
    }
  }
  return 0;
}

/* nick change */
static int gotnick(char *from, char *msg)
{
  char *nick, s1[UHOSTLEN], buf[UHOSTLEN], *uhost = buf;
  memberlist *m, *mm;
  struct chanset_t *chan;
  struct userrec *u;

  u = get_user_by_host(from);
  strcpy(uhost, from);
  nick = splitnick(&uhost);
  fixcolon(msg);
  chan = chanset;
  while (chan) {
    m = ismember(chan, nick);
    if (m) {
      putlog(LOG_JOIN, chan->name, "Nick change: %s -> %s", nick, msg);
      m->last = now;
      if (rfc_casecmp(nick, msg)) {
	/* not just a capitalization change */
	mm = ismember(chan, msg);
	if (mm) {
	  /* someone on channel with old nick?! */
	  if (mm->split)
	    putlog(LOG_JOIN, chan->name,
		   "Possible future nick collision: %s", mm->nick);
	  else
	    putlog(LOG_MISC, chan->name,
		   "* Bug: nick change to existing nick");
	  killmember(chan, mm->nick);
	}
      }
      /* banned? */
      /* compose a nick!user@host for the new nick */
      sprintf(s1, "%s!%s", msg, uhost);
      /* enforcing bans & haven't already kicked them? */
      if (channel_enforcebans(chan) && chan_sentkick(m) &&
	  (u_match_mask(global_bans, s1) || u_match_mask(chan->bans, s1)))
	refresh_ban_kick(chan, s1, msg);
      strcpy(m->nick, msg);
      detect_chan_flood(nick, uhost, from, chan, FLOOD_NICK, NULL);
      /* any pending kick to the old nick is lost. Ernst 18/3/1998 */
      if (chan_sentkick(m))
	m->flags &= ~SENTKICK;
      check_tcl_nick(nick, uhost, u, chan->name, msg);
    }
    chan = chan->next;
  }
  clear_chanlist();		/* cache is meaningless now */
  return 0;
}

/* signoff, similar to part */
static int gotquit(char *from, char *msg)
{
  char *nick, *p, *alt;
  int split = 0;
  memberlist *m;
  struct chanset_t *chan;
  struct userrec *u;

  u = get_user_by_host(from);
  nick = splitnick(&from);
  fixcolon(msg);
  /* Fred1: instead of expensive wild_match on signoff, quicker method */
  /* determine if signoff string matches "%.% %.%", and only one space */
  p = strchr(msg, ' ');
  if (p && (p == strrchr(msg, ' '))) {
    char *z1, *z2;

    *p = 0;
    z1 = strchr(p + 1, '.');
    z2 = strchr(msg, '.');
    if (z1 && z2 && (*(z1 + 1) != 0) && (z1 - 1 != p) &&
	(z2 + 1 != p) && (z2 != msg)) {
      /* server split, or else it looked like it anyway */
      /* (no harm in assuming) */
      split = 1;
    } else
      *p = ' ';
  }
  for (chan = chanset; chan; chan = chan->next) {
    m = ismember(chan, nick);
    if (m) {
      set_handle_laston(chan->name, u, now);
      if (split) {
	m->split = now;
	check_tcl_splt(nick, from, u, chan->name);
	putlog(LOG_JOIN, chan->name, "%s (%s) got netsplit.", nick,
	       from);
      } else {
	check_tcl_sign(nick, from, u, chan->name, msg);
	putlog(LOG_JOIN, chan->name, "%s (%s) left irc: %s", nick,
	       from, msg);
	killmember(chan, nick);
	check_lonely_channel(chan);
      }
    }
  }
  /* our nick quit? if so, grab it */
  /* heck, our altnick quit maybe, maybe we want it */
  if (keepnick) {
    alt = get_altbotnick();
    if (!rfc_casecmp(nick, origbotname)) {
      putlog(LOG_MISC, "*", IRC_GETORIGNICK, origbotname);
      dprintf(DP_MODE, "NICK %s\n", origbotname);
    } else if (alt[0]) {
      if (!rfc_casecmp(nick, alt) && strcmp(botname, origbotname)) {
	putlog(LOG_MISC, "*", IRC_GETALTNICK, alt);
	dprintf(DP_MODE, "NICK %s\n", alt);
      }
    }
  }
  return 0;
}

/* private message */
static int gotmsg(char *from, char *msg)
{
  char *to, *realto, buf[UHOSTLEN], *nick, buf2[512], *uhost = buf;
  char *p, *p1, *code, *ctcp;
  int ctcp_count = 0;
  struct chanset_t *chan;
  int ignoring;
  struct userrec *u;
  memberlist *m;
  struct flag_record fr =
  {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

  if (!strchr("&#@$", msg[0]))
    return 0;
  ignoring = match_ignore(from);
  to = newsplit(&msg);
  realto = (to[0] == '@') ? to + 1 : to;
  chan = findchan(realto);
  if (!chan)
    return 0;			/* privmsg to an unknown channel ?? */
  fixcolon(msg);
  strcpy(uhost, from);
  nick = splitnick(&uhost);
  /* only check if flood-ctcp is active */
  if (flud_ctcp_thr && detect_avalanche(msg)) {
    u = get_user_by_host(from);
    get_user_flagrec(u, &fr, chan->name);
    /* discard -- kick user if it was to the channel */
    if (me_op(chan) &&
	!chan_friend(fr) && !glob_friend(fr) &&
	!(channel_dontkickops(chan) &&
	  (chan_op(fr) || (glob_op(fr) && !chan_deop(fr))))) {	/* arthur2 */
      m = ismember(chan, nick);
      if (m && !chan_sentkick(m)) {
	if (ban_fun)
	  u_addban(chan, quickban(chan, uhost), origbotname,
		   IRC_FUNKICK, now + (60 * ban_time), 0);
	if (kick_fun) {
	  dprintf(DP_SERVER, "KICK %s %s :%s\n",
		  realto, nick, IRC_FUNKICK);	/* this can induce kickflood - arthur2 */
	  m->flags |= SENTKICK;
	}
      }
    }
    if (!ignoring) {
      putlog(LOG_MODES, "*", "Avalanche from %s!%s in %s - ignoring",
	     nick, uhost, realto);
      p = strchr(uhost, '@');
      if (p)
	p++;
      else
	p = uhost;
      simple_sprintf(buf2, "*!*@%s", p);
      addignore(buf2, origbotname, "ctcp avalanche", now + (60 * ignore_time));
    }
    return 0;
  }
  /* check for CTCP: */
  ctcp_reply[0] = 0;
  p = strchr(msg, 1);
  while (p && *p) {
    p++;
    p1 = p;
    while ((*p != 1) && *p)
      p++;
    if (*p == 1) {
      *p = 0;
      ctcp = buf2;
      strcpy(ctcp, p1);
      strcpy(p1 - 1, p + 1);
      detect_chan_flood(nick, uhost, from, chan,
			strncmp(ctcp, "ACTION ", 7) ?
			FLOOD_CTCP : FLOOD_PRIVMSG, NULL);
      /* respond to the first answer_ctcp */
      p = strchr(msg, 1);
      if (ctcp_count < answer_ctcp) {
	ctcp_count++;
	if (ctcp[0] != ' ') {
	  code = newsplit(&ctcp);
	  u = get_user_by_host(from);
	  if (!ignoring || trigger_on_ignore) {
	    if (!check_tcl_ctcp(nick, uhost, u, to, code, ctcp))
	      update_idle(realto, nick);
	    if (!ignoring) {
	      /* hell! log DCC, it's too a channel damnit! */
	      if (strcmp(code, "ACTION") == 0) {
		putlog(LOG_PUBLIC, realto, "Action: %s %s", nick, ctcp);
	      } else {
		putlog(LOG_PUBLIC, realto, "CTCP %s: %s from %s (%s) to %s",
		       code, ctcp, nick, from, to);
	      }
	    }
	  }
	}
      }
    }
  }
  /* send out possible ctcp responses */
  if (ctcp_reply[0]) {
    if (ctcp_mode != 2) {
      dprintf(DP_SERVER, "NOTICE %s :%s\n", nick, ctcp_reply);
    } else {
      if (now - last_ctcp > flud_ctcp_time) {
	dprintf(DP_SERVER, "NOTICE %s :%s\n", nick, ctcp_reply);
	count_ctcp = 1;
      } else if (count_ctcp < flud_ctcp_thr) {
	dprintf(DP_SERVER, "NOTICE %s :%s\n", nick, ctcp_reply);
	count_ctcp++;
      }
      last_ctcp = now;
    }
  }
  if (msg[0]) {
    /* if (!ignoring) */ /* removed by Eule 17.7.99 */ 
      detect_chan_flood(nick, uhost, from, chan, FLOOD_PRIVMSG, NULL);
    if (!ignoring || trigger_on_ignore) {
      if (check_tcl_pub(nick, uhost, realto, msg))
	return 0;
      check_tcl_pubm(nick, uhost, realto, msg);
    }
    if (!ignoring) {
      if (to[0] == '@')
	putlog(LOG_PUBLIC, realto, "@<%s> %s", nick, msg);
      else
	putlog(LOG_PUBLIC, realto, "<%s> %s", nick, msg);
    }
    update_idle(realto, nick);
  }
  return 0;
}

/* private notice */
static int gotnotice(char *from, char *msg)
{
  char *to, *realto, *nick, buf2[512], *p, *p1, buf[512], *uhost = buf;
  char *ctcp, *code;
  struct userrec *u;
  memberlist *m;
  struct chanset_t *chan;
  struct flag_record fr =
  {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
  int ignoring;

  if (!strchr("#&+@", *msg))
    return 0;
  ignoring = match_ignore(from);
  to = newsplit(&msg);
  realto = (*to == '@') ? to + 1 : to;
  chan = findchan(realto);
  fixcolon(msg);
  strcpy(uhost, from);
  nick = splitnick(&uhost);
  if (flud_ctcp_thr && detect_avalanche(msg)) {
    u = get_user_by_host(from);
    get_user_flagrec(u, &fr, chan->name);
    /* discard -- kick user if it was to the channel */
    if (me_op(chan) &&
	!chan_friend(fr) && !glob_friend(fr) &&
	!(channel_dontkickops(chan) &&
	  (chan_op(fr) || (glob_op(fr) && !chan_deop(fr))))) {	/* arthur2 */
      m = ismember(chan, nick);
      if (m || !chan_sentkick(m)) {
	if (ban_fun)
	  u_addban(chan, quickban(chan, uhost), origbotname,
		   IRC_FUNKICK, now + (60 * ban_time), 0);
	if (kick_fun) {
	  dprintf(DP_SERVER, "KICK %s %s :%s\n",
		  realto, nick, IRC_FUNKICK);	/* this can induce kickflood - arthur2 */
	  m->flags |= SENTKICK;
	}
      }
    }
    if (!ignoring)
      putlog(LOG_MODES, "*", "Avalanche from %s", from);
    return 0;
  }
  /* check for CTCP: */
  p = strchr(msg, 1);
  while (p && *p) {
    p++;
    p1 = p;
    while ((*p != 1) && *p)
      p++;
    if (*p == 1) {
      *p = 0;
      ctcp = buf2;
      strcpy(ctcp, p1);
      strcpy(p1 - 1, p + 1);
      p = strchr(msg, 1);
      /* if (!ignoring) */ /* removed by Eule 17.7.99 */
	detect_chan_flood(nick, uhost, from, chan,
			  strncmp(ctcp, "ACTION ", 7) ?
			  FLOOD_CTCP : FLOOD_PRIVMSG, NULL);
      if (ctcp[0] != ' ') {
	code = newsplit(&ctcp);
	u = get_user_by_host(from);
	if (!ignoring || trigger_on_ignore) {
	  check_tcl_ctcr(nick, uhost, u, to, code, msg);
	  if (!ignoring) {
	    putlog(LOG_PUBLIC, to, "CTCP reply %s: %s from %s (%s) to %s",
		   code, msg, nick, from, to);
	    update_idle(realto, nick);
	  }
	}
      }
    }
  }
  if (msg[0]) {
    /* if (!ignoring) */ /* removed by Eule 17.7.99 */
      detect_chan_flood(nick, uhost, from, chan, FLOOD_NOTICE, NULL);
    if (!ignoring)
      putlog(LOG_PUBLIC, realto, "-%s:%s- %s", nick, to, msg);
    update_idle(realto, nick);
  }
  return 0;
}

/* update the add/rem_builtins in irc.c if you add to this list!! */
static cmd_t irc_raw[] =
{
  {"324", "", (Function) got324, "irc:324"},
  {"352", "", (Function) got352, "irc:352"},
  {"354", "", (Function) got354, "irc:354"},
  {"315", "", (Function) got315, "irc:315"},
  {"367", "", (Function) got367, "irc:367"},
  {"368", "", (Function) got368, "irc:368"},
  {"405", "", (Function) got405, "irc:405"},
  {"471", "", (Function) got471, "irc:471"},
  {"473", "", (Function) got473, "irc:473"},
  {"474", "", (Function) got474, "irc:474"},
  {"442", "", (Function) got442, "irc:442"},
  {"475", "", (Function) got475, "irc:475"},
  {"INVITE", "", (Function) gotinvite, "irc:invite"},
  {"TOPIC", "", (Function) gottopic, "irc:topic"},
  {"331", "", (Function) got331, "irc:331"},
  {"332", "", (Function) got332, "irc:332"},
  {"JOIN", "", (Function) gotjoin, "irc:join"},
  {"PART", "", (Function) gotpart, "irc:part"},
  {"KICK", "", (Function) gotkick, "irc:kick"},
  {"NICK", "", (Function) gotnick, "irc:nick"},
  {"QUIT", "", (Function) gotquit, "irc:quit"},
  {"PRIVMSG", "", (Function) gotmsg, "irc:msg"},
  {"NOTICE", "", (Function) gotnotice, "irc:notice"},
  {"MODE", "", (Function) gotmode, "irc:mode"},
/* Added for IRCnet +e/+I support - Daemus 2/4/1999 */
  {"346", "", (Function) got346, "irc:346"},
  {"347", "", (Function) got347, "irc:347"},
  {"348", "", (Function) got348, "irc:348"},
  {"349", "", (Function) got349, "irc:349"},
  {0, 0, 0, 0}
};


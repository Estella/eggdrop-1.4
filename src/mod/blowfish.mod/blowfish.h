/* modified 19jul1996 by robey -- uses autoconf values now */
#ifndef _H_BLOWFISH
#define _H_BLOWFISH

#define MAXKEYBYTES 56		/* 448 bits */
#define bf_N             16
#define noErr            0
#define DATAERROR        -1
#define KEYBYTES         8

#define UBYTE_08bits  unsigned char
#define UWORD_16bits  unsigned short

#if SIZEOF_INT==4
#define UWORD_32bits  unsigned int
#else
#if SIZEOF_LONG==4
#define UWORD_32bits  unsigned long
#endif
#endif

/* choose a byte order for your hardware */

#ifdef WORDS_BIGENDIAN
/* ABCD - big endian - motorola */
union aword {
  UWORD_32bits word;
  UBYTE_08bits byte[4];
  struct {
    unsigned int byte0:8;
    unsigned int byte1:8;
    unsigned int byte2:8;
    unsigned int byte3:8;
  } w;
};

#endif				/* WORDS_BIGENDIAN */

#ifndef WORDS_BIGENDIAN
/* DCBA - little endian - intel */
union aword {
  UWORD_32bits word;
  UBYTE_08bits byte[4];
  struct {
    unsigned int byte3:8;
    unsigned int byte2:8;
    unsigned int byte1:8;
    unsigned int byte0:8;
  } w;
};

#endif				/* !WORDS_BIGENDIAN */

#endif

#ifndef _APPDEF_H_
#define _APPDEF_H_

/* useful definitions */
#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef SUCCESS
#define SUCCESS 0
#endif

#ifndef FAILURE
#define FAILURE (-1)
#endif

#ifndef INT_MAX
#ifdef MAX_INT
#define INT_MAX MAXINT
#endif
#endif

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef NELEMS
#define NELEMS(x) (sizeof(x)/sizeof(x[0]))
#endif

#ifndef SKIP_CHARS
#define SKIP_CHARS(x)  while(!isspace((int)*x)) (x)++
#endif

#ifndef SKIP_SPACES
#define SKIP_SPACES(x) while (isspace((int)*x)) (x)++ 
#endif

#ifndef SKIP_WORD
#define SKIP_WORD(x)   do { SKIP_SPACES(x); \
                            SKIP_CHARS(x);  \
                            SKIP_SPACES(x);} while(0)
#endif

#endif //_APPDEF_H_

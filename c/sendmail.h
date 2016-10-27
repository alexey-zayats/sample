/* $Id: sendmail.h,v 1.5 2007/10/17 10:57:50 anton Exp $ */

#ifndef CMS1_SENDMAIL_H
#define CMS1_SENDMAIL_H

#include "functions.h"
#include "symrec.h"
#include "array.h"
#include "lexer.h"
#include "util.h"
#include "cookie.h"
#include "request_const.h"
#include "typedef.h"

#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <ctype.h>

table *cms1_mailto( Lexer *lexer, table *headers, table *msg, array_header *attach );

#endif // CMS1_SENDMAIL_H

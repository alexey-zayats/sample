/* $Id: nodes.h,v 1.8 2007/10/17 10:57:50 anton Exp $ */

#ifndef _NODES_H
#define _NODES_H

#include "typedef.h"

Node *new_Node( Lexer *lexer, int type, ... );
void ReduceNode( Lexer *lexer, Node *parent, Node *current );
void SetVariable( Lexer *lexer, Node *var, int op, Node *current );
int CallFunction( Lexer *lexer, char *name, void **data, double *num );
void ClearNodes( Node *n );

#endif // _NODES_H

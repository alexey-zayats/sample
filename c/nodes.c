/* $Id: nodes.c,v 1.236 2007/11/02 12:51:02 anton Exp $ */

#include "nodes.h"
#include "lexer.h"
#include "grammar.h"
#include "symrec.h"
#include "array.h"
#include "util.h"
#include "cookie.h"
#include "functions.h"

#include "database.h"
#include "global.h"
#include "log.h"
#include "master_args.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <md5.h>

extern int _cache_set( const char *key, const char *val, const char *expire );
extern char *_cache_get( const char *key );

extern ContentBuff *out;
extern int isOctalDigit( unsigned short c );
extern int isDecimalDigit( unsigned short c );
extern pcre *re_compile ( Lexer *lexer, const char *pattern, int *options );
extern char *preprocessor( const char *haystack, size_t haystack_len );
extern void get_microtime ( double *num, char *str );
extern int cms_pcre_match_compiled (Lexer *lexer, pcre *re,
						   const char *subject, int offset );

void DoExec( Lexer *lexer, const char *sql );
char *FindPageElement( Lexer *lexer, const char *e );
int yyparse( Lexer *lexer );

int isLegalVarName( const char *s );
int isIdentLetter( unsigned short c );
char *tabs( int num );

void debugNode( const Node *n );
const char *nameByType( int i );
void debugSqlError( Lexer *lexer, table *sqlStat, const char *msg );

/* XXX it is too long to be inlined (and GNU compiler is correctly
 * pointing it out) */
/* static inline void setnumstr( char **str, double num ) */
void setnumstr( char **str, double num );

/*static */void setnumstr( char **str, double num )
{
	if ( *str ) free( *str );
	if ( num == (double)0 ) 
		*str = strdup( "0" ); 
	else 
	{ 
		*str = malloc( 80 );
		double x, y;
		x = modf( num, &y );
		if ( x == (double)0 ) sprintf( *str, "%.lf", num ); /* XXX exact comparison on inexacts */
		else sprintf( *str, "%lf", num ); 
		*str = realloc( *str, strlen(*str)+1 );
	}
}

static void
setnumstr_2(double p_num, char* p_buf, size_t p_bufsz)
{
    double  intgr;
    double  fract;

    fract = modf(p_num, &intgr);

    if(fract != 0){ /* XXX exact comparison on inexacts */
        snprintf(p_buf, p_bufsz, "%lf",  p_num);
    } else {
        snprintf(p_buf, p_bufsz, "%.lf", p_num);
    };
}

Node *new_Node( Lexer *lexer, int type, ... )
{
// 	fprintf( stderr, "Node *new_Node( %s )\n", nameByType( type ) );

	va_list ap;
	Node *n1 = NULL;
	Node *n2 = NULL;

	Node *n = (Node*)malloc( sizeof(Node) );

	n->type = type;
	n->elements = NULL;
	n->expr1 = NULL;
	n->expr2 = NULL;
	n->expr3 = NULL;
	n->args = 0;
	n->logical = NULL;
	n->stat = NULL;
	n->prog = NULL;
	n->next = NULL;
	n->prev = NULL;
	n->oper = -1;
	n->num  = 0;
	n->num2 = 0;
	n->str  = NULL;
	n->pattern  = NULL;
	n->flags = NULL;
	n->ident = NULL;
	n->list = NULL;
	n->element = NULL;
	n->collate = type;

	va_start( ap, type );

	switch ( type ) {
		case StatementListNode:
			n1 = va_arg(ap, Node *);
			n2 = va_arg(ap, Node *);
			if ( n1 ) {
				n->next = n1;
				n1->prev = n;
			}
			n->element = n2;
			break;
		case SourceElementListNode:
			n1 = va_arg(ap, Node *);
			n2 = va_arg(ap, Node *);
			if ( n1 ) {
				n->next = n1;
				n1->prev = n;
			}
			n->element = n2;
			break;
		case StatementNode:
			n->stat = va_arg(ap, Node *);
			break;
		case SourceElementNode:
			n->stat = va_arg(ap, Node *);
			break;
		case ProgramNode:
			n->prog = va_arg(ap, Node *);
			break;
		case ArgumentsNode:
			n1 = va_arg(ap, Node *);
			if ( n1 ) n->expr1 = n1;
			break;
        case ArgumentListNode:
            n1 = va_arg(ap, Node *);
            n2 = va_arg(ap, Node *);
            if ( n1 ) {
                n->next = n1;
                n1->prev = n;
            }
            n->expr1 = n2;
            break;
		case BlockNode:
			n1 = va_arg( ap, Node*);
			if ( n1 ) n->list = n1;
			break;
        case ElementPathNode:
            n1 = va_arg(ap, Node *);
            if ( n1 ) n->next = n1;
            n->str = va_arg( ap, char* );
            break;
		case EmptyStatementNode:
			break;
		case ExprStatementNode:
			n->expr1 = va_arg( ap, Node*);
			break;
		case NumberNode:
			n->num = va_arg( ap, double);
			break;
		case StringNode:
			n->str = va_arg( ap, char* );
			break;
		case CommaNode:
			n1 = va_arg( ap, Node*);
			n2 = va_arg( ap, Node*);
			n->expr1 = n1;
			n->expr2 = n2;
			break;
		case ArrayNode:
			n->expr1 = va_arg( ap, Node* );
			break;
		case FunctionCallNode:
			n->ident = va_arg(ap, char*);
			n1 = va_arg( ap, Node* );
			if ( n1 ) n->args = n1;
            n2 = va_arg( ap, Node* );
            if ( n2 ) n->expr1 = n2;
			break;
		case PostfixNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			break;
		case PrefixNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			break;
		case MultNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case AddNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case ShiftNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case RelationalNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case EqualNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case BitOperNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case LogicalNode:
			n->oper = va_arg( ap, int);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case ConditionalNode:
			n->logical = va_arg( ap, Node*);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case AssignNode:
			n->expr1 = va_arg( ap, Node*);
			n->stat = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case AssigOpNode:
			n->oper = va_arg( ap, int);
			break;
		case IfStatementNode:
			n->logical = va_arg( ap, Node*);
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		case ElseStamentNode:
			n->expr1 = va_arg( ap, Node*);
			break;
		case DoWhileNode:
			n->list = va_arg( ap, Node*);
			n->logical = va_arg( ap, Node*);
			break;
		case WhileNode:
			n->logical = va_arg( ap, Node*);
			n->list = va_arg( ap, Node*);
			break;
		case ForNode:
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			n->expr3 = va_arg( ap, Node*);
			n->list = va_arg( ap, Node*);
			break;
		case ContinueNode:
			n->expr1 = va_arg( ap, Node* );
			break;
		case BreakNode:
// 			n->expr1 = va_arg( ap, Node* );
			break;
		case ReturnNode:
			n1 = va_arg(ap, Node*);
			if ( n1 ) n->expr1 = n1;
			break;
		case ArrayItemNode:
			n->expr1 = va_arg( ap, Node* ); // IDENT
			n->args = va_arg( ap, Node* ); // INDEX EXPRESSION
			break;
		case IdentNode:
			n->ident = va_arg(ap, char*);
			n->oper = va_arg( ap, int );
			break;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case ForSqlNode:
			n->ident = va_arg(ap, char*);    // array symrec
			n->expr2 = va_arg( ap, Node*);    // sql statement
			n->expr3 = va_arg( ap, Node*);    // statement list
			n->oper = va_arg( ap, int);    // statement list
			break;
		case ExecuteNode:
			n->str = va_arg( ap, char*); // HASH IDENT
			n->expr1 = va_arg( ap, Node*);    // sql statement
			break;
		case SelectStatementNode:
		{
			n->str = va_arg( ap, char*); // HASH IDENT
			n->expr1 = va_arg( ap, Node*);    // fields
			break;
		}
		case ForInNode:
			n->ident = va_arg(ap, char*);
			n->expr2 = va_arg( ap, Node*);
			n->expr3 = va_arg( ap, Node*);
			break;
		case SelectNode:
			n->expr1 = va_arg( ap, Node*);
			break;
		case InsertNode:
			n->expr1 = va_arg(ap, Node *);
			break;
		case UpdateNode:
			n->expr1 = va_arg(ap, Node *);
			break;
		case DeleteNode:
			n->expr1 = va_arg(ap, Node *);
			break;
		case SqlClauseNode:
			n1 = va_arg(ap, Node *);
			n->expr1 = va_arg(ap, Node*);
			if ( n1 ) n->next = n1;
			break;
        case SqlTokenNode:
            n->oper = va_arg( ap, int);
            switch ( n->oper ) 
            {
                case 1: n->expr1 = va_arg( ap, Node*); break;
                case 2: {
					setnumstr( &(n->str), va_arg( ap, double) );
                    break;
                }
                case 3: {
                    char *t = va_arg( ap, char* ); 
                    n->str = malloc( strlen(t) + 3 );
                    sprintf( n->str, "'%s'", t );
                    free(t);
                    break;
                }
                case 4: n->str = va_arg( ap, char* ); break;
                case 5: n->str = strdup( va_arg( ap, char* ) ); break;
                default: break;
            }
//             fprintf( stderr, "%s.%i: [%s] %p\n", __FILE__, __LINE__, n->str, n->str );
            break;
		case HashNode:
			break;
		case AuthNode:
			n->expr1 = va_arg( ap, Node*);
			n->expr2 = va_arg( ap, Node*);
			break;
		default:
#ifdef SYSLOG
			 syslog( LOG_WARNING, "%s.%i: : unhandled numeration value in switch\nNode type is: %d\n", __FILE__, __LINE__, n->type );
#endif
			break;

	}
	va_end(ap);
	lexer->start = n;
	return n;
}

void ReduceNode( Lexer *lexer, Node *parent, Node *current )
{
	if ( lexer->exit == 1 ) return;
	if ( lexer->trigg_break == 1 ) return;
	if ( lexer->trigg_continue == 1 ) return;
	if ( lexer->trigg_return == 1 ) return;
	if ( current == NULL ) return;
	
//     log_debug( g_log, "ReduceNode( %s )", nameByType( current->type ) );

	switch ( current->type ) 
	{
		case StatementNode:
			if ( current->stat ) 
				ReduceNode( lexer, current, current->stat );
			break;
		case StatementListNode:
		case SourceElementListNode:
			while( current->next )
				current = current->next;
			while( current->prev ) 
			{
				if ( current->element ) 
					ReduceNode( lexer, current, current->element );
				current = current->prev;
			}
			if ( current->element ) 
				ReduceNode( lexer, current, current->element );
			break;
		case SourceElementNode:
			ReduceNode( lexer, current, current->stat );
			break;
		case ProgramNode:
			ReduceNode( lexer, current, current->prog );
			break;
		case ArgumentsNode:
			if ( current->expr1 ) 
				ReduceNode( lexer, current, current->expr1 );
			break;
		case ArgumentListNode:
		{
            const size_t    tmp_size = strlen(lexer->func) + 90;
            char            tmp[tmp_size];

			sprintf ( tmp, "%s_FUNC_IN_%d", lexer->func, lexer->func_suff );
//             strcpy(tmp, lexer->func);
//             strcat(tmp, "_FUNC_IN");

// 			log_debug( g_log, "ArgumentsNode: %s", tmp );
			symrec *ptr = getsym(lexer, tmp, 0 );
			if ( ptr == NULL ) {
				break;
			};
			
			while( current->next ) current = current->next;
			while( current->prev ) 
			{
				ReduceNode( lexer, current, current->expr1 );
// 				log_debug( g_log, "ARG: %s", current->expr1->str );
				if ( current->expr1->str != NULL )
					*(char **)push_array(ptr->value.array) = strdup( current->expr1->str );
				current = current->prev;
			}

			if ( current->expr1 ) 
			{
				ReduceNode( lexer, current, current->expr1 );
// 				log_debug( g_log, "ARG: %s", current->expr1->str );
				if ( current->expr1->str != NULL ) 
					*(char **)push_array(ptr->value.array) = strdup( current->expr1->str );
			}
			
			break;
		}
		case BlockNode:
			if ( current->list )
				ReduceNode( lexer, current, current->list );
			break;
		case EmptyStatementNode:
			break;
		case ExprStatementNode:
			ReduceNode( lexer, current, current->expr1 );
			break;
		case NumberNode:
		{
			setnumstr( &(current->str), current->num );
			break;
		}
		case StringNode:
		{
			current->num = toNumber( current->str );
			break;
		}
		case CommaNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );
			break;
		}
		case ArrayNode:
		{
			symrec *ptr = getsym(lexer, "_ARRAY_", 0 );
			if ( ptr == NULL )
				ptr = putsym(lexer, "_ARRAY_", ValueArray, 0 );
			else {
				array_delete(ptr->value.array);
			}

			ReduceNode( lexer, current, current->expr1 );

			ptr->value.array = make_array( current->expr1->num, sizeof(char *) );
			break;
		}
		case HashNode:
		{
			symrec *ptr = getsym(lexer, "_HASH_", 0 );
			if ( ptr == NULL )
				ptr = putsym(lexer, "_HASH_", ValueHash, 0 );
			else {
				table_delete( ptr->value.hash );
			}

			ptr->value.hash = make_table( DEFAULT_TABLE_NELTS );
			break;
		}
        case ElementPathNode:
            ReduceNode( lexer, current, current->next );
            if ( current->ident ) {
                current->ident = realloc( current->ident, strlen(current->ident)+strlen(current->str)+2 );

                if(0 == current->ident){
                    log_failed_errno_call_2(g_log, LOG_ERR, "realloc(..., %lu)",
                        strlen(current->ident) + strlen(current->str) + 2);
                };

                strcat(current->ident, "/");
                strcat(current->ident, current->str);
                parent->ident = current->ident;
            } else {
                parent->ident = malloc( strlen(current->str) + 2 );

                if(0 == parent->ident){
                    log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                        strlen(current->str) + 2);
                };

                sprintf( parent->ident, "/%s", current->str );
            }
            current->ident = 0;
            break;
		case FunctionCallNode:
		{
            if ( current->expr1 )
                ReduceNode( lexer, current, current->expr1 );
            
			if ( !current->ident ) return;
			
			if ( lexer->func ) free(lexer->func);
			lexer->func = malloc( strlen(current->ident) + 1 );

            if(0 == lexer->func){
                log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                    strlen(current->ident) + 1);
            };

			strcpy( lexer->func, current->ident );
			
			symrec *tail_ptr = 0;
			
			tail_ptr = lexer->sym_tail;
			lexer->func_suff++;
			
			if ( current->args ) 
			{
                const size_t    tmp_size = strlen(lexer->func) + 90;
                char            tmp[tmp_size];

				sprintf ( tmp, "%s_FUNC_IN_%d", lexer->func, lexer->func_suff );
// 				strcpy( tmp, lexer->func );
// 				strcat( tmp, "_FUNC_IN" );
				
// 				log_debug( g_log, "FunctionCallNode: %s", tmp );

				symrec *ptr = getsym(lexer, tmp, 0 );
				if( ptr == NULL ) 
				{
					ptr = putsym(lexer, tmp, ValueArray, 0 );
					ptr->value.array = make_array( 1, sizeof(char *) );
				}

				ReduceNode( lexer, current, current->args );
			}
			
			free(lexer->func);
			lexer->func = 0;

			void *data = NULL;
			double num;
			current->oper = CallFunction( lexer, current->ident, &data, &num );
			
			clear_stack( tail_ptr->next );
			tail_ptr->next = NULL;
			lexer->sym_tail = tail_ptr;
            
//             fprintf( stderr, "%s RETURN TYPE: %i\n", current->ident, rtype );
			lexer->func_suff--;

			switch ( current->oper ) 
			{
				case ValueNumber: 
				{
					current->num = num;
					current->collate = NumberNode;	
					setnumstr( &(current->str), current->num );
					break;
				}
				case ValueString: 
				{
					if ( current->str ) free(current->str);
					current->str = (char*)data;
					current->collate = StringNode;
					current->num = toNumber(current->str);
					break;
				}
				case ValueArray: 
				{
					current->num = 0;
					current->collate = ArrayNode;
					if ( current->str ) free(current->str);
					current->str = strdup("_ARRAY_");

					symrec *ptr = getsym(lexer, "_ARRAY_", 1 );
					if ( ptr == NULL )
						ptr = putsym(lexer, "_ARRAY_", ValueArray, 1 );
					else {
						array_delete( ptr->value.array );
					}
					ptr->value.array = (array_header*)data;
					break;
				}
				case ValueHash: 
				{
					current->num = 0;
					current->collate = HashNode;
					if ( current->str ) free(current->str);
					current->str = strdup("_HASH_");

					symrec *ptr = getsym(lexer, "_HASH_", 1 );
					if ( ptr == NULL )
						ptr = putsym(lexer, "_HASH_", ValueHash, 1 );
					else {
						table_delete( ptr->value.hash );
					}
					ptr->value.hash = (table*)data;
					break;
				}
				case ValueGdImage: 
				{
					symrec *ptr = getsym(lexer, "_GD_IMAGE_", 1 );
					if ( ptr == NULL )
						ptr = putsym(lexer, "_GD_IMAGE_", ValueGdImage, 1 );
					ptr->value.gd = data;
// 					if ( ptr->value.gd == 0 )
// 						log_debug( g_log, "Null value assigned to _GD_IMAGE_" );
					break;
				}
			}
            
//             fprintf( stderr, "%i\n", current->collate );

			break;
		}
		case PostfixNode:
		{
			ReduceNode( lexer, current, current->expr1 );

			double num = current->expr1->num;

			switch( current->oper ) 
			{
				case PLUSPLUS: num++; break;
				case MINUSMINUS: num--; break;
				default: break;
			}

			current->num = num;
			setnumstr( &(current->str), current->num );

			if ( current->expr1->type == IdentNode ) 
			{
				symrec *ptr = getsym(lexer, current->expr1->ident, 0 );
				
				if ( ptr && ptr->type == ValueNumber ) 
					ptr->value.num = num;
				else if ( ptr && ptr->type == ValueString ) {
					if ( ptr->value.str ) free( ptr->value.str);
					ptr->value.str = strdup( current->str );
				}
			}

			break;
		}
		case PrefixNode:
		{
			ReduceNode( lexer, current, current->expr1 );

			double num = current->expr1->num ;

			switch( current->oper ) 
			{
				case PLUSPLUS:
					++num;
					break;
				case MINUSMINUS:
					--num;
					break;
				case '+':
					num = +(num);
					break;
				case '-':
					num = -(num);
					break;
				case '!':
					num = num == 0 ? 1 : 0;
					break;
				default: break;
			}

			current->num = num;
			setnumstr( &(current->str), current->num );
			current->collate = NumberNode;

			if ( current->expr1->type == IdentNode && current->oper != '!' ) 
			{
				symrec *ptr = getsym(lexer, current->expr1->ident, 0 );
				if ( ptr && ptr->type == ValueNumber ) 
					ptr->value.num = num;
				else if ( ptr && ptr->type == ValueString ) 
				{
					if ( ptr->value.str ) free( ptr->value.str );
					ptr->value.str = strdup ( current->str );
				}
			}
			break;
		}
		case MultNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );

			double num = 0,
			op1 = current->expr1->num,
			op2 = current->expr2->num;
					
			if ( current->expr1->type == IdentNode ) 
			{
				symrec *ptr = getsym( lexer, current->expr1->ident, 0 );
				if ( ptr ) 
				{
					if ( ptr->type == ValueNumber) 
						op1 = ptr->value.num;
					else if ( ptr->type == ValueString ) 
						op1 = toNumber(ptr->value.str);
				}
			}
			if ( current->expr2->type == IdentNode ) 
			{
				symrec *ptr = getsym( lexer, current->expr2->ident, 0 );
				if ( ptr ) 
				{
					if ( ptr->type == ValueNumber) 
						op2 = ptr->value.num;
					else if ( ptr->type == ValueString ) 
						op2 = toNumber(ptr->value.str);
				}
			}

			if ( current->oper == (int)'*' ) 
			{
				num = op1 * op2;
			} 
			else if ( current->oper == (int)'/' ) 
			{
				if ( op2 == 0 ) 
				{
#ifdef SYSLOG
					syslog(LOG_WARNING, "%s.%i: Division by zero\n", __FILE__, __LINE__ );
#endif
					return;
				} else
					num = op1 / op2;
			} else if ( current->oper == (int)'%' ) 
			{
				if ( op2 == 0 ) {
#ifdef SYSLOG
					syslog(LOG_WARNING, "%s.%i: Division by zero\n", __FILE__, __LINE__ );
#endif
					return;
				} else
					num = (unsigned int)op1 % (unsigned int)op2;
			}

			current->num = num;
			current->collate = NumberNode;
			setnumstr( &(current->str), current->num );
			break;
		}
		case AddNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );

			double op1 = current->expr1->num,
			op2 = current->expr2->num;
					
			if ( current->expr1->type == IdentNode ) 
			{
				symrec *ptr = getsym( lexer, current->expr1->ident, 0 );
				if ( ptr ) 
				{
					if ( ptr->type == ValueNumber) 
						op1 = ptr->value.num;
					else if ( ptr->type == ValueString ) 
						op1 = toNumber(ptr->value.str);
				}
			}

			if ( current->expr2->type == IdentNode ) 
			{
				symrec *ptr = getsym( lexer, current->expr2->ident, 0 );
				if ( ptr ) 
				{
					if ( ptr->type == ValueNumber) 
						op2 = ptr->value.num;
					else if ( ptr->type == ValueString ) 
						op2 = toNumber(ptr->value.str);
				}
			}

			if ( current->oper == (int)'+' ) 
			{
				current->num = op1 + op2;
				current->collate = NumberNode;
				setnumstr( &(current->str), current->num );
			} 
			else if ( current->oper == (int)'-' ) 
			{
				current->num = op1 - op2;
				current->collate = NumberNode;
				setnumstr( &(current->str), current->num );
			} 
			else if ( current->oper == (int)'.' ) 
			{
				if ( current->str ) free( current->str );
				
				int slen = 0;
				
				current->collate = StringNode;
				current->str = malloc( slen + 1 );

                if(0 == current->str){
                    log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                        slen + 1);
                };

				current->str[slen] = '\0';
				
				if ( current->expr1->str ) {
					slen += strlen(current->expr1->str);
					current->str = realloc( current->str, slen + 1 );

                    if(0 == current->str){
                        log_failed_errno_call_2(g_log, LOG_ERR, "realloc(..., %lu)",
                            slen + 1);
                    };

					strcat( current->str, current->expr1->str );
				}
				
				if ( current->expr2->str ) 
				{
					slen += strlen(current->expr2->str);
					current->str = realloc( current->str, slen + 1 );

                    if(0 == current->str){
                        log_failed_errno_call_2(g_log, LOG_ERR, "realloc(..., %lu)",
                            slen + 1);
                    };

					strcat( current->str, current->expr2->str );
				}
			}
			break;
		}
		case ShiftNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );

			switch( current->oper ) 
			{
				case LSHIFT: current->num = (unsigned int)current->expr1->num << (unsigned int)current->expr2->num; break;
				case RSHIFT: current->num = (unsigned int)current->expr1->num >> (unsigned int)current->expr2->num; break;
				default: break;
			}

			current->collate = NumberNode;
			setnumstr( &(current->str), current->num );
			break;
		}
		case RelationalNode:
		{
			
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );

			double n1 = 0, n2 = 0;
			char *c1 = NULL, *c2 = NULL;
			
			n1 = current->expr1->num;
			n2 = current->expr2->num;

			c1 = current->expr1->str;
			c2 = current->expr2->str;
			
			if ( current->expr1->type == FunctionCallNode ) 
			{
				if ( current->expr1->collate == ValueNumber )
					n1 = current->expr1->num;
				else if ( current->expr1->collate == ValueString )
					c1 = current->expr1->str;
			}
			
			if ( current->expr2->type == FunctionCallNode  ) 
			{
				if ( current->expr1->collate == ValueNumber )
					n2 = current->expr1->num;
				else if ( current->expr1->collate == ValueString )
					c2 = current->expr1->str;
			}

			current->num = 0;

			switch( current->oper ) 
			{
				case '>': current->num = n1 > n2; break;
				case '<': current->num = n1 < n2; break;
				case LE: current->num = n1 <= n2; break;
				case GE: current->num = n1 >= n2; break;
				case STRLT: current->num = ( strcmp(c1, c2) < 0 ); break;
				case STRGT: current->num = ( strcmp(c1, c2) > 0 ); break;
				case STRLE: current->num = ( strcmp(c1, c2) <= 0 ); break;
				case STRGE: current->num = ( strcmp(c1, c2) >= 0 ); break;
				default: break;
			}

			current->collate = NumberNode;
			setnumstr( &(current->str), current->num );
			break;
		}
		case EqualNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );

			if ( current->expr1->collate == StringNode 
						  || current->expr2->collate == StringNode ) 
			{
				if ( !current->expr1->str ) current->expr1->str = strdup("");
				if ( !current->expr2->str ) current->expr2->str = strdup("");
			}


			switch ( current->oper ) 
			{
				case EQ: 
					current->num = current->expr1->num == current->expr2->num;
					break;
				case NEQ: 
					current->num = current->expr1->num != current->expr2->num;
					break;
				case STREQ: 
					current->num = ( strcmp(current->expr1->str, current->expr2->str) == 0 );
					break;
				case STRNEQ: 
					current->num = ( strcmp(current->expr1->str, current->expr2->str) != 0 ); 
					break;
				case REN :
				case '~': 
				{
					if ( !current->expr1->str ) current->expr1->str = strdup("(NULL)");

					int options = 0;
					
					pcre *re = re_compile ( lexer, current->expr2->str, &options );
					if ( re ) 
					{
						int str_len = strlen(current->expr1->str);
						int ovector[OVECCOUNT];
						int rc = pcre_exec(
								re,	/* the compiled pattern */
								NULL,			/* no extra data - we didn't study the pattern */
								current->expr1->str,			/* the subject string */
								str_len,		/* the length of the subject */
								0,				/* start at offset 0 in the subject */
								options,	    /* default options */
								ovector,		/* output vector for substring information */
								OVECCOUNT);		/* number of elements in the output vector */
	
						if (rc < 0) current->num = 0;
						else current->num = 1;
						pcre_free(re);
					} else {
						current->num = 0;
					}

					if (current->oper == REN)
						current->num = current->num == 0 ? 1 : 0;
					
				}
				default: break;
			}

			current->collate = NumberNode;
			setnumstr( &(current->str), current->num );
			
			break;
		}
		case BitOperNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );

			switch( current->oper ) 
			{
				case '&': current->num = (unsigned int)current->expr1->num & (unsigned int)current->expr2->num; break;
				case '^': current->num = (unsigned int)current->expr1->num ^ (unsigned int)current->expr2->num; break;
				case '|': current->num = (unsigned int)current->expr1->num | (unsigned int)current->expr2->num; break;
				default: break;
			}

			current->collate = NumberNode;
			setnumstr( &(current->str), current->num );
			
			break;
		}
		
		case LogicalNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );

			switch( current->oper ) 
			{
				case AND: 
					current->num = (unsigned int)current->expr1->num && (unsigned int)current->expr2->num; 
					break;
				case OR: 
					current->num = (unsigned int)current->expr1->num || (unsigned int)current->expr2->num; 
					break;
				default: break;
			}
			
			current->collate = NumberNode;
			setnumstr( &(current->str), current->num );
			
			break;
		}
		case ConditionalNode:
		{
			ReduceNode( lexer, current, current->logical );
			
			if ( (int)current->logical->num != 0 || (int)current->logical->num2 != 0 ) 
			{
// 				fprintf( stderr, "REDUCE FIRST EXPR\n" );
				ReduceNode( lexer, current, current->expr1 );
				if ( current->str ) free ( current->str );
				current->str = current->expr1->str ? strdup( current->expr1->str ) : strdup("");
				current->num = current->expr1->num;
			}
			else {
// 				fprintf( stderr, "REDUCE SECOND EXPR\n" );
				ReduceNode( lexer, current, current->expr2 );
				if ( current->str ) free ( current->str );
				if ( current->expr2->str ) {
					current->str = strdup( current->expr2->str );
				} else {
					current->str = strdup( "" );
				}
				current->num = current->expr2->num;
			}

 			if ( current->str )	
				current->collate = StringNode;
			else 
			{
				current->collate = NumberNode;
				setnumstr( &(current->str), current->num );
			}

			break;
		}
		case AssignNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );

			if ( current->expr1->ident && current->expr2 )
			{
				SetVariable( lexer, current->expr1, current->stat->oper, current->expr2 );
			}
			else if ( current->expr1->type == ArrayItemNode )
			{
				ReduceNode( lexer, current, current->expr1->args );

				char *name = NULL;

				if ( current->expr1->args->type == NumberNode ) 
				{
                    name = malloc(128);

                    if(0 == name){
                        log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)", 128);
                    };

					snprintf( name, 128, "%.f", current->expr1->args->num );
				} else {
					name = ( current->expr1->args->type == StringNode ||
							current->expr1->args->collate == StringNode ) ?
						strdup( current->expr1->args->str ) :
							current->expr1->args->type == ArrayItemNode ? strdup(current->expr1->args->str) :
							strdup( current->expr1->args->ident );
				}

				symrec *rec = getsym( lexer, current->expr1->expr1->ident, 0 );
				if ( !rec ) { free(name); return; }

				if ( rec->type == ValueArray )
				{
					int idx = 0;
					if ( current->expr1->args->type == NumberNode )
						idx = current->expr1->args->num;
					else
					{
						symrec *pval = getsym( lexer, name, 0 );
						if ( pval ) {
							if ( pval->type == ValueNumber)
								idx = pval->value.num;
							else if ( pval->type == ValueString )
								idx = strtol(pval->value.str, (char**)NULL, 10);
							else
								idx = strtol(name, (char**)NULL, 10 );
						}
					}

					array_header *arr = rec->value.array;

					while( arr->nelts <= idx ) {
						*(char **)push_array(rec->value.array) = strdup( "" );
					}

					switch( current->stat->oper )
					{
						case DOTASSIGN:
						{
							char *v = ((char **)arr->elts)[idx];
							v = realloc( v, strlen(v) + strlen(current->expr2->str) );

                            if(0 == v){
                                log_failed_errno_call(g_log, LOG_ERR, realloc());
                            };

							strcat( v, current->expr2->str );
							break;
						}
						case PLUSASSIGN:
						{
							double num = toNumber( ((char **)arr->elts)[idx] );
							num += current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case MINUSASSIGN:
						{
							double num = toNumber( ((char **)arr->elts)[idx] );
							num -= current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case MULTASSIGN: {
							double num = toNumber( ((char **)arr->elts)[idx] );
							num *= current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case DIVASSIGN: {
							double num = toNumber( ((char **)arr->elts)[idx] );
							if ( current->expr2->num == 0 ) {
#ifdef SYSLOG
								syslog(LOG_WARNING, "%s.%i: Division by zero\n", __FILE__, __LINE__ );
#endif
							} else {
								num /= current->expr2->num;
							}
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case LSHIFTASSIGN: {
							double num = toNumber( ((char **)arr->elts)[idx] );
							num = (unsigned int)num << (unsigned int)current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case RSHIFTASSIGN: {
							double num = toNumber( ((char **)arr->elts)[idx] );
							num = (unsigned int)num >> (unsigned int)current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case ANDASSIGN: {
							double num = toNumber( ((char **)arr->elts)[idx] );
							num = (unsigned int)num & (unsigned int)current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case XORASSIGN: {
							double num = toNumber( ((char **)arr->elts)[idx] );
							num = (unsigned int)num ^ (unsigned int)current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case ORASSIGN: {
							double num = toNumber( ((char **)arr->elts)[idx] );
							num = (unsigned int)num | (unsigned int)current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case MODASSIGN: {
							double num = toNumber( ((char **)arr->elts)[idx] );
							num = (unsigned int)num % (unsigned int)current->expr2->num;
							setnumstr( &( ((char **)arr->elts)[idx] ), num );
							break;
						}
						case '=':
							if ( ((char **)arr->elts)[idx] ) free(((char **)arr->elts)[idx]);
							((char **)arr->elts)[idx] = strdup(current->expr2->str);
							break;
					}
				}
				else if ( rec->type == ValueHash )
				{
					if ( !current->expr2->str ) current->expr2->str = strdup("");

					if ( current->expr1->args->type == IdentNode )
					{
						symrec *pval = getsym( lexer, name, 0 );
						if ( pval ) {
							if ( pval->type == ValueString )
								table_set( rec->value.hash, pval->value.str, current->expr2->str );
							else if ( pval->type == ValueNumber ) 
							{
								free(name);
								name = malloc( 80 );

                                if(0 == name){
                                    log_failed_errno_call_2(g_log, LOG_ERR,
                                        "malloc(%lu)", 80);
                                };

								sprintf( name, "%.f", pval->value.num );
							}
						}
					}
					
					switch( current->stat->oper )
					{
						case DOTASSIGN:
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";

                            const size_t    tmp_size = strlen(v) +
                                                       strlen(current->expr2->str) + 4;
                            char            tmp[tmp_size];

							strcpy( tmp, v );
							strcat( tmp, current->expr2->str );
							table_set( rec->value.hash, name, tmp );
							break;
						}
						case PLUSASSIGN:
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num += current->expr2->num;

                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case MINUSASSIGN:
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num -= current->expr2->num;

                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case MULTASSIGN:
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num *= current->expr2->num;
                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case DIVASSIGN:
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							if ( current->expr2->num == 0 ) {
#ifdef SYSLOG
								syslog(LOG_WARNING, "%s.%i: Division by zero\n", __FILE__, __LINE__ );
#endif
							} else {
								num /= current->expr2->num;
							}
                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case LSHIFTASSIGN: 
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num = (unsigned int)num << (unsigned int)current->expr2->num;

                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case RSHIFTASSIGN: 
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num = (unsigned int)num >> (unsigned int)current->expr2->num;

                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case ANDASSIGN: {
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num = (unsigned int)num & (unsigned int)current->expr2->num;

                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case XORASSIGN: {
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num = (unsigned int)num ^ (unsigned int)current->expr2->num;

                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case ORASSIGN: 
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num = (unsigned int)num | (unsigned int)current->expr2->num;

                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case MODASSIGN: 
						{
							const char *v = table_get( rec->value.hash, name );
							if ( !v ) v = "";
							double num = toNumber( v );
							num = (unsigned int)num % (unsigned int)current->expr2->num;

                            char    tmp[128];

                            setnumstr_2(num, tmp, sizeof(tmp));
							table_set( rec->value.hash, name, tmp );

							break;
						}
						case '=':
							table_set( rec->value.hash, name, current->expr2->str );
							break;
					}
				}
				free(name);
			}
			
			break;
		}
		case AssigOpNode:
			break;
		case IfStatementNode:
		{
			ReduceNode( lexer, current, current->logical );
			
			if ( (int)current->logical->num != 0 || (int)current->logical->num2 != 0 )
				ReduceNode( lexer, current, current->expr1 );
			else
				ReduceNode( lexer, current, current->expr2 );
			break;
		}
		case ElseStamentNode:
			ReduceNode( lexer, current, current->expr1 );
			break;
		case DoWhileNode:
		{
			do {
				current->logical->num = 0;
				ReduceNode( lexer, current, current->list );
				if ( lexer->trigg_continue ) {
					lexer->trigg_continue = 0;
					ReduceNode( lexer, current, current->logical );
					continue;
				}
				else
					ReduceNode( lexer, current, current->logical ); 
				
				if ( lexer->trigg_break ) {
					lexer->trigg_break = 0;
					break;
				}
				
			}
			while( current->logical->num );
			break;
		}
		case WhileNode:
		{
			ReduceNode( lexer, current, current->logical );
			while( (int)current->logical->num ) {
				current->logical->num = 0;
				ReduceNode( lexer, current, current->list );
				if ( lexer->trigg_continue ) {
					lexer->trigg_continue = 0;
					ReduceNode( lexer, current, current->logical );
					continue;
				} else {
					ReduceNode( lexer, current, current->logical );
				}
				if ( lexer->trigg_break ) {
					lexer->trigg_break = 0;
					break;
				}
				
			}
			break;
		}
		case ForNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );
			// we assume that node expr2 is a RelationalNode
			while( current->expr2->num ) 
			{
				current->expr2->num = 0;
				ReduceNode( lexer, current, current->list );
				if ( lexer->trigg_continue ) {
					lexer->trigg_continue = 0;
					ReduceNode( lexer, current, current->expr3 );
					ReduceNode( lexer, current, current->expr2 );
					continue;
				} else {
					ReduceNode( lexer, current, current->expr3 );
					ReduceNode( lexer, current, current->expr2 );
				}
				
				if ( lexer->trigg_break ) {
					lexer->trigg_break = 0;
					break;
				}
			}
			break;
		}
		case ContinueNode:
			lexer->trigg_continue = 1;
			break;
		case BreakNode:
// 			fprintf( stderr, "BREAK NODE\n" );
			lexer->trigg_break = 1;
			break;
		case ReturnNode:
			if( current->expr1 ) ReduceNode( lexer, current, current->expr1 );
			lexer->trigg_return = 1;
			break;
		case ArrayItemNode:
		{
			ReduceNode( lexer, current, current->args );

			char *name = NULL;
			double num = 0;
			int name_alloc = 0;
			
			if ( current->args->type == StringNode || current->args->collate == StringNode ) {
				name = current->args->str;
			} else if ( current->args->type == IdentNode ) {
				name = current->args->str;
				num = current->args->num;
			} else if ( current->args->type == NumberNode || current->args->collate == NumberNode ) {
				num = current->args->num;
			} else if ( current->args->type == ArrayItemNode ) {
				name = current->args->str;
			}


			if ( !name ) {
				setnumstr( &name, num );
				name_alloc = 1;
			}

			symrec *rec = getsym( lexer, current->expr1->ident, 0 );
			if ( !rec ) return;

			char *tmp = NULL;
			if ( rec->type == ValueArray ) 
			{
				int idx = atoi(name);
				array_header *arr = rec->value.array;
				if ( arr && idx >= 0 && arr->nelts > idx ) {
					tmp = ((char **)arr->elts)[idx];
				} else {
					tmp = NULL;
				}
			}
			else if ( rec->type == ValueHash ) 
			{
				tmp = (char*)table_get( rec->value.hash, name );
			}
			
			if ( name_alloc ) free(name);
			
			if ( current->str ) free ( current->str );
			if ( tmp ) 
			{
				current->str = malloc ( strlen(tmp) + 1 ) ;

                if(0 == current->str){
                    log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                        strlen(tmp) + 1);
                };

				sprintf( current->str, "%s", tmp );
				current->collate = StringNode;
				current->num = toNumber(tmp);
				current->num2 = 1;
			} 
			else 
			{
				current->str = strdup("");
				current->collate = StringNode;
				current->num = 0;
				current->num2 = 0;
			}
			
			break;
		}
		case IdentNode:
		{
			symrec *rec = getsym( lexer, current->ident, 0 );
			if ( !rec ) {
				break;
			}
			
// 			log_debug( g_log, "IdentNode: %s; %d", current->ident, rec->type );
			
			if ( rec->type == ValueNumber ) 
			{
				current->collate = NumberNode;
				current->num = rec->value.num;
				setnumstr( &(current->str), current->num );
			} 
			else if ( rec->type == ValueString && rec->value.str ) 
			{
				current->collate = StringNode;
				if ( current->str ) free(current->str);
				current->str = malloc( strlen(rec->value.str) + 1 );

                if(0 == current->str){
                    log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                        strlen(rec->value.str) + 1);
                };

				sprintf( current->str, "%s", rec->value.str );
				current->num = toNumber( current->str );
			} else /*if ( rec->type == ValueArray || rec->type == ValueHash  )*/ {
				if ( current->str ) free(current->str);
				current->str = strdup( current->ident );
			}
			
			break;
		}
		case ForInNode:
		{
			// fprintf( stderr, "ForInNode\n" );
// 			n->ident = strdup( va_arg(ap, char*) );    // array symrec
// 			n->expr2 = va_arg( ap, Node*);    // call statement
// 			n->expr3 = va_arg( ap, Node*);    // statement list
			break;
		}
		case ExecuteNode:
		case SelectStatementNode:
		{
			lexer->is_sql = 1;
			
			char *query = NULL;
			
			if ( current->type == SelectStatementNode ) 
			{
				ReduceNode( lexer, current, current->expr1 ); // fields
				query = malloc( 8 + strlen( current->pattern ) ) ;

                if(0 == query){
                    log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                        8 + strlen(current->pattern));
                };

				strcpy( query, "SELECT ");
				strcat( query, current->pattern);
			}
			else
			{
				ReduceNode( lexer, current, current->expr1 ); // fields
				query = current->expr1->str ? strdup( current->expr1->str ) : strdup(""); 
			}
			
#ifdef SYSLOG
			char *tab = tabs(lexer->depth);
			syslog( LOG_DEBUG, "%s.%i: %s QUERY: %s\n", __FILE__, __LINE__, tab, query );
			free(tab);
#endif
		
			symrec *stat = getsym(lexer, "QUERY", 0 );
			if( stat == NULL ) {
				stat = putsym(lexer, "QUERY", ValueHash, 0 );
				stat->value.hash = make_table(DEFAULT_TABLE_NELTS);
			} else {
				table_clear( stat->value.hash );
			}

			symrec *data = getsym(lexer, current->str, 0 );
			if( data == NULL ) 
			{
				data = putsym( lexer, current->str, ValueHash, 0 );
				data->value.hash = make_table(DEFAULT_TABLE_NELTS);
			} 
			else if ( data->type != ValueHash ) 
			{
				data = takesym( lexer, current->str);
				free_sym(data);
				data = putsym( lexer, current->str, ValueHash, 0 );
				data->value.hash = make_table(DEFAULT_TABLE_NELTS);
			}
			
			char *tmp = NULL;
			
			double s0, s1;
			get_microtime( &s0, (char*)0);
			alarm( g_config->worker_args.sql_timeout );
			PGresult *result = PQexec( database_user_or_anon(g_database), query );
			alarm(0);
			get_microtime( &s1, (char*)0);
#ifdef SYSLOG
			syslog( LOG_INFO, "QUERY TIME: %.8f\n", s1-s0 );
#endif

            {
                char    alt_tmp[32];

                snprintf(alt_tmp, sizeof(alt_tmp), "%i", PQresultStatus(result));
                table_set(stat->value.hash, "status", alt_tmp);
            };

			table_set( stat->value.hash, "query", query );
			free(query);

			if ( PQresultStatus( result ) == PGRES_FATAL_ERROR )
				debugSqlError( lexer, stat->value.hash, PQerrorMessage(database_user_or_anon(g_database)) );
			else if ( PQntuples( result ) > 0 ) 
			{
				int rows = PQntuples( result );
				int fields = PQnfields( result );
				int j, g = 0;

                {
                    char    alt_tmp[32];

                    snprintf(alt_tmp, sizeof(alt_tmp), "%i", rows);
				    table_set(stat->value.hash, "rows", alt_tmp);
                };

				for ( j = 0; j < fields; j++ ) 
				{
					const char *fname = PQfname( result, j );
					const char *val = PQgetvalue( result, 0, j);
					if ( isLegalVarName(fname) ) 
					{
						table_set( data->value.hash, fname, val );
					} 
					else 
					{
						g++;

                        char    alt_tmp[48];

                        snprintf(alt_tmp, sizeof(alt_tmp), "field%d", g);
						table_set(data->value.hash, alt_tmp, val);
					}
				}
			}
			PQclear( result );
			lexer->is_sql = 0;
			break;
		}
		case ForSqlNode:
		{
			lexer->is_sql = 1;
			char *query = NULL;

			if ( current->oper == 1 ) 
            {
				ReduceNode( lexer, current, current->expr2 );
				query = current->expr2->str ? strdup( current->expr2->str ) : strdup("");
			} 
            else 
            {
                ReduceNode( lexer, current, current->expr2 ); // fields
				query = strdup( current->expr2->str );
			}

#ifdef SYSLOG
			char *tab = tabs(lexer->depth);
			syslog( LOG_DEBUG, "%s.%i: %s QUERY: %s\n", __FILE__, __LINE__, tab, query );
			free(tab);
#endif
			symrec *stat = getsym(lexer, "QUERY", 0 );
			if( stat == NULL ) {
				stat = putsym(lexer, "QUERY", ValueHash, 0 );
				stat->value.hash = make_table(DEFAULT_TABLE_NELTS);
			} else {
				table_clear( stat->value.hash );
			}

			symrec *data = getsym(lexer, current->ident, 0 );
			if( data == NULL ) {
				data = putsym( lexer, current->ident, ValueHash, 0 );
				data->value.hash = make_table(DEFAULT_TABLE_NELTS);
			}
			else if ( data->type != ValueHash )
			{
                data = takesym( lexer, current->str);
                free_sym(data);
                data = putsym( lexer, current->str, ValueHash, 0 );
                data->value.hash = make_table(DEFAULT_TABLE_NELTS);
            }
			
			char *tmp = NULL;
			
			double s0, s1;
			get_microtime( &s0, (char*)0);
			alarm( g_config->worker_args.sql_timeout );
			PGresult *result = PQexec( database_user_or_anon(g_database), query );
			alarm(0);
			get_microtime( &s1, (char*)0);
#ifdef SYSLOG
			syslog( LOG_INFO, "QUERY TIME: %.8f\n", s1-s0 );
#endif

            {
                char    alt_tmp[32];

                snprintf(alt_tmp, sizeof(alt_tmp), "%i", PQresultStatus(result));
			    table_set(stat->value.hash, "status", alt_tmp);
            };

			table_set( stat->value.hash, "query", query );
			free(query);

// 			syslog( LOG_INFO, "SQL STATUS: %i", PQresultStatus( result ) );
			if ( PQresultStatus( result ) == PGRES_FATAL_ERROR ) 
				debugSqlError( lexer, stat->value.hash, PQerrorMessage(database_user_or_anon(g_database)) );
			else 
			{
				long rows = PQntuples( result );
				int fields = PQnfields( result );
				long i,j, g = 0;

                {
                    char    alt_tmp[32];

				    snprintf(alt_tmp, sizeof(alt_tmp), "%ld", rows);
				    table_set(stat->value.hash, "rows", alt_tmp);
                };

				for( i = 0; i < rows; i++ ) 
				{
                    char    alt_tmp[48];
					
                    snprintf(alt_tmp, sizeof(alt_tmp), "%ld", i+1);
					table_set(data->value.hash, "row", alt_tmp);

					for ( j = 0; j < fields; j++ ) 
					{
						char *fname = PQfname( result, j );
						if ( isLegalVarName(fname) ) 
						{
							table_set( data->value.hash, fname, PQgetvalue( result, i, j) );
						} else {
							g++;
							
                            snprintf(alt_tmp, sizeof(alt_tmp), "field%ld", g);
						    table_set(data->value.hash, alt_tmp, PQgetvalue( result, i, j));
						}
					}
					ReduceNode( lexer, current, current->expr3 );
					if ( lexer->trigg_break ) {
						lexer->trigg_break = 0;
						break;
					}
					else if ( lexer->trigg_continue ) {
						lexer->trigg_continue = 0;
						continue;
					}
				}
			}

			PQclear( result );
			lexer->is_sql = 0;
			break;
		}
		case SelectNode:
		{
            ReduceNode( lexer, current, current->expr1 ); // fields
			if( current->str ) free(current->str);
            current->str = malloc( strlen( current->pattern ) + 7 );

            if(0 == current->str){
                log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                    strlen(current->pattern) + 7);
            };

            strcpy( current->str, "SELECT");
            strcat( current->str, current->pattern);

            break;
		}
		case InsertNode:
		{
            ReduceNode( lexer, current, current->expr1 ); // fields
            current->str = malloc( strlen( current->pattern ) + 13 );

            if(0 == current->str){
                log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                    strlen(current->pattern) + 13);
            };

            strcpy( current->str, "INSERT INTO ");
            strcat( current->str, current->pattern);

#ifdef SYSLOG
			char *tab = tabs(lexer->depth);
			syslog( LOG_DEBUG, "%s.%i: %s QUERY: %s\n", __FILE__, __LINE__, tab, current->str );
			free(tab);
#endif
			DoExec( lexer, current->str );
            break;
		}
		case UpdateNode:
		{
            ReduceNode( lexer, current, current->expr1 ); // fields
            current->str = malloc( strlen( current->pattern ) + 8 );

            if(0 == current->str){
                log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                    strlen(current->pattern) + 8);
            };

            strcpy( current->str, "UPDATE ");
            strcat( current->str, current->pattern);

#ifdef SYSLOG
			char *tab = tabs(lexer->depth);
			syslog( LOG_DEBUG, "%s.%i: %s QUERY: %s\n", __FILE__, __LINE__, tab, current->str );
			free(tab);
#endif
			DoExec( lexer, current->str );
			
			break;
		}
		case DeleteNode:
		{
			ReduceNode( lexer, current, current->expr1 ); // fields
            current->str = malloc( strlen( current->pattern ) + 8 );

            if(0 == current->str){
                log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                    strlen(current->pattern) + 8);
            };

            strcpy( current->str, "DELETE ");
            strcat( current->str, current->pattern);

#ifdef SYSLOG
			char *tab = tabs(lexer->depth);
			syslog( LOG_DEBUG, "%s.%i: %s QUERY: %s\n", __FILE__, __LINE__, tab, current->str );
			free(tab);
#endif
			DoExec( lexer, current->str );
			
			break;
		}
        case SqlClauseNode:
		{
            ReduceNode( lexer, current, current->next );
            ReduceNode( lexer, current, current->expr1 );
            
			if ( parent->pattern ) free( parent->pattern );
            if ( current->pattern ) {
                if ( current->expr1->str ) {
                    current->pattern = realloc( current->pattern, strlen(current->pattern)+strlen(current->expr1->str)+2 );

                    if(0 == current->pattern){
                        log_failed_errno_call(g_log, LOG_ERR, realloc());
                    };

                    strcat(current->pattern, current->expr1->str);
                }
                parent->pattern = strdup(current->pattern);
            } else {
                parent->pattern = strdup(current->expr1->str);
            }
			break;
		}
        case SqlTokenNode:
        {
            if( current->oper == 1 ) 
            {
                ReduceNode( lexer, current, current->expr1 );
//                 debugNode(current->expr1);
                symrec *ptr = 0;
                switch ( current->expr1->collate ) 
                {
                    case NumberNode: 
// 						SETNUMSTR( "%.f", current->str, current->num );
// 						break;
                    case StringNode:
					{
                        const size_t    str_size_ = strlen(current->expr1->str);
                        const size_t    xtmp_size = str_size_ * 2 + 4;
                        char            xtmp[xtmp_size];

						int l = PQescapeString (xtmp, current->expr1->str, str_size_);
						if ( current->str ) free( current->str );
						current->str = malloc( l + 3 );

                        if(0 == current->str){
                            log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                                l + 3);
                        };

						snprintf( current->str, l + 3, "'%s'", xtmp );

                        break;
					}
                    case ArrayNode:
                        ptr = getsym(lexer, "_ARRAY_", 0 );
                        break;
                    case HashNode:
                        ptr = getsym(lexer, "_HASH_", 0 );
                        break;
                    case IdentNode:
                        ptr = getsym(lexer, current->expr1->str, 0 );
                        break;
                    default: break;
                        
                }
                if ( ptr ) 
                {
                    if ( ptr->type == ValueString && ptr->value.str ) 
                    {
                        const size_t    str_size_ = strlen(ptr->value.str);
                        const size_t    xtmp_size = str_size_ * 2 + 4;
                        char            xtmp[xtmp_size];

                        int l = PQescapeString ( xtmp, ptr->value.str, str_size_ );
                        current->str = malloc( l + 3 );

                        if(0 == current->str){
                            log_failed_errno_call_2(g_log, LOG_ERR, "malloc(%lu)",
                                l + 3);
                        };

                        snprintf( current->str, l + 3, "'%s'", xtmp );
                    } else if ( ptr->type == ValueNumber ) {
                        setnumstr( &(current->str), ptr->value.num );
                    } 
                    else if ( ptr->type == ValueArray ) 
                    {
                        int qlen = 1; 
                        current->str = malloc( qlen );
                        current->str[0] = '\0'; 
                        array_header *array = ptr->value.array;
                        int i;
                        for( i = 0; i < array->nelts; i++ ) 
                        {
                            const size_t    str_size_ = strlen(((char**)array->elts)[i]);
                            const size_t    xtmp_size = str_size_ * 2 + 4;
                            char            xtmp[xtmp_size];

                            qlen += PQescapeString(xtmp, ((char **)array->elts)[i], str_size_);
                            qlen += 2;

                            if ( i < (array->nelts - 1 ) ) qlen++;
                        
                            current->str = realloc( current->str, qlen );
                        
                            strcat( current->str, "'" );
                            strcat( current->str, xtmp );
                            strcat( current->str, "'" );
                        
                            if ( i < (array->nelts - 1 ) )
                                strcat( current->str, "," );
                        }
                    } 
                    else if ( ptr->type == ValueHash ) 
                    {
                        table *t = ptr->value.hash;
                        table_entry *elts = (table_entry *) t->a.elts;

                        int qlen = 1; 
						if ( current->str ) free(current->str);
                        current->str = malloc( qlen );
                        current->str[0] = '\0'; 
                        
                        /* char *xtmp = NULL; */
                        int j;
                        for (j = 0; j < t->a.nelts; ++j ) 
                        {
                            const size_t    str_size_ = strlen(elts[j].val);
                            const size_t    xtmp_size = str_size_ * 2 + 4;
                            char            xtmp[xtmp_size];

							qlen += strlen(elts[j].key);
                            qlen += PQescapeString(xtmp, elts[j].val, str_size_);
                            qlen += 3;

                            if ( j < (t->a.nelts - 1 ) ) qlen++;

                            current->str = realloc( current->str, qlen );
                            
                            strcat( current->str, elts[j].key );
                            strcat( current->str, "='" );
                            strcat( current->str, xtmp );
                            strcat( current->str, "'" );
                        
                            if ( j < (t->a.nelts - 1 ) )
                                strcat( current->str, "," );
                        }
					} 
					else {
						// handle working with call on non declared variables 
						current->str = malloc( 3 );
						strcpy( current->str, "''" );
					}
                }
//                 if ( current->str == NULL ) current->str = strdup("");
            }
//             fprintf( stderr, "%s.%i: %s; %p\n", __FILE__, __LINE__, current->str, current->str );
            break;
        }
		case AuthNode:
		{
			ReduceNode( lexer, current, current->expr1 );
			ReduceNode( lexer, current, current->expr2 );
			
			const char *username = current->expr1->str ? current->expr1->str : "anonymous";
			const char *user_password = current->expr2->str ? current->expr2->str : "anonymous";
			
			const size_t    user_password_len = strlen(user_password);
			char            pass[user_password_len * 2 + 1];

			quote_password(user_password, user_password_len, pass);
			
			PGconn* new_conn = database_make_connect(g_config->worker_args.db_name,
													username,
			 										pass,
													g_config->worker_args.db_host,
													g_config->worker_args.db_port,
													false);
			
			if ( PQstatus( new_conn ) != CONNECTION_OK ) {
				log_notice(g_log, "connection to database failed, "
						"error '%s'", PQerrorMessage(new_conn));
			} else {
				if ( g_database->user != 0 ) PQfinish( g_database->user ), g_database->user = 0;
				g_database->user = new_conn;
				table_set( lexer->req->env, "USER", username );
				log_debug(g_log, "user '%s' logged in", username );
			}
			break;
		}
		default:
#ifdef SYSLOG
			 syslog( LOG_WARNING, "%s.%i: unhandled numeration value in switch\nNode type is: %d\n", __FILE__, __LINE__, current->type );
#endif
//              fprintf( stderr, "%s.%i: unhandled numeration value in switch\nNode type is: %d\n", __FILE__, __LINE__, current->type );
			break;
	}
}

void SetVariable( Lexer *lexer, Node *var, int op, Node *current )
{
// 	log_debug( g_log, " --> SetVariable: %s is %sglobal", var->ident, (var->oper == 0 ? "not " : ""));

	symrec *tmp = NULL;
	symrec *rec = getsym( lexer, var->ident, 0 );

	if ( rec == NULL )
	{
		int type = 0;
// 		log_debug( g_log, " --> current->collate: %d", current->collate );
		switch ( current->collate )
		{
			case ConditionalNode:
			case NumberNode: type = ValueNumber; break;
			case FunctionCallNode: type = current->oper; break;
			case ArrayItemNode:
			case StringNode: type = ValueString; break;
			case ArrayNode: type = ValueArray; break;
			case HashNode: type = ValueHash; break;
 			case IdentNode: 
			{
				tmp = getsym( lexer, current->ident, 0 );
				if ( tmp ) type = tmp->type;
			}
			default: break;
		}
		rec = putsym( lexer, var->ident, type, var->oper );
	}

	if ( tmp == NULL && current->collate == IdentNode ) tmp = getsym( lexer, current->ident, 0 );

	if ( op == '=' ) {
		if ( rec->type == ValueNumber ) 
		{
			rec->value.num = current->num;
		} 
		else if ( rec->type == ValueString && current->str ) 
		{
			char *tmp = malloc( strlen( current->str ) + 1 );
			strcpy( tmp, current->str );
			if ( rec->value.str ) free(rec->value.str);
			rec->value.str = tmp;
		} 
		else if ( rec->type == ValueArray )
		{
			if ( tmp == NULL ) {
				tmp = getsym( lexer, "_ARRAY_", 1 );
			}
			if ( tmp ) {
				if ( rec->value.array ) array_delete(rec->value.array);
				rec->value.array = copy_array( tmp->value.array );
			}
		} 
		else if ( rec->type == ValueHash ) 
		{
			if ( tmp == NULL ) {
				tmp = getsym( lexer, "_HASH_", 1 );
			}
			if ( tmp ) {
				if ( rec->value.hash ) table_delete(rec->value.hash);
				rec->value.hash = copy_table(tmp->value.hash);
			}
		} 
		else if ( rec->type == ValueGdImage ) 
		{
			if ( tmp == NULL ) {
				tmp = getsym( lexer, "_GD_IMAGE_", 1 );
			}
			if ( tmp != NULL ) 
			{
				if ( 0 != rec->value.gd ) 
					gdImageDestroy(rec->value.gd);
				rec->value.gd = tmp->value.gd;
			}
		}
	}
	else if ( op == DOTASSIGN )
	{
		if ( rec->type == ValueString ) 
		{
			if ( rec->value.str && current->str ) {
				rec->value.str = realloc( rec->value.str, strlen(rec->value.str) + strlen(current->str) + 1 );
				strcat( rec->value.str, current->str );
			} else if ( !rec->value.str && current->str && strlen(current->str) ) {
				rec->value.str = malloc( strlen(current->str) + 1 );
				strcpy(rec->value.str, current->str );
			} else if ( !rec->value.str ) {
				rec->value.str = strdup("");
			}
		}
		else if ( rec->type == ValueHash && current->collate == HashNode ) 
		{
			tmp = getsym( lexer, "_HASH_", 1 );
			if ( tmp && tmp->type == ValueHash )
			{
				table *t = tmp->value.hash;
				table_entry *elts = (table_entry *) t->a.elts;
				int i;
				for (i = 0; i < t->a.nelts; ++i) {
					table_set( rec->value.hash, elts[i].key, elts[i].val );
				}
			}
		}
		else if ( rec->type == ValueArray ) 
		{
		}
		
	} else {
		double x = rec->value.num;
		switch ( op )
		{
			case PLUSASSIGN: x += current->num; break;
			case MINUSASSIGN: x -= current->num; break;
			case MULTASSIGN: x *= current->num; break;
			case DIVASSIGN: {
				if ( current->num > 0 ) 
					x /= current->num; 
				else {
#ifdef SYSLOG
					syslog(LOG_WARNING, "%s.%i: Division by zero\n", __FILE__, __LINE__ );
#endif
				} 
				break;
			}
			case LSHIFTASSIGN: x = (unsigned int)x << (unsigned int)current->num; break;
			case RSHIFTASSIGN: x = (unsigned int)x >> (unsigned int)current->num; break;
			case ANDASSIGN: x = (unsigned int)x & (unsigned int)current->num; break;
			case MODASSIGN: x = (unsigned int)x % (unsigned int)current->num; break;
			case XORASSIGN: x = (unsigned int)x ^ (unsigned int)current->num; break;
			case ORASSIGN: x = (unsigned int)x | (unsigned int)current->num; break;
			default: break;
		}
		rec->value.num = x;
	}
}

int CallFunction( Lexer *lexer, char *name, void **data, double *num )
{
    const size_t    fname_size = strlen(name) + 90;
    char            fname[fname_size];

	sprintf ( fname, "%s_FUNC_IN_%d", name, lexer->func_suff );
// 	strcpy( fname, name );
// 	strcat( fname, "_FUNC_IN" );

	array_header *array;
	symrec *args = getsym( lexer, fname, 0 );
	if ( args ) {
		array = args->value.array;
	} else {
		array = make_array(0, sizeof(char*));
	}
	
// 	log_debug( g_log, "Try to find function '%s' with %d params", name, array->nelts );
	
	int i;
	for ( i = 0; FunctionTable[i].name != 0; i++ )
	{
//         fprintf( stderr, "%s(%d)(%d) == %s(%d)\n",  FunctionTable[i].name, FunctionTable[i].argc, FunctionTable[i].rtype,
// 				 name, array->nelts );
		if ( strcmp(FunctionTable[i].name, name ) == 0
				   && ( FunctionTable[i].argc == array->nelts || FunctionTable[i].argc == -1 ))
		{
// 			log_debug( g_log, "CallFunction '%s' with %d params", name, array->nelts );
			*data = (FunctionTable[i].funct)(lexer,array,num);
			array_clear(array);
			return FunctionTable[i].rtype;
		}
	}
	
// 	log_debug( g_log, "Not found function '%s' with %d params", name, array->nelts );
// 	log_debug( g_log, "Try to call element '%s'", name );

	// try to find element and execute them
	char *elcont = FindPageElement( lexer, name );

	if ( elcont && *elcont != '\0' )
	{
		table_set( lexer->req->env, "ELEMENT", name );
#ifdef SYSLOG
		char *tab = tabs(lexer->depth);
		syslog( LOG_DEBUG, "%s.%i: %s %s\n", __FILE__, __LINE__, tab, name ) ;
		free(tab);
#endif
		
// 		log_debug( g_log, "Call element '%s'", name );

		Lexer *flex = Lexer_new();
		
		flex->depth = lexer->depth;
		flex->pcre_table = lexer->pcre_table;
		flex->req = lexer->req;
		flex->sym_head = lexer->sym_head;
		flex->sym_tail = lexer->sym_tail;
		flex->session_init = lexer->session_init;
		flex->exit = lexer->exit;
		flex->trigg_break = lexer->trigg_break;
		flex->trigg_continue = lexer->trigg_continue;
		flex->depth++;
		Lexer_setCode( flex, preprocessor( elcont, strlen(elcont) ), 0 );
		int code = yyparse( flex );
		if ( code )
		{
            const size_t    t_size = strlen(flex->errmsg) + 64;
            char            t[t_size];

		    snprintf(t, t_size, "SYNTAX error: %s at line %d, column: %d\n",
					 flex->errmsg, flex->yylineno+1, flex->pos);

#ifdef SYSLOG
		    char *tab = tabs(lexer->depth);
    		syslog( LOG_DEBUG, "%s.%i: %s %s\n", __FILE__, __LINE__, tab, t ) ;
	        free(tab);
#endif
			
			if ( ( out->len + t_size + 5 ) >= (size_t)out->size ) {
				out->size *= 2;
				out->data = realloc( out->data, out->size );
			}
			
			strncat( out->data, t, t_size );
			strncat( out->data, "<br/>", 5 );
			out->len += t_size;
			
			flex->exit = 1;
		} else {
			ReduceNode( flex, 0, flex->start );
		}
		flex->depth--;
		flex->pcre_table = 0;
		lexer->depth = flex->depth;
		lexer->sym_head = flex->sym_head;
		lexer->sym_tail = flex->sym_tail;
		lexer->session_init = flex->session_init;
		lexer->exit = flex->exit;
		lexer->trigg_break = flex->trigg_break;
		lexer->trigg_continue = flex->trigg_continue;
		Lexer_free( flex );
	}
// 	else 
// 		log_debug( g_log, "Not found element '%s'", name );
	
	free(elcont);

	array_clear(array);
	return 0;
}

///////////////////////////////////////////////

char *tabs( int num )
{
	char *s = malloc( num * sizeof(char) + 1);
	strcpy(s,"");
	//s[0] = '\0';
	int i;
	for( i = 0; i < num; i++ )
		strcat(s, "\t");

	//s[ num * sizeof(char) + 1 ] = '\0';
	return s;
}

int isLegalVarName( const char *s ) 
{
	if ( !isIdentLetter( s[0] ) ) return 0;
	
	while( *s ) 
	{
		if ( !isIdentLetter(*s) && !isDecimalDigit(*s) )
			return 0;
		s++;
	}

	return 1;
}

void DoExec( Lexer *lexer, const char *sql )
{
	symrec *stat = getsym(lexer, "QUERY", 0 );
	if( stat == NULL ) {
		stat = putsym(lexer, "QUERY", ValueHash, 0 );
		stat->value.hash = make_table(DEFAULT_TABLE_NELTS);
	} else {
		table_clear( stat->value.hash );
	}

	double s0, s1;
	get_microtime( &s0, (char*)0);
	alarm( g_config->worker_args.sql_timeout );
	PGresult *result = PQexec( database_user_or_anon(g_database), sql );
	alarm(0);
	get_microtime( &s1, (char*)0);
#ifdef SYSLOG
	syslog( LOG_INFO, "QUERY TIME: %.8f\n", s1-s0 );
#endif

    {
        char    tmp[32];
	
        snprintf(tmp, sizeof(tmp), "%i", PQresultStatus(result));
	    table_set( stat->value.hash, "status", tmp);
    };

	table_set( stat->value.hash, "query", sql );

	if ( PQresultStatus( result ) == 7 ) {
		debugSqlError( lexer, stat->value.hash, PQerrorMessage(database_user_or_anon(g_database)) );
	} else {
		table_set( stat->value.hash, "rows", PQcmdTuples(result) );
	}

	PQclear(result);
}


char *FindPageElement( Lexer *lexer, const char *e )
{
	char *z = NULL;
	char *tmp = NULL;
	PGresult *result = NULL;
	char *paramValues[2];
    
//     fprintf( stderr, "FindPageElement( %s )\n", e );
	
	paramValues[0] = (char*)e;
	paramValues[1] = (char*)table_get( lexer->req->env, "PAGE_ID");

    if ( e[0] == '/' ) {
		result = PQexecParams( database_user_or_anon(g_database), "SELECT elem_val_by_path( $1::TEXT )",
                               1, NULL, (const char **)&paramValues, NULL, NULL, 0 );
    } else {
		result = PQexecParams( database_user_or_anon(g_database), "SELECT elem_val( $1::TEXT, $2::INT4 )",
						2, NULL, (const char **)&paramValues, NULL, NULL, 0 );
    }

	if ( PQresultStatus( result ) == PGRES_TUPLES_OK && PQntuples( result ) == 1 ) {
		tmp = PQgetvalue( result, 0, 0 );
		z = malloc( strlen(tmp) + 1 );
		strcpy( z, tmp );
		PQclear( result );
	} else {
		z = malloc(1);
		z[0] = '\0';
	}
	
    return z;

}

void debugSqlError( Lexer *lexer, table *sqlStat, const char *msg )
{
	const char *scheme = table_get( lexer->req->env, "HTTP_SCHEME" );
	const char *host = table_get( lexer->req->env, "HTTP_HOST" );
	const char *uri = table_get( lexer->req->env, "REQUEST_URI" );
	const char *page_trail = table_get( lexer->req->env, "PAGE_TRAIL" );
	const char *element = table_get( lexer->req->env, "ELEMENT" );
	char *pgError = getword( &msg, '\n' );

    const size_t    err_msg_len = strlen(scheme) +
                                  strlen(host) +
                                  strlen(uri) +
                                  strlen(page_trail) +
                                  strlen(pgError) +
                                  ((element != 0) ? strlen(element) : 0) + 48;
    char            err_msg[err_msg_len];

	if ( element ) {
        snprintf(err_msg, err_msg_len, "SQL %s; URL(%s://%s%s); PAGE(%s); ELEMENT(%s);",
            pgError, scheme, host, uri, page_trail, element);
	} else {
        snprintf(err_msg, err_msg_len, "SQL %s; URL(%s://%s%s); PAGE(%s);",
            pgError, scheme, host, uri, page_trail);
	}

	table_set( sqlStat, "error", pgError );
	free( pgError );

#ifdef SYSLOG
	syslog( LOG_ERR, "%s\n", err_msg );
#endif
}

void ClearNodes( Node *n )
{
	if ( !n ) return;

// 	fprintf( stderr, "CLEAR NODE: %s(%i); MIN(%i); MAX(%i) \n", nameByType( n->type ), n->type, ProgramNode, ExecuteNode );
	
// 	if ( n->type < ProgramNode ) return;
// 	if ( n->type > ExecuteNode ) return;
	
	if ( n->str ) { /*fprintf( stderr, "[%s] (%p)\n", n->str, n->str );*/ free( n->str ); n->str = 0;  }
	if ( n->pattern ) { free( n->pattern ); n->pattern = 0;  }
	if ( n->flags ) { free( n->flags ); n->flags = 0; }
	if ( n->ident ) { free( n->ident ); n->ident = 0; }

	switch ( n->type ) {
		case ProgramNode:
		{
			if( n->prog ) ClearNodes( n->prog );
			break;
		}
		case SourceElementNode:
		case StatementNode:
		{
			if ( n->stat ) ClearNodes( n->stat );
			break;
		}
		case SourceElementListNode:
		case StatementListNode:
		{
			if( n->next ) ClearNodes( n->next );
			if( n->element ) ClearNodes( n->element );
			break;
		}
		case ArgumentsNode:
		{
			if( n->expr1 ) ClearNodes( n->expr1 );
			break;
		}
		case ArgumentListNode:
		{
			if( n->next ) ClearNodes( n->next );
			if( n->expr1 ) ClearNodes( n->expr1 );
			break;
		}
		case BlockNode:
		{
			if( n->list ) ClearNodes( n->list );
			break;
		}
		case ExprStatementNode:
		{
// 			debugNode( n );
			if ( n->expr1) ClearNodes( n->expr1 );
			break;
		}
		case CommaNode:
		{
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->expr2 ) ClearNodes( n->expr2 );
			break;
		}
		case ArrayNode:
		{
			if ( n->expr1 ) ClearNodes( n->expr1 );
			break;
		}
		case FunctionCallNode:
		{
// 			debugNode( n );
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->args ) ClearNodes( n->args );
			break;
		}
		case PostfixNode:
		case PrefixNode:
		{
			if ( n->expr1 ) ClearNodes( n->expr1 );
			break;
		}
		case MultNode:
		case AddNode: 
		case ShiftNode:
		case RelationalNode: 
		case EqualNode: 
		case BitOperNode:
		case LogicalNode:
		{
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->expr2 ) ClearNodes( n->expr2 );
			break;
		}
		case ConditionalNode:
		{
			if ( n->logical ) ClearNodes( n->logical );
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->expr2 ) ClearNodes( n->expr2 );
			break;
		}
		case AssignNode:
		{
			if ( n->stat ) ClearNodes( n->stat );
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->expr2 ) ClearNodes( n->expr2 );
			break;
		}
		case AssigOpNode: break;
		case IfStatementNode:
		{
			if ( n->logical ) ClearNodes( n->logical );
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->expr2 ) ClearNodes( n->expr2 );
			break;
		}
		case ElseStamentNode:
		{
			if ( n->expr1 ) ClearNodes( n->expr1 );
			break;
		}
		case DoWhileNode:
		case WhileNode:
		{
			if ( n->list ) ClearNodes( n->list );
			if ( n->logical ) ClearNodes( n->logical );
			break;
		}
		case ForNode:
		{
			if ( n->list ) ClearNodes( n->list );
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->expr2 ) ClearNodes( n->expr2 );
			if ( n->expr3 ) ClearNodes( n->expr3 );
			break;
		}
		case IdentNode: break;
		case ContinueNode: break;
		case BreakNode: break;
		case ReturnNode:
			if ( n->expr1) ClearNodes( n->expr1 );
			break;
		case ArrayItemNode:
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->args ) ClearNodes( n->args );
			break;
		case ForInNode:
			if ( n->expr2) ClearNodes( n->expr2 );
			if ( n->expr3) ClearNodes( n->expr3 );
			break;
		case SelectNode:
			if ( n->expr1) ClearNodes( n->expr1 );
			break;
		case InsertNode:
			if ( n->expr1 ) ClearNodes( n->expr1 );
			if ( n->expr2 ) ClearNodes( n->expr2 );
			break;
		case UpdateNode:
			if ( n->expr1 ) ClearNodes( n->expr1 );
			break;
		case DeleteNode:
			if ( n->expr2 ) ClearNodes( n->expr2 );
			break;
		case SqlClauseNode:
			if ( n->next ) ClearNodes( n->next );
            if ( n->expr1 ) ClearNodes( n->expr1 );
			break;
        case SqlTokenNode:
            if ( n->expr1 ) ClearNodes( n->expr1 );
            break;
		case ForSqlNode:
			if ( n->expr2 ) ClearNodes( n->expr2 );
			if ( n->expr3 ) ClearNodes( n->expr3 );
			break;
		case SelectStatementNode:
			if ( n->expr1 ) ClearNodes( n->expr1 );
			break;
        case ElementCallNode: 
            if ( n->expr1 ) ClearNodes( n->expr1 );
            if ( n->expr2 ) ClearNodes( n->expr2 );
            break;
        case ElementPathNode: 
            if( n->next ) ClearNodes( n->next );
            break;
		case ExecuteNode:
			break;
		default:
// 			fprintf( stderr, "UNKNOWN NODE TYPE IN CLEAR: %s!\n", nameByType(n->type));
			break;
	}

	// 	debugNode( n );
	free(n);
}

void debugNode( const Node *n )
{
	fprintf( stderr, "TYPE: %i\n", n->type );
	fprintf( stderr, "COLLATE: %i\n", n->collate);
	fprintf( stderr, "NUM: %f\n", n->num );
	fprintf( stderr, "NUM2: %f\n", n->num2 );
	fprintf( stderr, "STR: %s\n", n->str );
	fprintf( stderr, "PATTERN: %s\n", n->pattern);
	fprintf( stderr, "FLAGS: %s\n", n->flags);
	fprintf( stderr, "IDENT: %s\n", n->ident );
	fprintf( stderr, "LIST: %i\n", n->list != 0 );
	fprintf( stderr, "ELEMENT: %i\n", n->element != 0 );
	fprintf( stderr, "ELEMENTS: %i\n", n->elements != 0 );
	fprintf( stderr, "EXPR1: %i\n", n->expr1 != 0 );
	fprintf( stderr, "EXPR2: %i\n", n->expr2 != 0 );
	fprintf( stderr, "EXPR3: %i\n", n->expr3 != 0 );
	fprintf( stderr, "ARGS: %i\n", n->args != 0 );
	fprintf( stderr, "LOGICAL: %i\n", n->logical != 0 );
	fprintf( stderr, "STAT: %i\n", n->stat != 0 );
	fprintf( stderr, "PROG: %i\n", n->prog != 0 );
	fprintf( stderr, "NEXT: %i\n", n->next != 0 );
	fprintf( stderr, "PREV: %i\n", n->prev != 0 );
	fprintf( stderr, "OPER: %i\n", n->oper );
}

const char *nameByType( int i )
{
	switch(i) {
		case ProgramNode: return("ProgramNode");
		case SourceElementNode: return("SourceElementNode");
		case SourceElementListNode: return("SourceElementListNode");
		case StatementNode: return("StatementNode");
		case StatementListNode: return("StatementListNode");
		case ArgumentsNode: return("ArgumentsNode");
		case ArgumentListNode: return("ArgumentListNode");
		case BlockNode: return("BlockNode");
		case EmptyStatementNode: return("EmptyStatementNode");
		case ExprStatementNode: return("ExprStatementNode");
		case NumberNode: return("NumberNode");
		case StringNode: return("StringNode");
		case CommaNode: return("CommaNode");
		case ArrayNode: return("ArrayNode");
		case FunctionCallNode: return("FunctionCallNode");
		case PostfixNode: return("PostfixNode");
		case PrefixNode: return("PrefixNode");
		case MultNode: return("MultNode");
		case AddNode: return("AddNode");
		case ShiftNode: return("ShiftNode");
		case EqualNode: return("EqualNode");
		case BitOperNode: return("BitOperNode");
		case LogicalNode: return("LogicalNode");
		case ConditionalNode: return("ConditionalNode");
		case AssignNode: return("AssignNode");
		case AssigOpNode: return("AssigOpNode");
		case IfStatementNode: return("IfStatementNode");
		case ElseStamentNode: return("ElseStamentNode");
		case DoWhileNode: return("DoWhileNode");
		case WhileNode: return("WhileNode");
		case ForNode: return("ForNode");
		case ContinueNode: return("ContinueNode");
		case BreakNode: return("ContinueNode");
		case ReturnNode: return("ReturnNode");
		case ArrayItemNode: return("ArrayItemNode");
		case IdentNode: return("IdentNode");
		case ForInNode: return("ForInNode");
		case SelectNode: return("SelectNode");
		case InsertNode: return("InsertNode");
		case UpdateNode: return("UpdateNode");
		case DeleteNode: return("DeleteNode");
		case SqlClauseNode: return("SqlClauseNode");
        case SqlTokenNode: return("SqlTokenNode");
		case HashNode: return("HashNode");
		case ForSqlNode: return("ForSqlNode");
		case SelectStatementNode: return("SelectStatementNode");
		case ExecuteNode: return("ExecuteNode");
		case RelationalNode: return("RelationalNode");
        case ElementPathNode: return("ElementPathNode");
		default: {
			return "UNKNOWN";
		}
	}
}


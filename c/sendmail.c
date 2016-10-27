/* $Id: sendmail.c,v 1.40 2007/10/17 10:57:50 anton Exp $ */

#include "sendmail.h"
#include "base64.h"

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "database.h"
#include "global.h"
#include "log.h"

// From: from@mail.ru 
// Replay-to: no-replay@antora.ru
// X-Mailer: CMS1
// To: to@addres.net
// Subject: subject
// MIME-Version: 1.0
// Content-Type: multipart/mixed; boundary="-CMS-boundary"
//
// ---CMS-boundary
// Content-Type: text/html;	charset=koi8-r
// Content-Transfer-Encoding: base64
//
// message
//
// ---CMS-boundary
// Content-Type: attachtype name="attachname"
// Content-Transfer-Encoding: base64
// Content-Disposition: attachment; filename="attachname"
//
// attach
//
// ---CMS-boundary-

/* XXX all sends and recvs here is UNRELIABLE! */

char *encode_mime ( const char *str );

char *encode_mime ( const char *str )
{
	char *org = strdup(str);
	int orglen = strlen(org);
	
	int reslen = 1;
	char *result = malloc( reslen );
	result[reslen - 1] = '\0';
	
	char *t, *t1;
	int i, encode;
	char *tmp = NULL;
	int tlen = 0;

	while(*org != '\0')
	{
		t = getword( (const char **)&org, ',' );
		tlen = strlen(t);
		while(*t != '\0')
		{
			t1 = getword( (const char**)&t, ' ' );

			encode = 0;

			for ( i=0; t1[i] != '\0'; i++) {
				if ( ( t1[i]&0x80 ) > 0 ) { encode = 1; break; }
			}

			if ( encode ) 
			{
				int len = 0;

				t1 = realloc( t1, strlen(t1) + 2 );
				strcat( t1, " " );
// 				tmp = malloc( strlen(t1) + 2 );
// 				sprintf( tmp, "%s ", t1 );
// 				free(t1);
// 				t1 = tmp;
				
				char *b = (char *)base64_encode( (const unsigned char*)t1, strlen(t1), &len );
	
				reslen += 15 + len;
				
                {
                    const size_t    alt_tmp_size = len + 15;
                    char            alt_tmp[alt_tmp_size];

                    snprintf(alt_tmp, alt_tmp_size, "=?koi8-r?b?%s?= ", b);
                    result = realloc(result, reslen);   /* XXX */
                    strcat(result, alt_tmp);
                };
			} else {
// 				fprintf( stderr, "%i\n", reslen );
				reslen += strlen(t1) + 1;
// 				fprintf( stderr, "%i\n", reslen );
				result = realloc( result, reslen );
				strcat( result, t1 );
				strcat( result, " " );
			}
			
			free(t1);
			
		}
		
		t -= tlen;
		free(t);
		
		if (*org != '\0') {
			reslen += 1;
			result = realloc( result, reslen );
			strcat( result, "," );
		}
	}
	
	org -= orglen;
	free(org);

	return result;
}

table *cms1_mailto( Lexer *lexer, table *headers, table *msg, array_header *attach )
{
    /* WARNING tmp array sizes here is essential, do not touch it */

	int i;
	int len = 0;
	int sock;
	int multipart;
	struct sockaddr_in name;
	struct hostent *hostinfo;
	// struct sockaddr *name;

	int blen = 0;
	char *buff = NULL;
	char *tmp = NULL;
	const char *boundary = "--=CMS_outer_boundary_000";
	size_t boundary_len = strlen(boundary);

	const char *inner_boundary = "--=CMS_inner_boundary_001";
	size_t inner_boundary_len = strlen(inner_boundary);

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;

	sock = socket(PF_INET, SOCK_STREAM, 0);

	table* hash = make_table(2);

	table_set( hash, "status", "0" );
	table_set( hash, "error", "No error" );

	const char *text = table_get( msg, "text");
	const char *html = table_get( msg, "html");
	
	char *from = (char*)table_get( headers, "From" );
	char *to = (char*)table_get( headers, "To" );
	char *cc = (char*)table_get( headers, "Cc" );
	char *bcc = (char*)table_get( headers, "Bcc" );
	char *subject = (char*)table_get( headers, "Subject" );


// 	log_debug( g_log, " MSG: %i; ATTACH: %i;", msg->a.nelts, attach->nelts );
// 	log_debug( g_log, "%s\n", subject );
	
	multipart = msg->a.nelts > 1 || attach->nelts > 0 ? 1 : 0;

	if ( from == NULL || to == NULL )
	{
		table_set( hash, "status", "-1" );
		table_set( hash, "error", "Missing required headers: 'From' or 'To'" );
		return hash;
	}

	size_t subj_len = subject ? strlen(subject) : 0;
	if ( subj_len == 0 ) {
		subject = "No subject";
		table_set( headers, "Subject", subject );
	}
	else if ( strncasecmp(subject, "=?koi8-r?b?", 11 ) ) {
		buff = (char *)base64_encode( (const unsigned char*)subject, subj_len, &len );

        const size_t    alt_subject_size = len + 14;
        char            alt_subject[alt_subject_size];

        snprintf(alt_subject, alt_subject_size, "=?koi8-r?b?%s?=", buff);
		table_set(headers, "Subject", alt_subject);
		free(buff);
	}
	
	to = encode_mime( to );
	table_set( headers, "To", to );
	
	from = encode_mime( from );
	table_set( headers, "From", from );
	
	if ( 0 != cc ) {
		cc = encode_mime( cc );
		table_set( headers, "Cc", cc );
	}
	
	if ( 0 != bcc ) {
		bcc = encode_mime( bcc );
		table_set( headers, "Bcc", bcc );
	}

	table_set( headers, "X-CMS-Host", table_get(lexer->req->env, "HTTP_HOST" ) );
	table_set( headers, "X-CMS-Request-URI", table_get(lexer->req->env, "REQUEST_URI" ) );
	table_set( headers, "X-CMS-Host-ID", table_get(lexer->req->env, "HOST_ID" ) );
	table_set( headers, "X-CMS-Page-ID", table_get(lexer->req->env, "PAGE_ID" ) );

	hostinfo = gethostbyname( lexer->req->smtp_host );
	if ( hostinfo == NULL ) {
		table_set( hash, "status", "1" );
		table_set( hash, "error", "Could not gethostbyname" );
		return hash;
	}

	name.sin_addr = *((struct in_addr *) hostinfo->h_addr);
	name.sin_port = htons( lexer->req->smtp_port );

// 	log_debug( g_log, "connect ... " );
	if ( connect( sock, (struct sockaddr*)&name, sizeof(struct sockaddr_in)) == -1 ) {
		table_set( hash, "status", "2" );
		table_set( hash, "error", "Could not connect to mail server" );
		return hash;
	}

    const size_t    read_block = 256;
    char            srv[read_block];

	// connected
	recv(sock, srv, read_block, 0 );
	if ( strncmp( srv, "220", 3 ) )
	{
		table_set( hash, "status", "3" );
		table_set( hash, "error", "Mail server not ready" );
		close(sock);
		return hash;
	}
// 	log_debug( g_log, "connectd!");

	send(sock, (const void*)"EHLO\r\n", 7, MSG_OOB );
// 	log_debug( g_log, "EHLO");

	recv(sock, srv, read_block, 0 );
	if ( strncmp( srv, "250", 3 ) )
	{
		table_set( hash, "status", "4" );
		table_set( hash, "error", "Wrong hello message" );
		close(sock);
		return hash;
	}

	// try to authenticate
	char *auth = NULL;
	
	if ( lexer->req->smtp_auth_plain ) {
		auth = strdup(lexer->req->smtp_auth_plain);
	}
	else if ( lexer->req->smtp_username && lexer->req->smtp_password )
	{
        const size_t    alt_buff_size = strlen(lexer->req->smtp_username) * 2 +
                                        strlen(lexer->req->smtp_password) + 3;
        char            alt_buff[alt_buff_size];

        snprintf(alt_buff, alt_buff_size, "%s%c%s%c%s", lexer->req->smtp_username,
            '\0', lexer->req->smtp_username, '\0', lexer->req->smtp_password);

		auth = (char *)base64_encode( (const unsigned char*)alt_buff, alt_buff_size, NULL );
	}

	if ( auth )
	{
        const size_t    alt_buff_size = strlen(auth) + 14;
        char            alt_buff[alt_buff_size];

        snprintf(alt_buff, alt_buff_size, "AUTH PLAIN %s\r\n", auth);
		free(auth);
		
// 		log_debug( g_log, "%s", buff);

		send(sock, (const void*)alt_buff, alt_buff_size, MSG_OOB);
		
		recv(sock, srv, read_block, 0 ); 
		if ( strncmp(srv, "235", 3 ) ) {
			table_set( hash, "status", "5" );
			table_set( hash, "error", "Could not authenticate on smtp" );
			table_set( hash, "srv", srv );
			close(sock);
			return hash;
		}
	}

// 	log_debug( g_log, "data handsnake" );

    {
        const size_t    alt_buff_size = strlen(from) + 14;
        char            alt_buff[alt_buff_size];

        snprintf(alt_buff, alt_buff_size, "MAIL FROM: %s\r\n", from);
        free(from);

    	if(-1 == send(sock, alt_buff, alt_buff_size, MSG_OOB)){
			log_debug( g_log, "send filure: %s", strerror(errno) );
    	};
    };

	recv(sock, srv, read_block, 0 );
	if ( strncmp(srv, "250", 3 ) ) {
		table_set( hash, "status", "6" );
 		table_set( hash, "error", "Error on MAIL smtp command" );
		table_set( hash, "srv", srv );
		close(sock);
		return hash;
	}

	int tolen = strlen(to);
	while(*to != '\0')
	{
		char *t = getword( (const char **)&to, ',');
        const size_t    alt_buff_size = strlen(t) + 11;
        char            alt_buff[alt_buff_size];

        snprintf(alt_buff, alt_buff_size, "RCPT TO: %s\n", t);
        free(t);

		send(sock, (const void*)alt_buff, alt_buff_size, MSG_OOB );

		recv(sock, srv, read_block, 0 );
		if ( strncmp(srv, "250", 3 ) ) {
			table_set( hash, "status", "7" );
			table_set( hash, "error", "Error on RCPT TO smtp command" );
			close(sock);
			return hash;
		}
	}
	to -= tolen;
	free(to);
	
	if ( 0 != cc ) 
	{
// 		log_debug( g_log, "RCPT TO(Cc): %s", cc );
		int cclen = strlen(cc);
		while(*cc != '\0')
		{
			char *t = getword( (const char **)&cc, ',');
			const size_t    alt_buff_size = strlen(t) + 11;
			char            alt_buff[alt_buff_size];

			snprintf(alt_buff, alt_buff_size, "RCPT TO: %s\n", t);
			free(t);

			send(sock, (const void*)alt_buff, alt_buff_size, MSG_OOB );

			recv(sock, srv, read_block, 0 );
			if ( strncmp(srv, "250", 3 ) ) {
				table_set( hash, "status", "7" );
				table_set( hash, "error", "Error on RCPT TO(Cc) smtp command" );
				close(sock);
				return hash;
			}
		}
		cc -= cclen;
		free(cc);
	}
	
	if ( 0 != bcc ) 
	{
// 		log_debug( g_log, "RCPT TO(Bcc): %s", cc );
		int bcclen = strlen(bcc);
		while(*bcc != '\0')
		{
			char *t = getword( (const char **)&bcc, ',');
			const size_t    alt_buff_size = strlen(t) + 11;
			char            alt_buff[alt_buff_size];

			snprintf(alt_buff, alt_buff_size, "RCPT TO: %s\n", t);
			free(t);

			send(sock, (const void*)alt_buff, alt_buff_size, MSG_OOB );

			recv(sock, srv, read_block, 0 );
			if ( strncmp(srv, "250", 3 ) ) {
				table_set( hash, "status", "7" );
				table_set( hash, "error", "Error on RCPT TO(Bcc) smtp command" );
				close(sock);
				return hash;
			}
		}
		bcc -= bcclen;
		free(bcc);
	}

// 	log_debug( g_log, "DATA" );
	
	send(sock, (const void*)"DATA\n", 6, MSG_OOB );
	recv(sock, srv, read_block, 0 );
	
	if ( strncmp(srv, "354", 3 ) ) {
		table_set( hash, "status", "8" );
		table_set( hash, "error", "Error on DATA smtp command" );
		close(sock);
		return hash;
	}
	
	// 1.
	// BUILD HEADERS PART OF MESSAGE
	
	table_entry *elts = (table_entry *) headers->a.elts;
	for (i = 0; i < headers->a.nelts; i++ )
	{
        const size_t    alt_buff_size = strlen(elts[i].key) +
                                        strlen(elts[i].val) + 4;
        char            alt_buff[alt_buff_size];

		snprintf(alt_buff, alt_buff_size, "%s: %s\n", elts[i].key, elts[i].val);
		alt_buff[alt_buff_size - 1] = '\0';
// 		log_debug( g_log, "%s", alt_buff );
		send(sock, (const void*)alt_buff, alt_buff_size - 1, 0 );
	}

	send(sock, (const void*)"MIME-Version: 1.0\n", 18, 0 );
	if ( multipart ) {
		send(sock, (const void*)"Content-Type: multipart/mixed; boundary=\"", 41, 0 );
		send(sock, (const void*)boundary, boundary_len, 0 );
		send(sock, (const void*)"\"\n\n", 3, 0 );
	}
	
	// 2.
	// APPEND MESSAGE TEXT-PART

	if ( html && text )
	{
		send(sock, (const void*)"--", 2, 0 );
		send(sock, (const void*)boundary, boundary_len, 0 );
		send(sock, (const void*)"\n", 1, 0 );

		send(sock, (const void*)"Content-Type: multipart/alternative; boundary=\"", 47, 0 );
		send(sock, (const void*)inner_boundary, inner_boundary_len, 0 );
		send(sock, (const void*)"\"\n\n", 3, 0 );
	}

	if ( text )
	{
		if ( multipart ) {
			send(sock, (const void*)"--", 2, 0 );
			if ( html )
				send(sock, (const void*)inner_boundary, inner_boundary_len, 0 );
			else
				send(sock, (const void*)boundary, boundary_len, 0 );
			send(sock, (const void*)"\n", 1, 0 );
		}

		send(sock, (const void*)"Content-Type: text/plain; charset=koi8-r\n", 41, 0 );
		send(sock, (const void*)"Content-Transfer-Encoding: base64\n\n", 35, 0 );
	
		buff = (char*)base64_encode( (const unsigned char*)text, strlen(text), &len );
		send(sock, (const void*)buff, len-1, 0 );
		free(buff);

		send(sock, (const void*)"\n\n", 2, 0 );
	}

	if ( html )
	{
		if ( multipart ) {
			send(sock, (const void*)"--", 2, 0 );
			if ( text )
				send(sock, (const void*)inner_boundary, inner_boundary_len, 0 );
			else
				send(sock, (const void*)boundary, boundary_len, 0 );
			send(sock, (const void*)"\n", 1, 0 );
		}
		
		send(sock, (const void*)"Content-Type: text/html; charset=koi8-r\n", 40, 0 );
		send(sock, (const void*)"Content-Transfer-Encoding: base64\n\n", 36, 0 );
	
		buff = (char*)base64_encode( (const unsigned char*)html, strlen(html), &len );
		send(sock, (const void*)buff, len, 0 );
		free(buff);

		send(sock, (const void*)"\n\n", 2, 0 );
	}

	if ( text && html )
	{
		send(sock, (const void*)"--", 2, 0 );
		send(sock, (const void*)inner_boundary, inner_boundary_len, 0 );
		send(sock, (const void*)"--\n", 3, 0 );
	}
	

	// 3.
	// ITERATE OVER ATTACHMENTS ARRAY
	PGresult *result = NULL;
	char *paramValues[1];
	
	for (i = 0; i < attach->nelts; i++ )
	{
		paramValues[0] = ((char**)attach->elts)[i];

		result = PQexecParams( database_user_or_anon(g_database),
							   "SELECT filename, pid, name, mimetype, lo, size FROM blob WHERE id = $1",
							   1, NULL, (const char **)&paramValues, NULL, NULL, 0 );
		if ( PQntuples(result) == 0 || PQresultStatus(result) != PGRES_TUPLES_OK )
			continue;

// 		log_debug( g_log, "ATTACH: %s", ((char**)attach->elts)[i] );

		char *filename = PQgetvalue( result, 0, 0 );
		char *pid = PQgetvalue( result, 0, 1 );
		char *name = PQgetvalue( result, 0, 2 );
		char *mime = PQgetvalue( result, 0, 3 );

		send(sock, (const void*)"--", 2, 0 );
		send(sock, (const void*)boundary, boundary_len, 0 );
		send(sock, (const void*)"\n", 1, 0 );

        {
            const size_t    alt_buff_size = strlen(mime) +
                                            strlen(filename) + 25;
            char            alt_buff[alt_buff_size];

		    snprintf(alt_buff, alt_buff_size, "Content-Type: %s; "
                "name=\"%s\"\n", mime, filename);
		
            send(sock, (const void*)alt_buff, alt_buff_size, 0 );
        };

        {
    		tmp = (char*)table_get( lexer->req->env, "HTTP_HOST" );

            const size_t    alt_buff_size = strlen(tmp) +
                                            strlen(pid) +
                                            strlen(name) + 35;
            char            alt_buff[alt_buff_size];

        	snprintf(alt_buff, alt_buff_size, "Content-Location: http://%s/file/%s/%s\n",
                tmp, pid, name);
    		send(sock, (const void*)alt_buff, alt_buff_size, 0 );
        };

		send(sock, (const void*)"Content-Transfer-Encoding: base64\n", 35, 0 );

        {
            const size_t    alt_buff_size = strlen(filename) + 48;
            char            alt_buff[alt_buff_size];

    		snprintf(alt_buff, alt_buff_size, "Content-Disposition: attachment; filename=\"%s\"\n\n", filename);
    		send(sock, (const void*)alt_buff, alt_buff_size, 0 );
        };

		buff = (char*)PQunescapeBytea( (const unsigned char*)PQgetvalue( result, 0, 4 ), (size_t*)&len );
		tmp = (char*)base64_encode( (const unsigned char*)buff, len, &len );
		send(sock, (const void*)tmp, len, 0 );
		free(tmp);

		PQfreemem(buff);
		PQclear(result);

		send(sock, (const void*)"\n\n", 2, 0 );
	}

	if ( multipart ) {
		send(sock, (const void*)"--", 2, 0 );
		send(sock, (const void*)boundary, boundary_len, 0 );
		send(sock, (const void*)"--\n", 3, 0 );
	}

// 	log_debug( g_log, "." );
	send(sock, (const void*)".\n", 2, 0 );

	recv(sock, srv, read_block, 0 );
	if ( strncmp(srv, "250", 3 ) ) {
		table_set( hash, "status", "9" );
		table_set( hash, "error", "Error on END DATA(.) smtp command" );
		close(sock);
		return hash;
	}

// 	log_debug( g_log, "QUIT" );
	send(sock, (const void*)"QUIT\n", 5, 0 );
	
	close( sock );
	return hash;
}

FUNCT_DEF(send_mail3)
{
	table *msg;
	array_header *attach;
	table *headers;
	symrec *ptr;
	table *result;
	
	// get headers
	ptr = getsym( lexer, GET_ARG_STRING(0), 0 );
	if ( ptr ) headers = copy_table(ptr->value.hash);
	else headers = make_table( DEFAULT_TABLE_NELTS );

	ptr = getsym( lexer, GET_ARG_STRING(1), 0 );
	if ( ptr ) msg = copy_table(ptr->value.hash);
	else msg = make_table( DEFAULT_TABLE_NELTS );
	
	ptr = getsym( lexer, GET_ARG_STRING(2), 0 );
	if ( ptr ) attach = copy_array( ptr->value.array );
	else attach = make_array( 1, sizeof(char *) );

	ptr = 0;
	
	result = cms1_mailto( lexer, headers, msg, attach );
	table_delete( headers );
	table_delete( msg );
	array_delete( attach );
	
	return result;
}

FUNCT_DEF(send_mail2)
{
	table *msg;
	array_header *attach;
	table *headers;
	symrec *ptr;
	table *result;

	// get headers
	ptr = getsym( lexer, GET_ARG_STRING(0), 0 );
	if ( ptr ) headers = copy_table( ptr->value.hash );
	else headers = make_table( DEFAULT_TABLE_NELTS );

	ptr = getsym( lexer, GET_ARG_STRING(1), 0 );
	if ( ptr ) msg = copy_table( ptr->value.hash );
	else msg = make_table( DEFAULT_TABLE_NELTS );
	
	attach = make_array( 1, sizeof(char *) );

	ptr = 0;

	result = cms1_mailto( lexer, headers, msg, attach );
	table_delete( headers );
	table_delete( msg );
	array_delete( attach );
	
	return result;
}


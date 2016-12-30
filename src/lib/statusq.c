/*###########################################################################
#
# This file contains a portion of code from Samba package, 
# which contains the following license:                    
# Unix SMB/Netbios implementation
# Version 1.9
# Main SMB server routine
# Copyright (C) Andrew Tridgell 1992-199
#
# written by :	Stephen J. Friedl
#		Software Consultant
#		steve@unixwiz.net
#
# Copyright (C) 2007 - 2008 by
# nixkoenner <nixkoenner@newnigma2.to>
# License: GPL
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
###########################################################################*/

#define BSD_SOURCE
#include <ctype.h>
#include <endian.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include "statusq.h"

#define MIN(a,b)	((a) < (b) ? (a) : (b))

/* Start of code from Samba */
static int name_mangle(const char *In, char *Out, char name_type) {
	int   i;
	int   c;
	int   len;
	char  buf[20];
	char *p = Out;
	char* scope="";

	/* Safely copy the input string, In, into buf[]. */
	(void)memset( buf, 0, 20 );
	if (strcmp(In,"*") == 0) buf[0] = '*';
	else
#ifdef HAVE_SNPRINTF
		(void)snprintf( buf, sizeof(buf) - 1, "%-15.15s%c", In, name_type );
#else
		(void)sprintf( buf, "%-15.15s%c", In, name_type );
#endif /* HAVE_SNPRINTF */

	/* Place the length of the first field into the output buffer. */
	p[0] = 32;
	p++;

	/* Now convert the name to the rfc1001/1002 format. */
	for( i = 0; i < 16; i++ ) {
		c = toupper( buf[i] );
		p[i*2]     = ( (c >> 4) & 0x000F ) + 'A';
		p[(i*2)+1] = (c & 0x000F) + 'A';
	}
	p += 32;
	p[0] = '\0';

	/* Add the scope string. */
	for( i = 0, len = 0; NULL != scope; i++, len++ ) {
		switch( scope[i] ) {
      		case '\0':
        		p[0]     = len;
        		if( len > 0 ) p[len+1] = 0;
        		return( strlen(Out) );
      		case '.':
        		p[0] = len;
      		p   += (len + 1);
    		len  = 0;
        		break;
      		default:
                        p[len+1] = scope[i];
                	break;
		}
    	}
        return( strlen(Out) );
}; /* name_mangle */
/* end of code from Samba */


int send_query(int sock, const struct sockaddr *sa, uint32_t rtt_base)
{
        struct nbname_request request;
	int status;
	struct timeval tv;

        request.flags = htons(FL_BROADCAST);
        request.question_count = htons(1);
        request.answer_count = 0;
        request.name_service_count = 0;
        request.additional_record_count = 0;
        name_mangle("*", request.question_name,0);
        request.question_type = htons(0x21);
        request.question_class = htons(0x01);

	gettimeofday(&tv, NULL);
	/* Use transaction ID as a timestamp */
	request.transaction_id = htons((tv.tv_sec-rtt_base)*1000+tv.tv_usec/1000);
	/* printf("%s: timestamp: %d\n", inet_ntoa(dest_addr), request.transaction_id); */
        
	status = sendto(sock, (char*)&request, sizeof(request), 0, sa, sizeof(struct sockaddr_storage));
	if(status==-1) {
		char addrstr[INET6_ADDRSTRLEN];
		getnameinfo(sa, sizeof(struct sockaddr_storage), addrstr, sizeof(addrstr), NULL, 0, NI_NUMERICHOST);
		fprintf(stderr, "sendto %s: %m", addrstr);
		return -1;
        }
	return 0;
};

static inline uint32_t get32(const void *data)
{
	return be32toh(*(const uint32_t *)data);
}

static inline uint16_t get16(const void *data)
{
	return be16toh(*(const uint16_t *)data);
}

void parse_response(const char *buff, int buffsize, struct nb_host_info *hostinfo)
{
	nbname_response_footer_t* response_footer;
	nbname_response_header_t* response_header;
	int name_table_size;
	int offset = 0;

	memset(hostinfo, 0, sizeof(struct nb_host_info));

	response_header = &hostinfo->header;
	response_footer = &hostinfo->footer;

	/* Parsing received packet */
	/* Start with header */
	if( offset+sizeof(response_header->transaction_id) >= buffsize) goto broken_packet;
	response_header->transaction_id = get16(buff+offset); 
	//Move pointer to the next structure field
	offset+=sizeof(response_header->transaction_id);

	// Check if there is room for next field in buffer
	if( offset+sizeof(response_header->flags) >= buffsize) goto broken_packet; 
	response_header->flags = get16(buff+offset);
        offset+=sizeof(response_header->flags);
	
	if( offset+sizeof(response_header->question_count) >= buffsize) goto broken_packet;
	response_header->question_count = get16(buff+offset);
        offset+=sizeof(response_header->question_count);
        
	if( offset+sizeof(response_header->answer_count) >= buffsize) goto broken_packet;
	response_header->answer_count = get16(buff+offset);
        offset+=sizeof(response_header->answer_count);
        
	if( offset+sizeof(response_header->name_service_count) >= buffsize) goto broken_packet;
	response_header->name_service_count = get16(buff+offset);
        offset+=sizeof(response_header->name_service_count);
        
	if( offset+sizeof(response_header->additional_record_count) >= buffsize) goto broken_packet;
	response_header->additional_record_count = get16(buff+offset);
        offset+=sizeof(response_header->additional_record_count);
        
	if( offset+sizeof(response_header->question_name) >= buffsize) goto broken_packet;
	strncpy(response_header->question_name, buff+offset, sizeof(response_header->question_name));
        offset+=sizeof(response_header->question_name);
        
	if( offset+sizeof(response_header->question_type) >= buffsize) goto broken_packet;
	response_header->question_type = get16(buff+offset);
        offset+=sizeof(response_header->question_type);
        
	if( offset+sizeof(response_header->question_class) >= buffsize) goto broken_packet;
	response_header->question_class = get16(buff+offset);
        offset+=sizeof(response_header->question_class);
        
	if( offset+sizeof(response_header->ttl) >= buffsize) goto broken_packet;
        response_header->ttl = get32(buff+offset);
        offset+=sizeof(response_header->ttl);
        
	if( offset+sizeof(response_header->rdata_length) >= buffsize) goto broken_packet;
        response_header->rdata_length = get16(buff+offset);
        offset+=sizeof(response_header->rdata_length);
        
	if( offset+sizeof(response_header->number_of_names) >= buffsize) goto broken_packet;
	response_header->number_of_names = *(typeof(response_header->number_of_names)*)(buff+offset);
  offset+=sizeof(response_header->number_of_names);

	/* Done with packet header - it is okay */
	
	name_table_size = (response_header->number_of_names) * (sizeof(struct nbname));
	if( offset+name_table_size >= buffsize) goto broken_packet;
	
	memcpy(hostinfo->names, buff + offset, MIN(name_table_size, sizeof(hostinfo->names)));
	//printf("DEBUG: %s , %d\n", hostinfo->names, name_table_size);
	
	offset+=name_table_size;

	/* Done with name table - it is okay */ 
 
        /* Now parse response footer */

	if( offset+sizeof(response_footer->adapter_address) >= buffsize) goto broken_packet;	
	memcpy(response_footer->adapter_address, 
	       (buff+offset), 
	       sizeof(response_footer->adapter_address));
	offset+=sizeof(response_footer->adapter_address);

	if( offset+sizeof(response_footer->version_major) >= buffsize) goto broken_packet;
	response_footer->version_major = *(typeof(response_footer->version_major)*)(buff+offset);
	offset+=sizeof(response_footer->version_major);
	
	if( offset+sizeof(response_footer->version_minor) >= buffsize) goto broken_packet;
	response_footer->version_minor = *(typeof(response_footer->version_minor)*)(buff+offset);
	offset+=sizeof(response_footer->version_minor);
	
	if( offset+sizeof(response_footer->duration) >= buffsize) goto broken_packet;
	response_footer->duration = get16(buff+offset);
	offset+=sizeof(response_footer->duration);
	
	if( offset+sizeof(response_footer->frmps_received) >= buffsize) goto broken_packet;
	response_footer->frmps_received= get16(buff+offset);
	offset+=sizeof(response_footer->frmps_received);
	
	if( offset+sizeof(response_footer->frmps_transmitted) >= buffsize) goto broken_packet;
	response_footer->frmps_transmitted = get16(buff+offset);
	offset+=sizeof(response_footer->frmps_transmitted);
	
	if( offset+sizeof(response_footer->iframe_receive_errors) >= buffsize) goto broken_packet;
	response_footer->iframe_receive_errors = get16(buff+offset);
	offset+=sizeof(response_footer->iframe_receive_errors);
	
	if( offset+sizeof(response_footer->transmit_aborts) >= buffsize) goto broken_packet;
	response_footer->transmit_aborts =  get16(buff+offset);
	offset+=sizeof(response_footer->transmit_aborts);
	
	if( offset+sizeof(response_footer->transmitted) >= buffsize) goto broken_packet;
	response_footer->transmitted = get32(buff+offset);
	offset+=sizeof(response_footer->transmitted);
	
	if( offset+sizeof(response_footer->received) >= buffsize) goto broken_packet;
	response_footer->received = get32(buff+offset);
	offset+=sizeof(response_footer->received);
	
	if( offset+sizeof(response_footer->iframe_transmit_errors) >= buffsize) goto broken_packet;
	response_footer->iframe_transmit_errors = get16(buff+offset);
	offset+=sizeof(response_footer->iframe_transmit_errors);
	
	if( offset+sizeof(response_footer->no_receive_buffer) >= buffsize) goto broken_packet;
	response_footer->no_receive_buffer = get16(buff+offset);
	offset+=sizeof(response_footer->no_receive_buffer);
	
	if( offset+sizeof(response_footer->tl_timeouts) >= buffsize) goto broken_packet;
	response_footer->tl_timeouts = get16(buff+offset);
	offset+=sizeof(response_footer->tl_timeouts);
	
	if( offset+sizeof(response_footer->ti_timeouts) >= buffsize) goto broken_packet;
	response_footer->ti_timeouts = get16(buff+offset);
	offset+=sizeof(response_footer->ti_timeouts);
	
	if( offset+sizeof(response_footer->free_ncbs) >= buffsize) goto broken_packet;
	response_footer->free_ncbs = get16(buff+offset);
	offset+=sizeof(response_footer->free_ncbs);
	
	if( offset+sizeof(response_footer->ncbs) >= buffsize) goto broken_packet;
	response_footer->ncbs = get16(buff+offset);
	offset+=sizeof(response_footer->ncbs);
	
	if( offset+sizeof(response_footer->max_ncbs) >= buffsize) goto broken_packet;
	response_footer->max_ncbs = get16(buff+offset);
	offset+=sizeof(response_footer->max_ncbs);
	
	if( offset+sizeof(response_footer->no_transmit_buffers) >= buffsize) goto broken_packet;
	response_footer->no_transmit_buffers = get16(buff+offset);
	offset+=sizeof(response_footer->no_transmit_buffers);
	
	if( offset+sizeof(response_footer->max_datagram) >= buffsize) goto broken_packet;
	response_footer->max_datagram = get16(buff+offset);
	offset+=sizeof(response_footer->max_datagram);
	
	if( offset+sizeof(response_footer->pending_sessions) >= buffsize) goto broken_packet;
	response_footer->pending_sessions = get16(buff+offset);
	offset+=sizeof(response_footer->pending_sessions);
	
	if( offset+sizeof(response_footer->max_sessions) >= buffsize) goto broken_packet;
	response_footer->max_sessions = get16(buff+offset);
	offset+=sizeof(response_footer->max_sessions);
	
	if( offset+sizeof(response_footer->packet_sessions) >= buffsize) goto broken_packet;
	response_footer->packet_sessions = get16(buff+offset);
	offset+=sizeof(response_footer->packet_sessions);
	
	/* Done with packet footer and the whole packet */
broken_packet:
	return;
};

static const nb_service_t services[] = {
{"__MSBROWSE__", 0x01, 0, "Master Browser"},
{"INet~Services", 0x1C, 0, "IIS"},
{"IS~", 0x00, 1, "IIS"},
{"", 0x00, 1, "Workstation Service"},
{"", 0x01, 1, "Messenger Service"},
{"", 0x03, 1, "Messenger Service"},
{"", 0x06, 1, "RAS Server Service"},
{"", 0x1F, 1, "NetDDE Service"},
{"", 0x20, 1, "File Server Service"},
{"", 0x21, 1, "RAS Client Service"},
{"", 0x22, 1, "Microsoft Exchange Interchange(MSMail Connector)"},
{"", 0x23, 1, "Microsoft Exchange Store"},
{"", 0x24, 1, "Microsoft Exchange Directory"},
{"", 0x30, 1, "Modem Sharing Server Service"},
{"", 0x31, 1, "Modem Sharing Client Service"},
{"", 0x43, 1, "SMS Clients Remote Control"},
{"", 0x44, 1, "SMS Administrators Remote Control Tool"},
{"", 0x45, 1, "SMS Clients Remote Chat"},
{"", 0x46, 1, "SMS Clients Remote Transfer"},
{"", 0x4C, 1, "DEC Pathworks TCPIP service on Windows NT"},
{"", 0x52, 1, "DEC Pathworks TCPIP service on Windows NT"},
{"", 0x87, 1, "Microsoft Exchange MTA"},
{"", 0x6A, 1, "Microsoft Exchange IMC"},
{"", 0xBE, 1, "Network Monitor Agent"},
{"", 0xBF, 1, "Network Monitor Application"},
{"", 0x03, 1, "Messenger Service"},
{"", 0x00, 0, "Domain Name"},
{"", 0x1B, 1, "Domain Master Browser"},
{"", 0x1C, 0, "Domain Controllers"},
{"", 0x1D, 1, "Master Browser"},
{"", 0x1E, 0, "Browser Service Elections"},
{"", 0x2B, 1, "Lotus Notes Server Service"},
{"IRISMULTICAST", 0x2F, 0, "Lotus Notes"},
{"IRISNAMESERVER", 0x33, 0, "Lotus Notes"},
{"Forte_$ND800ZA", 0x20, 1, "DCA IrmaLan Gateway Server Service"}
};

const char *getnbservicename(uint8_t service, int unique, const char *name) {
	int i;
	static char unknown[100];

	for(i=0; i < 35; i++) {
		if(strstr(name, services[i].nb_name) && 
			service == services[i].service_number &&
			unique == services[i].unique)
			printf("blubber name = %s\n",services[i].service_name);
		return services[i].service_name;
	};	

	snprintf(unknown, 100, "Unknown service (code %x)", service);
	return(unknown);
};

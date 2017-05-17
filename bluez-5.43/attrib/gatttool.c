/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2010  Nokia Corporation
 *  Copyright (C) 2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>

#include "lib/bluetooth.h"
#include "lib/hci.h"
#include "lib/hci_lib.h"
#include "lib/sdp.h"
#include "lib/uuid.h"

#include "src/shared/util.h"
#include "att.h"
#include "btio/btio.h"
#include "gattrib.h"
#include "gatt.h"
#include "gatttool.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/un.h>

#define MAX_MAP_SIZE 40



static char *opt_src = NULL;
static char *opt_dst = NULL;
static char *opt_dst_type = NULL;
static char *opt_value = NULL;
static char *opt_sec_level = NULL;
static bt_uuid_t *opt_uuid = NULL;
static int opt_start = 0x0001;
static int opt_end = 0xffff;
static int opt_handle = -1;
static int opt_mtu = 0;
static int opt_psm = 0;
static gboolean opt_primary = FALSE;
static gboolean opt_characteristics = FALSE;
static gboolean opt_char_read = FALSE;
static gboolean opt_listen = FALSE;
static gboolean opt_char_desc = FALSE;
static gboolean opt_char_write = FALSE;
static gboolean opt_char_write_req = FALSE;
static gboolean opt_interactive = FALSE;

//-----------------------for server option-------------------------//
static gboolean opt_server = FALSE;
//----------------------------------------------------------------//

static GMainLoop *event_loop;
static gboolean got_error = FALSE;
static GSourceFunc operation;

//----------------------------my code (variables)---------------------------------//
static gint scanCounter = 20;
static gint centerCounter = 20;

int sockfd_scan;
struct sockaddr_un addr_scan;
int scanfd;

int sockfd_sensor;
struct sockaddr_un addr_sensor;
// sensorfd;

int sockfd_auth;
struct sockaddr_un addr_auth;

int sockfd_upload;
struct sockaddr_un addr_upload;

char buffer[64];
char buffer2[64];
char buffer3[64];
char buffer4[64];

ssize_t bytes_read;

int MapSize = 0;
int MapIndex;
bool connectingFlag = 0;
struct MAP{
	GAttrib *attrib;
	char MAC[18];
};

struct MAP map[MAX_MAP_SIZE];

int selfNum;
int connectCount = 0;
//--------------------------------------my code (variable)------------------------//

struct characteristic_data {
	GAttrib *attrib;
	uint16_t start;
	uint16_t end;
};


static void events_handler(const uint8_t *pdu, uint16_t len, gpointer user_data)
{
	GAttrib *attrib = user_data;
	uint8_t *opdu;
	uint16_t handle, i, olen = 0;
	size_t plen;

	handle = get_le16(&pdu[1]);

	switch (pdu[0]) {
	case ATT_OP_HANDLE_NOTIFY:
		g_print("Notification handle = 0x%04x value: ", handle);
		break;
	case ATT_OP_HANDLE_IND:
		g_print("Indication   handle = 0x%04x value: ", handle);
		break;
	default:
		g_print("Invalid opcode\n");
		return;
	}

	for (i = 3; i < len; i++){
		g_print("%02x ", pdu[i]);
	}

	if (pdu[0] == ATT_OP_HANDLE_NOTIFY)
		return;

	opdu = g_attrib_get_buffer(attrib, &plen);
	olen = enc_confirmation(opdu, plen);

	if (olen > 0)
		g_attrib_send(attrib, 0, opdu, olen, NULL, NULL, NULL);
}



static gboolean listen_start(gpointer user_data)
{
	GAttrib *attrib = user_data;

	g_attrib_register(attrib, ATT_OP_HANDLE_NOTIFY, GATTRIB_ALL_HANDLES,
						events_handler, attrib, NULL);
	g_attrib_register(attrib, ATT_OP_HANDLE_IND, GATTRIB_ALL_HANDLES,
						events_handler, attrib, NULL);

	return FALSE;
}

static void connect_cb(GIOChannel *io, GError *err, gpointer user_data)
{
	GAttrib *attrib;
	uint16_t mtu;
	uint16_t cid;
	GError *gerr = NULL;

	if (err) {
		g_printerr("%s\n", err->message);
		got_error = TRUE;
		g_main_loop_quit(event_loop);
	}

	bt_io_get(io, &gerr, BT_IO_OPT_IMTU, &mtu,
				BT_IO_OPT_CID, &cid, BT_IO_OPT_INVALID);

	if (gerr) {
		g_printerr("Can't detect MTU, using default: %s",
								gerr->message);
		g_error_free(gerr);
		mtu = ATT_DEFAULT_LE_MTU;
	}

	if (cid == ATT_CID)
		mtu = ATT_DEFAULT_LE_MTU;

	attrib = g_attrib_new(io, mtu, false);

	if (opt_listen)
		g_idle_add(listen_start, attrib);

	operation(attrib);
}


static void primary_all_cb(uint8_t status, GSList *services, void *user_data)
{
	GSList *l;

	if (status) {
		g_printerr("Discover all primary services failed: %s\n",
							att_ecode2str(status));
		goto done;
	}

	for (l = services; l; l = l->next) {
		struct gatt_primary *prim = l->data;
		g_print("attr handle = 0x%04x, end grp handle = 0x%04x "
			"uuid: %s\n", prim->range.start, prim->range.end, prim->uuid);
	}

done:
	g_main_loop_quit(event_loop);
}

static void primary_by_uuid_cb(uint8_t status, GSList *ranges, void *user_data)
{
	GSList *l;

	if (status != 0) {
		g_printerr("Discover primary services by UUID failed: %s\n",
att_ecode2str(status));
		goto done;
	}

	for (l = ranges; l; l = l->next) {
		struct att_range *range = l->data;
		g_print("Starting handle: %04x Ending handle: %04x\n",
						range->start, range->end);
	}

done:
	g_main_loop_quit(event_loop);
}

static gboolean primary(gpointer user_data)
{
	GAttrib *attrib = user_data;

	if (opt_uuid)
		gatt_discover_primary(attrib, opt_uuid, primary_by_uuid_cb,
									NULL);
	else
		gatt_discover_primary(attrib, NULL, primary_all_cb, NULL);

	return FALSE;
}

static void char_discovered_cb(uint8_t status, GSList *characteristics,
								void *user_data)
{
	GSList *l;

	if (status) {
		g_printerr("Discover all characteristics failed: %s\n",
							att_ecode2str(status));
		goto done;
	}

	for (l = characteristics; l; l = l->next) {
		struct gatt_char *chars = l->data;

		g_print("handle = 0x%04x, char properties = 0x%02x, char value "
			"handle = 0x%04x, uuid = %s\n", chars->handle,
			chars->properties, chars->value_handle, chars->uuid);
	}

done:
	g_main_loop_quit(event_loop);
}

static gboolean characteristics(gpointer user_data)
{
	GAttrib *attrib = user_data;

	gatt_discover_char(attrib, opt_start, opt_end, opt_uuid,
						char_discovered_cb, NULL);

	return FALSE;
}

static void char_read_cb(guint8 status, const guint8 *pdu, guint16 plen,
							gpointer user_data)
{
	uint8_t value[plen];
	ssize_t vlen;
	int i;

	if (status != 0) {
		g_printerr("Characteristic value/descriptor read failed: %s\n",
							att_ecode2str(status));
		goto done;
	}

	vlen = dec_read_resp(pdu, plen, value, sizeof(value));
	if (vlen < 0) {
		g_printerr("Protocol error\n");
		goto done;
	}
	g_print("Characteristic value/descriptor: ");
	for (i = 0; i < vlen; i++)
		g_print("%02x ", value[i]);
	g_print("\n");

done:
	if (!opt_listen)
		g_main_loop_quit(event_loop);
}

static void char_read_by_uuid_cb(guint8 status, const guint8 *pdu,
					guint16 plen, gpointer user_data)
{
	struct att_data_list *list;
	int i;

	if (status != 0) {
		g_printerr("Read characteristics by UUID failed: %s\n",
							att_ecode2str(status));
		goto done;
	}

	list = dec_read_by_type_resp(pdu, plen);
	if (list == NULL)
		goto done;

	for (i = 0; i < list->num; i++) {
		uint8_t *value = list->data[i];
		int j;

		g_print("handle: 0x%04x \t value: ", get_le16(value));
		value += 2;
		for (j = 0; j < list->len - 2; j++, value++)
			g_print("%02x ", *value);
		g_print("\n");
	}

	att_data_list_free(list);

done:
	g_main_loop_quit(event_loop);
}

static gboolean characteristics_read(gpointer user_data)
{
	GAttrib *attrib = user_data;

	if (opt_uuid != NULL) {

		gatt_read_char_by_uuid(attrib, opt_start, opt_end, opt_uuid,
						char_read_by_uuid_cb, NULL);

		return FALSE;
	}

	if (opt_handle <= 0) {
		g_printerr("A valid handle is required\n");
		g_main_loop_quit(event_loop);
		return FALSE;
	}

	gatt_read_char(attrib, opt_handle, char_read_cb, attrib);

	return FALSE;
}

static void mainloop_quit(gpointer user_data)
{
	uint8_t *value = user_data;

	g_free(value);
	g_main_loop_quit(event_loop);
}

static gboolean characteristics_write(gpointer user_data)
{
	GAttrib *attrib = user_data;
	uint8_t *value;
	size_t len;


	g_print("My code is here, HAHAHA!\n");
	if (opt_handle <= 0) {
		g_printerr("A valid handle is required\n");
		goto error;
	}

	if (opt_value == NULL || opt_value[0] == '\0') {
		g_printerr("A value is required\n");
		goto error;
	}

	len = gatt_attr_data_from_string(opt_value, &value);
	if (len == 0) {
		g_printerr("Invalid value\n");
		goto error;
	}

	gatt_write_cmd(attrib, opt_handle, value, len, mainloop_quit, value);

	g_free(value);
	return FALSE;

error:
	g_main_loop_quit(event_loop);
	return FALSE;
}

static void char_write_req_cb(guint8 status, const guint8 *pdu, guint16 plen,
							gpointer user_data)
{
	if (status != 0) {
		g_printerr("Characteristic Write Request failed: "
						"%s\n", att_ecode2str(status));
		goto done;
	}

	if (!dec_write_resp(pdu, plen) && !dec_exec_write_resp(pdu, plen)) {
		g_printerr("Protocol error\n");
		goto done;
	}

	g_print("Characteristic value was written successfully\n");

done:
	if (!opt_listen)
		g_main_loop_quit(event_loop);
}

static gboolean characteristics_write_req(gpointer user_data)
{

	GAttrib *attrib = user_data;
	uint8_t *value;
	size_t len;

	if (opt_handle <= 0) {
		g_printerr("A valid handle is required\n");
		goto error;
	}

	if (opt_value == NULL || opt_value[0] == '\0') {
		g_printerr("A value is required\n");
		goto error;
	}

	len = gatt_attr_data_from_string(opt_value, &value);
	if (len == 0) {
		g_printerr("Invalid value\n");
		goto error;
	}

	g_print("opt_handle = %d, value = %8u, len = %zu\n",
				opt_handle, *value, len);
	gatt_write_char(attrib, opt_handle, value, len, char_write_req_cb,
									NULL);

	g_free(value);
	return FALSE;

error:
	g_main_loop_quit(event_loop);
	return FALSE;
}

static void char_desc_cb(uint8_t status, GSList *descriptors, void *user_data)
{
	GSList *l;

	if (status) {
		g_printerr("Discover descriptors failed: %s\n",
							att_ecode2str(status));
		return;
	}

	for (l = descriptors; l; l = l->next) {
		struct gatt_desc *desc = l->data;

		g_print("handle = 0x%04x, uuid = %s\n", desc->handle,
								desc->uuid);
	}

	if (!opt_listen)
		g_main_loop_quit(event_loop);
}

static gboolean characteristics_desc(gpointer user_data)
{
	GAttrib *attrib = user_data;

	gatt_discover_desc(attrib, opt_start, opt_end, NULL, char_desc_cb,
									NULL);

	return FALSE;
}

static gboolean parse_uuid(const char *key, const char *value,
				gpointer user_data, GError **error)
{
	if (!value)
		return FALSE;

	opt_uuid = g_try_malloc(sizeof(bt_uuid_t));
	if (opt_uuid == NULL)
		return FALSE;

	if (bt_string_to_uuid(opt_uuid, value) < 0)
		return FALSE;

	return TRUE;
}

static GOptionEntry primary_char_options[] = {
	{ "start", 's' , 0, G_OPTION_ARG_INT, &opt_start,
		"Starting handle(optional)", "0x0001" },
	{ "end", 'e' , 0, G_OPTION_ARG_INT, &opt_end,
		"Ending handle(optional)", "0xffff" },
	{ "uuid", 'u', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK,
		parse_uuid, "UUID16 or UUID128(optional)", "0x1801"},
	{ NULL },
};

static GOptionEntry char_rw_options[] = {
	{ "handle", 'a' , 0, G_OPTION_ARG_INT, &opt_handle,
		"Read/Write characteristic by handle(required)", "0x0001" },
	{ "value", 'n' , 0, G_OPTION_ARG_STRING, &opt_value,
		"Write characteristic value (required for write operation)",
		"0x0001" },
	{NULL},
};

static GOptionEntry gatt_options[] = {
	{ "primary", 0, 0, G_OPTION_ARG_NONE, &opt_primary,
		"Primary Service Discovery", NULL },
	{ "characteristics", 0, 0, G_OPTION_ARG_NONE, &opt_characteristics,
		"Characteristics Discovery", NULL },
	{ "char-read", 0, 0, G_OPTION_ARG_NONE, &opt_char_read,
		"Characteristics Value/Descriptor Read", NULL },
	{ "char-write", 0, 0, G_OPTION_ARG_NONE, &opt_char_write,
		"Characteristics Value Write Without Response (Write Command)",
		NULL },
	{ "char-write-req", 0, 0, G_OPTION_ARG_NONE, &opt_char_write_req,
		"Characteristics Value Write (Write Request)", NULL },
	{ "char-desc", 0, 0, G_OPTION_ARG_NONE, &opt_char_desc,
		"Characteristics Descriptor Discovery", NULL },
	{ "listen", 0, 0, G_OPTION_ARG_NONE, &opt_listen,
		"Listen for notifications and indications", NULL },
	{ "interactive", 'I', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE,
		&opt_interactive, "Use interactive mode", NULL },
	//----------------------------Register Option---------------------------------//
	{ "server", 'S', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE,
	&opt_server, "Use server mode", NULL },
	//---------------------------------------------------------------------------//
	{ NULL },
};

static GOptionEntry options[] = {
	{ "adapter", 'i', 0, G_OPTION_ARG_STRING, &opt_src,
		"Specify local adapter interface", "hciX" },
	{ "device", 'b', 0, G_OPTION_ARG_STRING, &opt_dst,
		"Specify remote Bluetooth address", "MAC" },
	{ "addr-type", 't', 0, G_OPTION_ARG_STRING, &opt_dst_type,
		"Set LE address type. Default: public", "[public | random]"},
	{ "mtu", 'm', 0, G_OPTION_ARG_INT, &opt_mtu,
		"Specify the MTU size", "MTU" },
	{ "psm", 'p', 0, G_OPTION_ARG_INT, &opt_psm,
		"Specify the PSM for GATT/ATT over BR/EDR", "PSM" },
	{ "sec-level", 'l', 0, G_OPTION_ARG_STRING, &opt_sec_level,
		"Set security level. Default: low", "[low | medium | high]"},
	{ NULL },
};

////////////////my code (functions) ////////////////////////////////////////
static void myEvents_handler(const uint8_t *pdu, uint16_t len, gpointer user_data)
{
	//GAttrib *attrib = user_data;
	GAttrib *targetAtt;

	GString *str = user_data;

	uint8_t *opdu;
	uint16_t handle, i, olen = 0;
	size_t plen;

	uint8_t value[32];
	char toSend[32];
	uint16_t myLen = 0;
	char MAC[18];
	char* MACptr = MAC;
	char tmp[2];
	handle = get_le16(&pdu[1]);

	switch (pdu[0]) {
	case ATT_OP_HANDLE_NOTIFY:
		g_print("Notification handle = 0x%04x value: ", handle);
		break;
	case ATT_OP_HANDLE_IND:
		g_print("Indication handle = 0x%04x value: ", handle);
		break;
	default:
		g_print("Invalid opcode\n");
		return;
	}

	for (i = 3; i < len; i++){
		g_print("%02x ", pdu[i]);
		value[i - 3] = pdu[i];
	}
	myLen = len - 3;
	g_print("\n");
	g_print("MAC: %s\n", str->str);
	g_print("Packet Type: %x\n", value[0]);
	switch(value[0]){
		case 0x72: //'r', sensor reply to user
			memset(&toSend, 0, sizeof(toSend));
			for( i = 1; i <= 6; i++ ){
				if(i != 1){
				 strcat(MAC, ":");
				MACptr++;
				}
				MACptr += sprintf(MACptr, "%02x", value[i]); 
			}
			*MACptr = 0;
			g_print("MAC = %s\n", MAC);
			g_print("map size = %d\n", MapSize);
			//for( i = 0; i <= 17; i++ ){
			//	g_print("i th char: %c, %c. strncmp: %d\n"
			//		, MAC[i], map[0].MAC[i]
			//		, strncmp(MAC, map[0].MAC, 17));
			//}
			int j;
			for( j = 0; j < MapSize; j++){
				g_print("map[%d]: %s\n", j, map[j].MAC);
				g_print("%d\n", strncmp(MAC, map[j].MAC, 17));
				if(strncmp(MAC, map[j].MAC, 17) == 0) break;
			}
			if(j != MapSize){
				targetAtt = map[j].attrib;
				g_print("get attrib\n");
				strcpy(toSend, value + 7);	
				gatt_write_char(targetAtt, 0x002d, toSend, strlen(toSend), char_write_req_cb, NULL);
			}
			break;
		case 0x75: //'u', sensor upload data
			memset(&toSend, 0, sizeof(toSend));
			//client, socket 4
  			sockfd_upload = socket(AF_UNIX, SOCK_STREAM, 0);
			memset(&addr_upload, 0, sizeof(addr_upload));
			addr_upload.sun_family = AF_UNIX;
			strcpy(addr_upload.sun_path, "/tmp/upload.socket");

			if (connect(sockfd_upload, (struct sockaddr*)&addr_upload, sizeof(addr_upload)) == -1) {
	    	perror("upload center connect error");
	    	exit(-1);
	  		}
			g_print("Up load data\n");
			strcpy(toSend, str->str);
			strcat(toSend, value + 1);
			send(sockfd_upload, toSend, strlen(toSend), 0);

			close(sockfd_upload);
			g_print("Upload succeed\n");
			break;

		case 0x64: //'d', user send to sensor
			strcpy(MAC, str->str);
			memset(&toSend, 0, sizeof(toSend));
			memset(&tmp, 0, sizeof(tmp));
			toSend[0] = value[0];
			for( i = 1; i <= 6; i++){
				strncpy(tmp, MAC + (i - 1) * 3, 2);
				//toSend[i] = atoi(tmp, 16);
				sscanf(tmp, "%02x", toSend + i);
				g_print("%u\n", toSend[i]);
			}
			for ( i = 1; i < myLen; i++)
				toSend[6 + i] = value[i];
			g_print("toSend = %s\n", toSend);
			send(sockfd_sensor, toSend, strlen(toSend), MSG_DONTWAIT);
			break;

		case 0x70: //'p', user send password
			memset(&toSend, 0, sizeof(toSend));
			//client, socket 3
	  		sockfd_auth = socket(AF_UNIX, SOCK_STREAM, 0);
			memset(&addr_auth, 0, sizeof(addr_auth));
			addr_auth.sun_family = AF_UNIX;
			strcpy(addr_auth.sun_path, "/tmp/auth.socket");

			if (connect(sockfd_auth, (struct sockaddr*)&addr_auth, sizeof(addr_auth)) == -1) {
	    	perror("auth center connect error");
	    	exit(-1);
	  		}

	  		strcpy(toSend, str->str);
	  		toSend[17] = '@';
			for ( i = 1; i < myLen; i++)
				toSend[17 + i] = value[i];
			send(sockfd_auth, toSend, strlen(toSend), MSG_DONTWAIT);
			//send(sockfd_auth, value + 1, myLen, MSG_DONTWAIT);
			break;

		case 0x6c: //'l', user asks for list
			memset(&toSend, 0, sizeof(toSend));
			toSend[0] = 'l';
			toSend[1] = 0;
			strcat(toSend, str->str);
			send(sockfd_sensor, toSend, strlen(toSend), MSG_DONTWAIT);
			break;

		case 0x6e: //'n', new sensor
			memset(&toSend, 0, sizeof(toSend));
			toSend[0] = 'n';
			toSend[1] = 0;
			strcat(toSend, str->str);
			toSend[18] = value[1];
			toSend[19] = value[2];
			toSend[20] = 0;
			//Tell sensor center there is a new child. n MAC sensor_type service_type
			send(sockfd_sensor, toSend, strlen(toSend), MSG_DONTWAIT);
			break;
		case 0x74: //'t', packet in tree network
			//Transmit to Sensor Center without change
			send(sockfd_sensor, value, strlen(value), MSG_DONTWAIT);
			break;			

		default:
			g_print("Unknown packet type: \n");
	}
	if (pdu[0] == ATT_OP_HANDLE_NOTIFY)
		return;

	for (i = 0; i < MapIndex; i++) {
		if(strcmp(map[i].MAC, str->str) == 0)
			break;
	}
	opdu = g_attrib_get_buffer(map[i].attrib, &plen);
	olen = enc_confirmation(opdu, plen);

	if (olen > 0)
		g_attrib_send(map[i].attrib, 0, opdu, olen, NULL, NULL, NULL);
}

static void myConnect_cb(GIOChannel *io, GError *err, gpointer user_data)
{
	GAttrib *attrib;
	GString *str;
	uint16_t mtu;
	uint16_t cid;
	size_t len;
	char sendBuffer[32];
	GError *gerr = NULL;
	uint8_t *value = malloc(sizeof(uint8_t));	

	g_print("connected\n");
	if (err) {
		g_printerr("%s\n", err->message);
		got_error = TRUE;
		g_main_loop_quit(event_loop);
	}
	bt_io_get(io, &gerr, BT_IO_OPT_IMTU, &mtu,
				BT_IO_OPT_CID, &cid, BT_IO_OPT_INVALID);
	if (gerr) {
		g_printerr("Can't detect MTU, using default: %s",
								gerr->message);
		g_error_free(gerr);
		mtu = ATT_DEFAULT_LE_MTU;
	}
	if (cid == ATT_CID)
		mtu = ATT_DEFAULT_LE_MTU;

	attrib = g_attrib_new(io, mtu, false);
	*value = 3; 
	len = 2;

	map[MapIndex].attrib = attrib;

	str = g_string_new(map[MapIndex].MAC);

	//g_attrib_register(map[MapIndex].attrib, ATT_OP_HANDLE_NOTIFY, GATTRIB_ALL_HANDLES,
	//					myEvents_handler, str, NULL);
	g_attrib_register(map[MapIndex].attrib, ATT_OP_HANDLE_IND, GATTRIB_ALL_HANDLES,
						myEvents_handler, str, NULL);
	
	if(map[MapIndex].MAC[0] == '9'){
		gatt_write_char(map[MapIndex].attrib, 0x000e, value, len, char_write_req_cb, NULL);
		sendBuffer[0] = 's';
		sendBuffer[1] = 0;
		strcat(sendBuffer, map[MapIndex].MAC);
		send(sockfd_sensor, sendBuffer, sizeof(sendBuffer), MSG_DONTWAIT);
	}
	else{
		//sendBuffer[0] = 'n';
		//sendBuffer[1] = 0;
		//strcat(sendBuffer, map[MapIndex].MAC);
		//send(sockfd_auth, sendBuffer, sizeof(sendBuffer), MSG_DONTWAIT);

		//strcat(sendBuffer, "Enter Password")
		g_attrib_register(map[MapIndex].attrib, ATT_OP_HANDLE_NOTIFY, GATTRIB_ALL_HANDLES,
						myEvents_handler, str, NULL);
		if(map[MapIndex].MAC[0] == 'b' || map[MapIndex].MAC[0] == '0' || map[MapIndex].MAC[0] == '5'){
			connectCount++; //Maintain the connectCount
			sendBuffer[0] = '0';
			sprintf(sendBuffer+1, "%d", connectCount);
			//Send to Sensor Center "0 connectCount" to maintain the connectCount in Sensor Center
			send(sockfd_sensor, sendBuffer, sizeof(sendBuffer), MSG_DONTWAIT);

			sendBuffer[0] = 'n';
			sprintf(sendBuffer+1,"%d", selfNum);
			strcat(sendBuffer, "@");
			char c[2];
			sprintf(c, "%d", connectCount);
			strcat(sendBuffer, c);
			//Tell the child "n selfNum @ connectCounnt"
			gatt_write_char(map[MapIndex].attrib, 0x000c, sendBuffer, strlen(sendBuffer), char_write_req_cb, NULL);	 
		}
		gatt_write_char(map[MapIndex].attrib, 0x000d, value, len, char_write_req_cb, NULL);
		sendBuffer[0] = 'p';
		sendBuffer[1] = 0;
		gatt_write_char(map[MapIndex].attrib, 0x000c, sendBuffer, strlen(sendBuffer), char_write_req_cb, NULL);

	}
	if(MapIndex == MapSize)
		MapSize++;
	g_free(value);
	connectingFlag  = 0;
}

//------------------------------Receive message from scan center----------------------------//
static gboolean checkScan(gpointer arg)
{
    //g_print(".");
    if(--scanCounter == 0){
        //g_print("\b\b\b\b\b\b\b\b\b");
        scanCounter = 5;
        //退出循环
        //注销定时器
        bytes_read = recv(sockfd_scan, buffer, sizeof(buffer), MSG_DONTWAIT);

		//Receive MAC

        if(bytes_read > 0 && connectingFlag == 0){
			connectingFlag = 1;
        	GError *gerr = NULL;
        	GIOChannel *chan;
        	g_print("Bytes received: %zd\n", bytes_read);
        	g_print("%s\n", buffer);
	        	if(buffer[0] == '9' || buffer[0] == 'b' || buffer[0] == '5' || buffer[0] == '0')
	        	{
					//Connect to MAC
					g_print("connect to public device\n");
	        		chan = gatt_connect("hci0", buffer, "public", "low",
							0, 0, myConnect_cb, &gerr);
	        	}
        		else{        		
					g_print("connect to random device\n");
					chan = gatt_connect("hci0", buffer, "random", "low",
						0, 0, myConnect_cb, &gerr);
				}
				if (chan == NULL) {
					g_printerr("%s\n", gerr->message);
					g_clear_error(&gerr);
					
				}
				else{
					int i;
					for( i = 0; i < MapSize; i++){
						if(strcmp(map[i].MAC, buffer) == 0)
								break;
					}
					MapIndex = i;
					g_print("map index = %d\n", MapIndex); 
					if( i == MapSize)
						strcpy(map[MapSize].MAC, buffer);
				}  		
		}
        return TRUE;
    }
    //定时器继续运行
    return TRUE;
}
//------------------------------------------------------------------------//

//--------------------------------Receive message from sensor center---------------------------//
static gboolean checkCenters(gpointer arg)
{
    //g_print(".");
    if(--centerCounter == 0){
        //g_print("\b\b\b\b\b\b\b\b\b");
        centerCounter = 1;
        //退出循环
        //注销定时器
        bytes_read = recv(sockfd_sensor, buffer2, sizeof(buffer2), MSG_DONTWAIT);
        if(bytes_read > 0){
        	g_print("Received message from sockfd_sensor: %s\n", buffer2);
        	if(buffer2[0] == 'd'){					// d AA:BB:CC:DD:EE:FF payload
        		GAttrib *targetAtt;
        		char mac[18];
        		char payload[20];
        		char packet[32];
        		strncpy(mac, buffer2 + 1, 17);
        		mac[17] = 0;
        		g_print("Sensor MAC: %s\n", mac);
        		strcpy(payload, buffer2 + 18);
        		g_print("Finding...\n");
        		int i;
        		for(i = 0; i < MapSize; i++){
        			if(strcmp(map[i].MAC, mac) == 0)
        			{
        				g_print("Found\n");
        				targetAtt = map[i].attrib;
        				strcpy(packet, "d");
        				strcat(packet, payload);
        				gatt_write_char(targetAtt, 0x000b, packet, strlen(packet), char_write_req_cb, NULL);
        				break;
        			}
        		}
        		
        	}
			else if(buffer2[0] == 't'){					// t AA:BB:CC:DD:EE:FF payload
        		GAttrib *targetAtt;
        		char mac[18];
        		char payload[20];
        		char packet[32];
				// get the MAC
        		strncpy(mac, buffer2 + 1, 17);
        		mac[17] = 0;
        		g_print("Sensor MAC: %s\n", mac);

				//get the payload
        		strcpy(payload, buffer2 + 18);
        		g_print("Finding...\n");
        		int i;
        		for(i = 0; i < MapSize; i++){

					//Find Connection by MAC
        			if(strcmp(map[i].MAC, mac) == 0)
        			{
        				g_print("Found\n");
        				targetAtt = map[i].attrib;
        				strcpy(packet, "t");
        				strcat(packet, payload);
						//Send t payload to child
        				gatt_write_char(targetAtt, 0x000c, packet, strlen(packet), char_write_req_cb, NULL);
        				break;
        			}
        		}
        		
        	}
        	else if(buffer2[0] == 'l'){		// l AA:BB:CC:DD:EE:FF payload
        		GAttrib *targetAtt;
        		char mac[18];
        		char packet[32];

        		packet[0] = 'l';
        		packet[1] = 0;

        		strncpy(mac, buffer2 + 1, 17);
        		mac[17] = 0;

        		strcat(packet, mac);
        		strcat(packet, buffer2 + 18);
        		int i;
        		for(i = 0; i < MapSize; i++){
        			if(strcmp(map[i].MAC, mac) == 0)
        			{
        				g_print("Found\n");
        				targetAtt = map[i].attrib;
        				gatt_write_char(targetAtt, 0x002d, packet, strlen(packet), char_write_req_cb, NULL);
        				break;
        			}
        		}
        	}
			else if(buffer2[0] == '0'){ //Maintain the selfNum to connect to others
				selfNum = atoi(buffer2 + 1);
			}
    	}
        return TRUE;
    }
    //定时器继续运行
    return TRUE;
}
//-----------------------------------------------------------------//

//----------------------------------no use now--------------------------------------//
static gboolean checkAuth(gpointer arg){
	if(--centerCounter == 0){
        //g_print("\b\b\b\b\b\b\b\b\b");
        centerCounter = 5;
        //退出循环
        //注销定时器
        bytes_read = recv(sockfd_auth, buffer3, sizeof(buffer3), MSG_DONTWAIT);
        if(bytes_read > 0){
        	g_print("Received message from sockfd_auth: %s\n", buffer3);

        	if(buffer3[0] == 'S'){
        		GAttrib *targetAtt;
        		char mac[18];
        		char packet[5];

        		strcpy(packet, "ok");
        		packet[2] = 0;
        		g_print("Ready to send ok\n");
        		strncpy(mac, buffer3 + 1, 17);
        		mac[17] = 0;

        		int i;
        		for(i = 0; i < MapSize; i++){
        			if(strcmp(map[i].MAC, mac) == 0)
        			{
        				g_print("Send\n");
        				targetAtt = map[i].attrib;
        				gatt_write_char(targetAtt, 0x002d, packet, strlen(packet), char_write_req_cb, NULL);
        				break;
        			}
        		}
        	}


        close(sockfd_auth);
    	}
        return TRUE;
    }
    //定时器继续运行
    return TRUE;
}
//------------------------------------------------------------------//
//////////////////my code (functions)/////////////////////////

int main(int argc, char *argv[])
{
	GOptionContext *context;
	GOptionGroup *gatt_group, *params_group, *char_rw_group;
	GError *gerr = NULL;
	GIOChannel *chan;
	opt_dst_type = g_strdup("random");
	opt_sec_level = g_strdup("low");

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, options, NULL);

	/* GATT commands */
	gatt_group = g_option_group_new("gatt", "GATT commands",
					"Show all GATT commands", NULL, NULL);
	g_option_context_add_group(context, gatt_group);
	g_option_group_add_entries(gatt_group, gatt_options);

	/* Primary Services and Characteristics arguments */
	params_group = g_option_group_new("params",
			"Primary Services/Characteristics arguments",
			"Show all Primary Services/Characteristics arguments",
			NULL, NULL);
	g_option_context_add_group(context, params_group);
	g_option_group_add_entries(params_group, primary_char_options);

	/* Characteristics value/descriptor read/write arguments */
	char_rw_group = g_option_group_new("char-read-write",
		"Characteristics Value/Descriptor Read/Write arguments",
		"Show all Characteristics Value/Descriptor Read/Write "
		"arguments",
		NULL, NULL);
	g_option_context_add_group(context, char_rw_group);
	g_option_group_add_entries(char_rw_group, char_rw_options);

	if (!g_option_context_parse(context, &argc, &argv, &gerr)) {
		g_printerr("%s\n", gerr->message);
		g_clear_error(&gerr);
	}

	if (opt_interactive) {
		interactive(opt_src, opt_dst, opt_dst_type, opt_psm);
		goto done;
	}

	////////////////my code
	if (opt_server)
		goto server;
	///////////////my code
	
	if (opt_primary)
		operation = primary;
	else if (opt_characteristics)
		operation = characteristics;
	else if (opt_char_read)
		operation = characteristics_read;
	else if (opt_char_write)
		operation = characteristics_write;
	else if (opt_char_write_req)
		operation = characteristics_write_req;
	else if (opt_char_desc)
		operation = characteristics_desc;
	
	else {
		char *help = g_option_context_get_help(context, TRUE, NULL);
		g_print("%s\n", help);
		g_free(help);
		got_error = TRUE;
		goto done;
	}

	if (opt_dst == NULL) {
		g_print("Remote Bluetooth address required\n");
		got_error = TRUE;
		goto done;
	}
	
	chan = gatt_connect(opt_src, opt_dst, opt_dst_type, opt_sec_level,
						opt_psm, opt_mtu, connect_cb, &gerr);
	if (chan == NULL) {
			g_printerr("%s\n", gerr->message);
			g_clear_error(&gerr);
			got_error = TRUE;
			goto done;
	}
	
	/*
	//chan = gatt_connect("hci1", "98:4F:EE:0F:4C:9E", opt_dst_type, opt_sec_level,
	//				opt_psm, opt_mtu, myConnect_cb, &gerr);
	//g_print("src = %s, dst = %s, dst_type = %s, psm = %d, mtu = %d, sec_level = %s\n",
	//		opt_src, "98:4F:EE:0F:4C:9E", opt_dst_type,
	//		opt_psm, opt_mtu, opt_sec_level);
	if (chan == NULL) {
		g_printerr("%s\n", gerr->message);
		g_clear_error(&gerr);
		got_error = TRUE;
		goto done;
	}
	*/

	///////////////////my code
server:
	if (opt_server){
		opt_listen = 1;

		//server, socket 1, for scan
		sockfd_scan = socket(AF_UNIX, SOCK_STREAM, 0);
		memset(&addr_scan, 0, sizeof(addr_scan));
		addr_scan.sun_family = AF_UNIX;
		strcpy(addr_scan.sun_path, "/tmp/scan.socket");
		
		//bind(sockfd_scan, (struct sockaddr*)&addr_scan, sizeof(addr_scan));
		if (connect(sockfd_scan, (struct sockaddr*)&addr_scan, sizeof(addr_scan)) == -1) {
    	perror("scan center connect error");
    	exit(-1);
  		}


		/* make it listen to socket with max 1 connections */
		//listen(sockfd_scan, 2);
		
		//scanfd = accept(sockfd_scan, NULL, NULL);

		//client, socket 2, for sensor center
		sockfd_sensor = socket(AF_UNIX, SOCK_STREAM, 0);
		memset(&addr_sensor, 0, sizeof(addr_sensor));
		addr_sensor.sun_family = AF_UNIX;
		strcpy(addr_sensor.sun_path, "/tmp/sensor.socket");

		if (connect(sockfd_sensor, (struct sockaddr*)&addr_sensor, sizeof(addr_sensor)) == -1) {
    	perror("sensor center connect error");
    	exit(-1);
  		}

  		

		//if (connect(sockfd_auth, (struct sockaddr*)&addr_auth, sizeof(addr_auth)) == -1) {
    	//perror("auth center connect error");
    	//exit(-1);
  		//}

  		


		g_timeout_add(10,checkScan,NULL);
		g_timeout_add(5,checkCenters,NULL);
		g_timeout_add(100,checkAuth,NULL);
	}
	/////////////////////////my code

	event_loop = g_main_loop_new(NULL, FALSE);
		g_main_loop_run(event_loop);
		g_main_loop_unref(event_loop);

done:
	g_option_context_free(context);
	g_free(opt_src);
	g_free(opt_dst);
	g_free(opt_uuid);
	g_free(opt_sec_level);

	if (got_error)
		exit(EXIT_FAILURE);
	else
		exit(EXIT_SUCCESS);
}

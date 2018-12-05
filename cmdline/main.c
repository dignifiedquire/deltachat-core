/* This is a CLI program and a little testing frame.  This file must not be
included when using Delta Chat Core as a library.

Usage:  messenger-backend <databasefile>
(for "Code::Blocks, use Project / Set programs' arguments")
all further options can be set using the set-command (type ? for help). */


#include <string.h>
#include <unistd.h>
#include "../src/deltachat.h"
#include "../src/dc_context.h"
#include "cmdline.h"
#include "stress.h"
#include "../../pgp/libpgp.h"

/*******************************************************************************
 * Event Handler
 ******************************************************************************/


static int s_do_log_info = 1;
#define ANSI_RED    "\e[31m"
#define ANSI_YELLOW "\e[33m"
#define ANSI_NORMAL "\e[0m"


static uintptr_t receive_event(dc_context_t* context, int event, uintptr_t data1, uintptr_t data2)
{
	switch (event)
	{
		case DC_EVENT_GET_STRING:
			break; /* do not show the event as this would fill the screen */

		case DC_EVENT_INFO:
			if (s_do_log_info) {
				printf("%s\n", (char*)data2);
			}
			break;

		case DC_EVENT_SMTP_CONNECTED:
			printf("[DC_EVENT_SMTP_CONNECTED] %s\n", (char*)data2);
			break;

		case DC_EVENT_IMAP_CONNECTED:
			printf("[DC_EVENT_IMAP_CONNECTED] %s\n", (char*)data2);
			break;

		case DC_EVENT_SMTP_MESSAGE_SENT:
			printf("[DC_EVENT_SMTP_MESSAGE_SENT] %s\n", (char*)data2);
			break;

		case DC_EVENT_WARNING:
			printf("[Warning] %s\n", (char*)data2);
			break;

		case DC_EVENT_ERROR:
			printf(ANSI_RED "[DC_EVENT_ERROR] %s" ANSI_NORMAL "\n", (char*)data2);
			break;

		case DC_EVENT_ERROR_NETWORK:
			printf(ANSI_RED "[DC_EVENT_ERROR_NETWORK] first=%i, msg=%s" ANSI_NORMAL "\n", (int)data1, (char*)data2);
			break;

		case DC_EVENT_ERROR_SELF_NOT_IN_GROUP:
			printf(ANSI_RED "[DC_EVENT_ERROR_SELF_NOT_IN_GROUP] %s" ANSI_NORMAL "\n", (char*)data2);
			break;

		case DC_EVENT_HTTP_GET:
			{
				char* ret = NULL;
				char* tempFile = dc_get_fine_pathNfilename(context, context->blobdir, "curl.result");
				char* cmd = dc_mprintf("curl --silent --location --fail --insecure %s > %s", (char*)data1, tempFile); /* --location = follow redirects */
				int error = system(cmd);
				if (error == 0) { /* -1=system() error, !0=curl errors forced by -f, 0=curl success */
					size_t bytes = 0;
					dc_read_file(context, tempFile, (void**)&ret, &bytes);
				}
				free(cmd);
				free(tempFile);
				return (uintptr_t)ret;
			}

		case DC_EVENT_IS_OFFLINE:
			printf(ANSI_YELLOW "{{Received DC_EVENT_IS_OFFLINE()}}\n" ANSI_NORMAL);
			break;

		case DC_EVENT_MSGS_CHANGED:
			printf(ANSI_YELLOW "{{Received DC_EVENT_MSGS_CHANGED(%i, %i)}}\n" ANSI_NORMAL, (int)data1, (int)data2);
			break;

		case DC_EVENT_CONTACTS_CHANGED:
			printf(ANSI_YELLOW "{{Received DC_EVENT_CONTACTS_CHANGED()}}\n" ANSI_NORMAL);
			break;

		case DC_EVENT_CONFIGURE_PROGRESS:
			printf(ANSI_YELLOW "{{Received DC_EVENT_CONFIGURE_PROGRESS(%i ‰)}}\n" ANSI_NORMAL, (int)data1);
			break;

		case DC_EVENT_IMEX_PROGRESS:
			printf(ANSI_YELLOW "{{Received DC_EVENT_IMEX_PROGRESS(%i ‰)}}\n" ANSI_NORMAL, (int)data1);
			break;

		case DC_EVENT_IMEX_FILE_WRITTEN:
			printf(ANSI_YELLOW "{{Received DC_EVENT_IMEX_FILE_WRITTEN(%s)}}\n" ANSI_NORMAL, (char*)data1);
			break;

		case DC_EVENT_FILE_COPIED:
			printf(ANSI_YELLOW "{{Received DC_EVENT_FILE_COPIED(%s)}}\n" ANSI_NORMAL, (char*)data1);
			break;

		case DC_EVENT_CHAT_MODIFIED:
			printf(ANSI_YELLOW "{{Received DC_EVENT_CHAT_MODIFIED(%i)}}\n" ANSI_NORMAL, (int)data1);
			break;

		default:
			printf(ANSI_YELLOW "{{Received DC_EVENT_%i(%i, %i)}}\n" ANSI_NORMAL, (int)event, (int)data1, (int)data2);
			break;
	}
	return 0;
}


/*******************************************************************************
 * Threads for waiting for messages and for jobs
 ******************************************************************************/


static pthread_t imap_thread = 0;
static int       run_threads = 0;
static void* imap_thread_entry_point (void* entry_arg)
{
	dc_context_t* context = (dc_context_t*)entry_arg;

	while (run_threads) {
		// jobs(), fetch() and idle()
		// MUST be called from the same single thread and MUST be called sequentially.
		dc_perform_imap_jobs(context);
		dc_perform_imap_fetch(context);
		dc_perform_imap_idle(context); // this may take hours ...
	}

	imap_thread = 0;
	return NULL;
}


static pthread_t smtp_thread = 0;
static void* smtp_thread_entry_point (void* entry_arg)
{
	dc_context_t* context = (dc_context_t*)entry_arg;

	while (run_threads) {
		dc_perform_smtp_jobs(context);
		dc_perform_smtp_idle(context); // this may take hours ...
	}

	smtp_thread = 0;
	return NULL;
}


static void start_threads(dc_context_t* context)
{
	run_threads = 1;
	if (!imap_thread) {
		pthread_create(&imap_thread, NULL, imap_thread_entry_point, context);
	}

	if (!smtp_thread) {
		pthread_create(&smtp_thread, NULL, smtp_thread_entry_point, context);
	}
}


static void stop_threads(dc_context_t* context)
{
	run_threads = 0;
	dc_interrupt_imap_idle(context);
	dc_interrupt_smtp_idle(context);

	// wait until the threads are finished
	while (imap_thread || smtp_thread) {
		usleep(100*1000);
	}
}


/*******************************************************************************
 * The main loop
 ******************************************************************************/


static char* read_cmd(void)
{
	printf("> ");
	static char cmdbuffer[1024];
	fgets(cmdbuffer, 1000, stdin);

	while (strlen(cmdbuffer)>0
	 && (cmdbuffer[strlen(cmdbuffer)-1]=='\n' || cmdbuffer[strlen(cmdbuffer)-1]==' '))
	{
		cmdbuffer[strlen(cmdbuffer)-1] = '\0';
	}

	return cmdbuffer;
}


int main(int argc, char ** argv)
{
	char*         cmd = NULL;
	dc_context_t* context = dc_context_new(receive_event, NULL, "CLI");

	dc_cmdline_skip_auth(context); /* disable the need to enter the command `auth <password>` for all mailboxes. */

	/* open database from the commandline (if omitted, it can be opened using the `open`-command) */
	if (argc == 2) {
		if (!dc_open(context, argv[1], NULL)) {
			printf("ERROR: Cannot open %s.\n", argv[1]);
		}
	}
	else if (argc != 1) {
		printf("ERROR: Bad arguments\n");
	}

	s_do_log_info = 0;
	/*stress_functions(context);*/
	s_do_log_info = 1;

        /* dancing with the stars */

        PublicOrSecret* key = rpgp_key_from_armor("-----BEGIN PGP PUBLIC KEY BLOCK-----\n\nmDMEWkN+5BYJKwYBBAHaRw8BAQdAIGqj23Kp273IPkgjwA7ue5MDIRAfWLYRqnFy\nc2AFMcC0EUxpZ2h0IDxsaWdodEBzdW4+iJAEExYIADgWIQSGS0GuVELT3Rs0woce\nzfAmwCRYMAUCWkN+5AIbAwULCQgHAgYVCAkKCwIEFgIDAQIeAQIXgAAKCRAezfAm\nwCRYMLteAQCFZcl8kBxCH86wmqpc2+KtEA8l/hsfh7jd+JWuyFuuRAD7BOix8Vo1\nP/hv8qUYwSn3IRXPeGXucoWVoKGgxRd+zAO4OARaQ37kEgorBgEEAZdVAQUBAQdA\nL1KkHCFxtK1CgvZlInT/y6OQeCfXiYzd/i452t2ZR2ADAQgHiHgEGBYIACAWIQSG\nS0GuVELT3Rs0wocezfAmwCRYMAUCWkN+5AIbDAAKCRAezfAmwCRYMJ71AQDmoQTg\n36pfjrl82srS6XPRJxl3r/6lpWGaNij0VptB2wEA2V10ifOhnwILCw1qBle6On7a\nBa257lrFM+cOSMaEsgo=\n=D8HS\n-----END PGP PUBLIC KEY BLOCK-----\n", 620);

        char* key_id = rpgp_key_id(key);
        printf("Key ID: %s\n", key_id);

        /* cleanup */

        rpgp_key_drop(key);
        rpgp_free_string(key_id);

        /* --- */


	printf("Delta Chat Core is awaiting your commands.\n");

	/* wait for command */
	while (1)
	{
		/* read command */
		const char* cmdline = read_cmd();
		free(cmd);
		cmd = dc_strdup(cmdline);
		char* arg1 = strchr(cmd, ' ');
		if (arg1) { *arg1 = 0; arg1++; }

		if (strcmp(cmd, "connect")==0)
		{
			start_threads(context);
		}
		else if (strcmp(cmd, "disconnect")==0)
		{
			stop_threads(context);
		}
		else if (strcmp(cmd, "smtp-jobs")==0)
		{
			if (run_threads) {
				printf("smtp-jobs are already running in a thread.\n");
			}
			else {
				dc_perform_smtp_jobs(context);
			}
		}
		else if (strcmp(cmd, "imap-jobs")==0)
		{
			if (run_threads) {
				printf("imap-jobs are already running in a thread.\n");
			}
			else {
				dc_perform_imap_jobs(context);
			}
		}
		else if (strcmp(cmd, "configure")==0)
		{
			start_threads(context);
			dc_configure(context);
		}
		else if (strcmp(cmd, "clear")==0)
		{
			printf("\n\n\n\n"); /* insert some blank lines to visualize the break in the buffer */
			printf("\e[1;1H\e[2J"); /* should work on ANSI terminals and on Windows 10. If not, well, then not. */
		}
		else if (strcmp(cmd, "getqr")==0 || strcmp(cmd, "getbadqr")==0)
		{
			start_threads(context);
			char* qrstr  = dc_get_securejoin_qr(context, arg1? atoi(arg1) : 0);
			if (qrstr && qrstr[0]) {
				if (strcmp(cmd, "getbadqr")==0 && strlen(qrstr)>40) {
					for (int i = 12; i < 22; i++) { qrstr[i] = '0'; }
				}
				printf("%s\n", qrstr);
				char* syscmd = dc_mprintf("qrencode -t ansiutf8 \"%s\" -o -", qrstr); /* `-t ansiutf8`=use back/write, `-t utf8`=use terminal colors */
				system(syscmd);
				free(syscmd);
			}
			free(qrstr);
		}
		else if (strcmp(cmd, "joinqr")==0)
		{
			start_threads(context);
			if (arg1) {
				dc_join_securejoin(context, arg1);
			}
		}
		else if (strcmp(cmd, "exit")==0)
		{
			break;
		}
		else if (cmd[0] == 0)
		{
			; /* nothing typed */
		}
		else
		{
			char* execute_result = dc_cmdline(context, cmdline);
			if (execute_result) {
				printf("%s\n", execute_result);
				free(execute_result);
			}
		}
	}

	free(cmd);
	stop_threads(context);
	dc_close(context);
	dc_context_unref(context);
	context = NULL;
	return 0;
}


/*
	Amir Tannouri | 2026 | Labor Rechnersysteme DHBW Mannheim
	
	Das Programm empfaengt per TRDP Process Data (PD) die Temperaturwerte
	der vier Drehgestelle auf den ComIDs 1100 bis 1103 (auf Loopback 127.0.0.1).
	Die Drehgestellnummer ergibt sich aus der ComID (1100 -> 1 ... 1103 -> 4),
	die Temperatur steht als 1 Byte in °C in den Nutzdaten.

	- Es wird eine TRDP-Session geoeffnet und auf dieser vier Subscriber angelegt
	- Der Main-Thread (main-funktion) ruft zyklisch pd_comm_process() auf und verteilt damit alle empfangenen Frames auf die Subscriber
	- Pro ComID laeuft ein eigener Thread, der den jeweiligen Wert ausliest, mit dem Grenzwert vergleicht und loggt
	- Ein Mutex regelt alle Zugriffe auf die gemeinsame Session

	- Grenzwert: 100 °C, ueberschritten gilt ab > 100 °C.
	- Beim Uebergang von "OK" zu "ueberschritten" wird einmalig eine TRDP Message Notification (MN) mit ComID 1200 gesendet
	- Faellt der Wert wieder auf <= 100 °C, wird der Zustand zurueckgesetzt

	MN-Nutzdaten (12 Byte):
	Byte 0		Drehgestellnummer
	Byte 1		Temperatur in °C
	Byte 2		Grenzwert in °C
	Byte 3		res
	Byte 4-7	Zeitpunkt, Unix-Timestamp
	Byte 8-11	Millisekunden
*/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "pd_communication.h"
#include "vos_thread.h"
#include "vos_utils.h"

#define NUM_COMIDS    4u
#define FIRST_COMID   1100u
#define LOOPBACK_IP   "127.0.0.1"
#define TEMP_LIMIT    100u
#define MN_COMID      1200u
#define MN_DEST_IP    LOOPBACK_IP
#define MN_DATA_SIZE  12u

typedef struct
{
	UINT32         comid;
	UINT32         bogie;
	TRDP_SUB_T     subscriber;
	BOOL8          subscribed;
	BOOL8          overheated;
	volatile BOOL8 finished;
} ThreadArgs;

static volatile sig_atomic_t keep_running = 1;
static pthread_mutex_t       pd_lock = PTHREAD_MUTEX_INITIALIZER;
static PD_COMM_T             communication;
static TRDP_IP_ADDR_T        mn_destination_ip;

static void stop (int signal_number)
{
	(void) signal_number;
	keep_running = 0;
}

static void format_timestamp (CHAR8 *buffer, size_t length, const struct timespec *ts)
{
	struct tm local_time;
	size_t    used;

	localtime_r(&ts->tv_sec, &local_time);
	used = strftime(buffer, length, "%Y-%m-%d %H:%M:%S", &local_time);
	snprintf(buffer + used, length - used, ".%03ld", ts->tv_nsec / 1000000L);
}

/// Sends TRDP MN as described in header comment
static TRDP_ERR_T send_overheat_mn (const ThreadArgs *args, UINT8 temperature, const struct timespec *ts)
{
	UINT8      data[MN_DATA_SIZE];
	UINT32     seconds      = htonl((UINT32) ts->tv_sec);
	UINT32     milliseconds = htonl((UINT32) (ts->tv_nsec / 1000000L));
	TRDP_ERR_T error;

	data[0] = (UINT8) args->bogie;
	data[1] = temperature;
	data[2] = (UINT8) TEMP_LIMIT;
	data[3] = 0u;
	memcpy(&data[4], &seconds, sizeof(seconds));
	memcpy(&data[8], &milliseconds, sizeof(milliseconds));

	pthread_mutex_lock(&pd_lock);
	error = tlm_notify(communication.session, NULL, NULL,
					   MN_COMID, 0u, 0u,
					   communication.config.source_ip, mn_destination_ip,
					   TRDP_FLAGS_NONE, data, MN_DATA_SIZE, NULL, NULL);
	pthread_mutex_unlock(&pd_lock);

	return error;
}

///prototype function for TRDP PD listener threads
static void *comidListener (void *arg)
{
	ThreadArgs      *args = (ThreadArgs *) arg;
	TRDP_PD_INFO_T  info;
	UINT8           payload[TRDP_MAX_PD_DATA_SIZE];
	UINT32          size;
	UINT32          last_seq = 0u;
	BOOL8           first = TRUE;
	BOOL8           exceeded;
	UINT8           temperature;
	TRDP_ERR_T      error;
	struct timespec now;
	CHAR8           stamp[32];

	printf("Receiving PD ComID %u (Drehgestell %u)\n", args->comid, args->bogie);

	while (keep_running)
	{
		size = TRDP_MAX_PD_DATA_SIZE;

		pthread_mutex_lock(&pd_lock);
		error = tlp_get(communication.session, args->subscriber, &info, payload, &size);
		pthread_mutex_unlock(&pd_lock);

		if (error == TRDP_NO_ERR)
		{
			if ((first || info.seqCount != last_seq) && size >= 1u)
			{
				first    = FALSE;
				last_seq = info.seqCount;

				clock_gettime(CLOCK_REALTIME, &now);
				format_timestamp(stamp, sizeof(stamp), &now);

				temperature = payload[0];
				exceeded    = (temperature > TEMP_LIMIT) ? TRUE : FALSE;

				printf("[%s] ComID %u | Drehgestell %u | Temperatur %3u C | %s\n",
					   stamp, args->comid, args->bogie, (unsigned int) temperature,
					   exceeded ? "UEBERSCHRITTEN" : "OK");

				if (exceeded && !args->overheated)
				{
					args->overheated = TRUE;

					printf("[%s] Grenzwert ueberschritten! | Drehgestell %u | Temperatur %u C\n",
						   stamp, args->bogie, (unsigned int) temperature);

					error = send_overheat_mn(args, temperature, &now);

					if (error == TRDP_NO_ERR)
					{
						printf("[%s] TRDP MN gesendet | ComID %u | %s -> %s | Drehgestell %u | Temperatur %u C\n",
							   stamp, MN_COMID, LOOPBACK_IP, MN_DEST_IP,
							   args->bogie, (unsigned int) temperature);
					}
					else
					{
						fprintf(stderr, "[%s] TRDP MN fehlgeschlagen | ComID %u | Drehgestell %u | %s\n",
								stamp, MN_COMID, args->bogie, vos_getErrorString((VOS_ERR_T) error));
					}
				}
				else if (!exceeded && args->overheated)
				{
					args->overheated = FALSE;

					printf("[%s] INFO | Grenzwert wieder eingehalten | Drehgestell %u | Temperatur %u C\n",
						   stamp, args->bogie, (unsigned int) temperature);
				}
			}
		}
		else if (error != TRDP_NODATA_ERR && error != TRDP_TIMEOUT_ERR)
		{
			fprintf(stderr, "ComID %u: PD receive failed: %s\n",
					args->comid, vos_getErrorString((VOS_ERR_T) error));
			break;
		}

		(void) vos_threadDelay(10000u);
	}

	args->finished = TRUE;

	return NULL;
}

static TRDP_ERR_T subscribe_comid (ThreadArgs *args)
{
	TRDP_ERR_T error;

	error = tlp_subscribe(communication.session, &args->subscriber,
						  NULL, NULL, 0u, args->comid, 0u, 0u,
						  communication.config.source_ip, VOS_INADDR_ANY,
						  communication.config.destination_ip, TRDP_FLAGS_NONE,
						  communication.config.timeout_us, TRDP_TO_SET_TO_ZERO);

	if (error != TRDP_NO_ERR)
	{
		return error;
	}

	args->subscribed = TRUE;

	return tlc_updateSession(communication.session);
}

int main (void)
{
	static ThreadArgs args[NUM_COMIDS];
	VOS_THREAD_T      threads[NUM_COMIDS];
	BOOL8             started[NUM_COMIDS] = { FALSE };
	CHAR8             name[NUM_COMIDS][32];
	PD_COMM_CONFIG_T  config;
	TRDP_ERR_T        error;
	VOS_ERR_T         err;
	UINT32            i;

	signal(SIGINT, stop);
	signal(SIGTERM, stop);

	err = vos_threadInit();

	if (err != VOS_NO_ERR)
	{
		fprintf(stderr, "vos_threadInit failed: %s\n", vos_getErrorString(err));
		return 1;
	}

	mn_destination_ip = vos_dottedIP(MN_DEST_IP);

	for (i = 0u; i < NUM_COMIDS; i++)
	{
		memset(&args[i], 0, sizeof(args[i]));
		args[i].comid = FIRST_COMID + i;
		args[i].bogie = i + 1u;
	}

	pd_comm_config_init(&config);
	pd_comm_config_set_comid(&config, args[0].comid);

	if (pd_comm_config_set_source_ip(&config, LOOPBACK_IP) != TRDP_NO_ERR ||
		pd_comm_config_set_destination_ip(&config, LOOPBACK_IP) != TRDP_NO_ERR)
	{
		fprintf(stderr, "Invalid IPv4 address.\n");
		vos_threadTerm();
		return 1;
	}

	error = pd_comm_open(&communication, &config);

	if (error == TRDP_NO_ERR)
	{
		error = pd_comm_open_subscriber(&communication);
	}

	if (error != TRDP_NO_ERR)
	{
		fprintf(stderr, "PD receiver setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
		pd_comm_close(&communication);
		vos_threadTerm();
		return 1;
	}

	args[0].subscriber = communication.subscriber;
	args[0].subscribed = TRUE;

	for (i = 1u; i < NUM_COMIDS; i++)
	{
		error = subscribe_comid(&args[i]);

		if (error != TRDP_NO_ERR)
		{
			fprintf(stderr, "ComID %u: subscribe failed: %s\n",
					args[i].comid, vos_getErrorString((VOS_ERR_T) error));
		}
	}

	printf("Temperaturueberwachung | Grenzwert %u C | Bei Ueberschreitung: TRDP MN ComID %u an %s\n",
		   TEMP_LIMIT, MN_COMID, MN_DEST_IP);

	for (i = 0u; i < NUM_COMIDS; i++)
	{
		if (args[i].subscribed == FALSE)
		{
			continue;
		}

		snprintf(name[i], sizeof(name[i]), "comid_%u", args[i].comid);

		err = vos_threadCreate(
			&threads[i],
			name[i],
			VOS_THREAD_POLICY_OTHER,
			0,
			0,
			0,
			(VOS_THREAD_FUNC_T) comidListener,
			&args[i]
		);

		if (err != VOS_NO_ERR)
		{
			fprintf(stderr, "Thread for ComID %u could not be started: %s\n",
					args[i].comid, vos_getErrorString(err));
		}
		else
		{
			started[i] = TRUE;
		}
	}

	printf("Press Ctrl-C to stop.\n");

	while (keep_running)
	{
		pthread_mutex_lock(&pd_lock);
		error = pd_comm_process(&communication);
		pthread_mutex_unlock(&pd_lock);

		if (error != TRDP_NO_ERR && error != TRDP_BLOCK_ERR)
		{
			fprintf(stderr, "TRDP process failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
			keep_running = 0;
		}

		(void) vos_threadDelay(10000u);
	}

	for (i = 0u; i < NUM_COMIDS; i++)
	{
		if (started[i])
		{
			while (!args[i].finished)
			{
				(void) vos_threadDelay(10000u);
			}
		}
	}

	for (i = 1u; i < NUM_COMIDS; i++)
	{
		if (args[i].subscribed)
		{
			(void) tlp_unsubscribe(communication.session, args[i].subscriber);
		}
	}

	pd_comm_close(&communication);
	vos_threadTerm();

	return 0;
}
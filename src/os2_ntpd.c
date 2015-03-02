/* os2_ntpd.c - client NTP daemon for OS/2

  ---------------------------------------------------------------------------
   - client process to connect to an NTP service via UDP port 123. - - The
   client process uses the NTP data packet received to set the time of the -
   local clock.  The accuracy and resolution of the OS/2 IBM-PC architecture is
   - limited to 31.25 mS. - - This program and the time protocol it uses are
   under development and the - implementation may change without notice.
   ---------------------------------------------------------------------------

  ***** BEGIN LICENSE BLOCK *****
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.

  Copyright (c) 1998 Bruce M. Penrod (Initial Developer)
  Portions Copyright (c) 2014 Steven H. Levine
  ***** END LICENSE BLOCK *****


   1998-06-01 BMP Baseline
   2014-03-29 SHL Reformt with indent
   2014-04-20 SHL Add alphanumeric hotkeys
   2014-04-22 SHL Convert to OpenWatcom
   2014-06-10 SHL Avoiid DST ringing
   2014-06-10 SHL Ensure code page 437 characters display correctly
   2014-07-12 SHL Support running detached
*/

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <sys\timeb.h>                  /* ftime */
#ifdef __WATCOMC__
#undef _M_IX86                          // 2014-04-21 SHL avoid intrinsic inp, outp
#endif
#include <conio.h>
#ifdef __WATCOMC__
#define _M_IX86
#endif
#include <process.h>                    /* _beginthread, _endthread */
#include <stdarg.h>                     /* va_list */

#define INCL_NOPMAPI                    /* Don't need PM Windowing stuff */
#define INCL_DOSDATETIME                /* System time and date functions */
#define INCL_DOSPROCESS                 /* Threads and process control */
#define INCL_DOSMISC                    /* DosQuerySysInfo */
#define INCL_DOSEXCEPTIONS              /* DosEnterMustComplete */
#define INCL_DOSSEMAPHORES              /* Semaphores */
#define INCL_VIO                        /* Screen code page control */
#define INCL_DOSERRORS                  /* Screen code page control */

#include <os2.h>

#define BSD_SELECT              /* Force use of the correct (BSD) select() function */

/* IBM Warp Toolkit Socket Includes */
#include <sys\socket.h>
#include <sys\select.h>
#include <netdb.h>

#include "portio.h"                     /* Portio IOPL inp(),outp() - bcos2 version */

#include "ntp_menu.h"                   /* User Interface include */

#define OS2_NTPD_VERSION          "OS2_NTPD V1.4--Network Time Protocol Client"
#define AUTHOR                    "Bruce M. Penrod"
// 2014-04-16 SHL FIXME to be gone
// #define COMPANY                   "EndRun Technologies"
// #define ADDRESS1                  "2270 Northpoint Parkway"
// #define ADDRESS2                  "Santa Rosa, CA  95407"
#define MAINTAINER                   "Steven H. Levine, steve53@earthlink.net"
#define WEBSITE                   "For Network Time Server Product Info, try http://endruntechnologies.com/"
// #define REGISTRATION              "Please Register OS2_NTPD:  E-Mail your Name & Address to Bruce"

#define  NTP_UDP_PORT              123          /* NTP specific port */
#define  UNIX_EPOCH_MJD            40587        /* UNIX epoch date MJD */
#define  UNIX_EPOCH_SECONDS        2208988800   /* UNIX epoch NTP seconds */
#define  TWO_TO_32                 4294967296.0 /* INT MAX VALUE */
#define  TWO_TO_32_DIV_1000        4294967.296  /* INT MAX VALUE/1000 */

#define  NORMAL_PRIORITY           31           /* REGULAR CLASS PRIORITY */
#define  DEFAULT_STKSIZE           32768        /* Default Thread StackSize */

#define  MAX_POLL_INTERVAL         256
#define  REPLY_TIMEOUT_MS          625
#define  SUSPECT_THRESHOLD         .25  /* Threshold to save pkt */

#define  K_STATS                   .9   /* peer stats exp filter */
#define  ONE_MINUS_K_STATS         .1   /* coefficients */

#define  TAU                     (24.0 * send_interval)
#define  DAMPING                  2.0   /* loop filter damping ratio */

#define  GOOD_PKT                  1    /* GetNTPPktThread flag value */
#define  TIME_OUT                  0    /* GetNTPPktThread flag value */
#define  BAD_PKT                  -1    /* GetNTPPktThread flag value */
#define  IN_ALARM                  2    /* GetNTPPktThread flag value */

#define  SHIFT_SUCCESS             0    /* Shift_sub_secondsThread flag value */
#define  SHIFT_IN_PROGRESS         1    /* Shift_sub_secondsThread flag value */
#define  SHIFT_NOT_NOW            -1    /* Shift_sub_secondsThread flag value */
#define  SHIFT_BAD_SLEEP          -2    /* Shift_sub_secondsThread flag value */

#define  COARSE_ADJ_FAILURE       16    /* adjust_clock return values */
#define  COARSE_ADJ_SUCCESS        0    /* adjust_clock return values */
#define  FINE_ADJ_SUCCESS          1    /* adjust_clock return values */
#define  ADJ_NOT_NOW               2    /* adjust_clock return values */
#define  ADJ_BAD_SLEEP             4    /* adjust_clock return values */
#define  ADJ_NOT_NEEDED            8    /* adjust_clock return values */
#define  ADJ_THREAD_TIMEOUT       32    /* adjust_clock return values */

/*********************************************************/
/************ Global Variable Declarations ***************/
/*********************************************************/

/* Abbreviated Video Mode Data for Color 80 character/line mode */
AMD C80 = {
    8,                                  /* length of structure in bytes */
    1,                                  /* type, PM windowable color text mode */
    4,                                  /* 16 colors */
    80,                                 /* 80 columns */
    25                                  /* 25 rows */
};

/**
 * Log file handle
 * Used for log output
 * Points to stdout when logging turned off
 * Points to file handle when logging turned on
 * Points to file handle and used all output when running detached
 */
static FILE *hLogFile = stdout;

static
FILE *rtc_type = NULL,                  /* RTC type file pointer */
     *drift = NULL,                     /* Oscillator frequency offset file */
     *suspect_pkt = NULL;               /* Bad Time pkts save file */

HMTX hmtxNTP_Pkt,                       /* semaphore handle to allow sharing NTP data */
    hmtxLog,                            /* semaphore to control opening and closing log file */
    hmtxExclusive;                      /* semaphore to prevent multiple instances of os2_ntpd */

HEV hevTickAdj,                         /* semaphore on shifttime_subseconds completion */
    hevPrint;                           /* semaphore on print_thread completion */

double correction = 0.0,                /* actual correction to be peformed */
    ensemble_mean = 0.0,                /* ensemble stat */
    ensemble_variance = 0.0,            /* ensemble stat */
    freq_offset = 0.0,                  /* local clock frequency offset */
    tau = 0.0;                          /* Loop Filter working tau */

CHAR fRTC_Identified,                   /* flag indicating whether RTC type is known */
    fDrift;                             /* flag indicating whether LO freq_offset is known */

typedef enum { logOff,
	       logOpening,
	       logOpened,
	       logClosing
	     } tLogState;

volatile tLogState logState = logOff;   /* foreground mode logging state */

UINT logLevel;				/* detached mode logging verbosity */

volatile CHAR fQuick_Sync = 0;          /* flag enabling jam sync of local clock */
volatile CHAR fDebug = 0;               /* flag enabling suspect pkt logging */

INT sleep_error;                        /* probe for shift_sub_seconds thread */

CHAR szLogFileName[CCHMAXPATH];         /* log file path */

INT peer_N;                             /* number of servers in cfgdata file */
PCSZ menu_desc[12] =                    /* fkey menu button strings */
{ "Show ~Nxt Pkt", "Show P~rv Pkt", "~Quit", "----", "----", "Quick ~Sync",
    "Show ~Peers", "----", "----", "Enable ~Debug", "Open ~LogFile", "~About"
};

typedef struct
{
    UCHAR LI_VN_Mode;                   // LI and Status
    UCHAR Stratum;                      // aka Type
    CHAR Poll;                          // 2**Poll seconds, V3
    CHAR Precision;                     // 2**Precision, -32..+32
    INT RootDelay;
    INT RootDisp;
    UCHAR RefID[4];
    UINT RefTStamp[2];
    UINT OrgTStamp[2];
    UINT RxTStamp[2];
    UINT TxTStamp[2];
    char Authenticator[12];
} NTP_Pkt;

typedef union
{
    NTP_Pkt Pkt;
    char Buffer[60];
} NTP_Pkt_Buffer;

/* client request packet, same for all peers */
NTP_Pkt_Buffer ntp_request = {
    0x1B,                               /* LI=0,VN=3,Mode=3 */
    2,                                  /* Stratum, via NTP,SNTP */
    4,                                  /* Poll, initially 16 seconds */
    -5,                                 /* Precision, 31 mS */
    0,                                  /* RootDelay */
    0,                                  /* Root Disp */
    {0, 0, 0, 0},                       /* RefID */
    {0, 0},                             /* RefTStamp */
    {0, 0},                             /* OrgTStamp */
    {0, 0},                             /* RxTStamp */
    {0, 0},                             /* TxTStamp */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} /* Auth */
};

typedef struct
{
    char cp_ntp[81];                    /* Server address string */
    INT fBufferRdy;                     /* semaphore, pkt reception status, bad pkt length */
    INT s_ntp;                          /* socket descriptor */
    NTP_Pkt ntp_reply_h;                /* NTP reply packet, host ordered bytes */
    double t_org;                       /* Client Originate TS, seconds since 1900 */
    double t_rx;                        /* Server Receive TS, seconds since 1900 */
    double t_tx;                        /* Server Transmit TS, seconds since 1900 */
    double t_recv;                      /* Client Receive TS, seconds since 1900 */
    double pathdelay;                   /* Symmetrical, one-way delay */
    double correction;                  /* +=advance, -=retard */
    double mean;                        /* mean */
    double sumsqrs;                     /* sum of squares for variance calculation */
    double variance;                    /* variance */
    double max;                         /* maximum server - client measurement */
    double min;                         /* minimum server - client measurement */
} get_ntp_pkt_buffer;

typedef struct
{
    double correction;                  /* tickadj value in seconds */
    char fSuccess;                      /* return status */
    short RTC_type;                     /* RTC type */
} ShiftTime_subseconds_buffer;

typedef struct
{
    INT adj_rtn;
} print_thread_buffer;

typedef struct
{
    UINT last_time;
    double freq_offset;
} LO_Drift;

/* array holding peer data structures */
static get_ntp_pkt_buffer peerdata[22];

/* array of indices to current peers, # of current peers */
INT peer_current[22];
INT peer_N_actual;

/* structure for communicating with ShiftTime_subseconds_thread */
ShiftTime_subseconds_buffer tickadj = { 0.0, 0, 0 };

/* structure for communicating with print_thread */
print_thread_buffer prt_buf;

/* structure holding LO phase drift (frequency offset) and timestamp of applicability */
LO_Drift current_drift = { 0, 0.0 };

static char title_string[80];


/*********************************************************/
/************ Internal Function Prototypes ***************/
/*********************************************************/

/* General Socket functions from Comer Vol. III */
INT connectTCP(PSZ host, PSZ service);
INT connectUDP(PSZ host, PSZ service);
INT connectsock(PSZ host, PSZ service, PSZ protocol);

/* functions to manipulate the NTP packet and print its contents */
VOID ntoh_ntp(NTP_Pkt * ntp_reply_h, NTP_Pkt * ntp_reply_n);
VOID PrintNTP_Pkt(get_ntp_pkt_buffer * ntp);
VOID save_suspect_pkt(get_ntp_pkt_buffer * ntp);

/* NTP packet receive function */
VOID get_ntp_pkt(get_ntp_pkt_buffer * ntp);

/* calculate mean and variance of ith peer */
VOID do_peer_stats(INT i);

/* calculate ensemble mean and variance */
VOID do_ensemble_stats(char peer_N_actual,
		       double *ensemble_mean,
		       double *ensemble_variance);

/* Control Loop Filter */
VOID LoopFilter(double *correction, double clock_error, INT send_interval);

/* adjust the clock based on correction from loop filter */
INT adjust_clock(double *correction);

/* ShiftTime seconds */
INT ShiftTime(double correction);       /* rough setting */

/* ShiftTime_subseconds thread */
VOID ShiftTime_subseconds_thread(PVOID ptickadj); /* fine setting */

/* print thread */
VOID print_thread(PVOID print_buffer);

/* function key interface thread */
VOID fkey_interface_thread(PVOID dummy);

VOID printf_info_status(UINT line_number, INT just, PCSZ pszFmt, ...);
VOID printf_fatal_status(PCSZ pszFmt, ...);

VOID put_fatal_status(PCSZ status_line);

VOID report_fatal_cfgfile_error(INT line_number);
VOID report_fatal_fopen_fail(PSZ filename);
VOID report_fatal_error(char* fmt, ...);

char Identify_RTC(double ensemble_mean);
VOID NTP_ftime(struct timeb *timeb);
VOID Init_PeerStats(VOID);
VOID Update_Drift(VOID);

VOID About(VOID);
VOID Usage(VOID);

BOOL log_open(BOOL return_on_error);
VOID log_close();

/**
 * main
 */

INT main(INT argc, PSZ argv[])
{
    /* Local variables */
    FILE *configdata;                   /* daemon configuration file */
    INT loop_num;                       // 2014-07-14 SHL
    INT loop_inc;
    INT send_interval;                  /* time between pkts, cfg_data pool interval */
    INT send_N;                         /* # of pkts & servers */
    INT sleeptime;
    INT i;
    INT j;                              /* loop counters */
    INT test;
    INT tries;
    PSZ psz;
    INT argnum;
    UINT state;

    INT last_send_interval;

    TID tidPrintThread;                 /* print thread id */
    TID tidFKeyThread;  /* console control interface thread id */
    ULONG ulPostCnt;                    /* for DosResetEventSem */
    APIRET apiret;

    CHAR temp_buf[81];
    struct timeb last_time, now;        /* to calculate sleep time in main loop */

    /******************** BEGIN MAIN EXECUTION ***************************/

    sprintf(title_string, "%s built %s@%s PST", OS2_NTPD_VERSION, __DATE__, __TIME__);

    /* Determine if running detached
       Try to set screen to code page 437 because special characters depend on it
       Silently ignore errors
       2014-06-10 SHL
    */
    apiret = VioSetCp(0, 437, NULLHANDLE);
    if (apiret == ERROR_VIO_DETACHED) {
	// 2014-07-12 SHL
	running_detached = TRUE;
	log_open(FALSE);
	fputs(title_string, hLogFile);
	fputc('\n', hLogFile);
	fflush(hLogFile);
    }
    else
	clrscr();

    /* Make sure this is the only copy of OS2_NTPD running now */
    hmtxExclusive = NULLHANDLE;
    if (!DosOpenMutexSem("\\sem32\\OS2_NTPD", &hmtxExclusive))
	report_fatal_error("OS2_NTPD is already running on this system!!!");
    else {
	apiret = DosCreateMutexSem("\\sem32\\OS2_NTPD", &hmtxExclusive, 0L, TRUE);
	if (apiret)
	    report_fatal_error("DosCreateMutexSem failed with error %u", apiret);
    }

    state = 0;
    for (argnum = 1; argnum < argc; argnum++) {
      psz = argv[argnum];
      switch(state) {
      case 0:
	  if (*psz == '-' || *psz == '/') {
	      psz++;
	      if (*psz == '?' || *psz == 'h')
		  Usage();
	      if (*psz == 'l') {
		  // FIXME to complain if not detached
		  logLevel++;		// Applies only to detached logging
		  continue;
	      }
	  }
	  state++;			// End of switch options
	  // Drop thru
      case 1:
	  strcpy(peerdata[0].cp_ntp, psz); // NTP server host
	  peer_N = 1;                     /* number of peer NTP server hosts */
	  // Preset defaults
	  send_interval = 1;
	  send_N = 1;
	  loop_inc = 1;
	  state++;
	  break;
      case 2:
	  sscanf(psz, "%d", &send_interval);
	  // FIXME to complain
	  if (send_interval <= 0)
	      send_interval = 1;          // FIXME to initialize to 16 ???
	  break;
      case 3:
	  sscanf(argv[3], "%d", &send_N);
	  if (send_N == 0) {
	      /* loop forever */
	      loop_inc = 0;
	      send_N = 1;
	  }
	  else
	      loop_inc = 1;
	  state++;
	  break;
      default:
	  Usage();
      } // switch
    } // for args

    if (state <= 1) {
	// No args - try for cfg_data
	if (!(configdata = fopen("cfg_data", "r")))
	    report_fatal_fopen_fail("cfg_data");

	if (!fgets(temp_buf, 80, configdata))
	    report_fatal_cfgfile_error(1); /* read first line */
	if (strncmp(temp_buf, "cfg_data", strlen("cfg_data")))
	    report_fatal_cfgfile_error(1);
	if (!fgets(temp_buf, 80, configdata))
	    report_fatal_cfgfile_error(2); /* read second line */
	if (strncmp
	    (temp_buf, "poll interval = ", strlen("poll interval = ")))
	    report_fatal_cfgfile_error(2);
	if (1 != sscanf(temp_buf, "poll interval = %d", &send_interval))
	    report_fatal_cfgfile_error(2);
	peer_N = 0;
	while (fgets(temp_buf, 80, configdata)) {
	    if (peer_N == 22) {
		fputs("Can only use the first 22 NTP peers\n", hLogFile);
		break;
	    }
	    sscanf(temp_buf, "%s", peerdata[peer_N].cp_ntp);
	    peer_N++;
	}
	fprintf(hLogFile, "There are %d peer NTP server hosts configured:\n", peer_N);
	for (i = 0; i < peer_N; i++) {
	    fprintf(hLogFile, "Server %2d = %s\n", i, peerdata[i].cp_ntp);
	}
	DosSleep(500);                  // 2014-05-17 SHL
	/* set loop control parameters for loop forever */
	send_N = 1;
	loop_inc = 0;
	fclose(configdata);
	fflush(hLogFile);               // In case redirected
    } // if use cfgdata
    else {
	Usage();
    }

    // Initialize poll interval
    ntp_request.Pkt.Poll = 0;
    i = send_interval;
    while (i /= 2)
	ntp_request.Pkt.Poll++;
    last_send_interval = send_interval;
    if (running_detached)
	NTP_ftime(&now);            // Code assumes valid

    /* check for rtc_type file */
    if (!(rtc_type = fopen("rtc_type", "r")))
	fRTC_Identified = 0;
    else if (1 == fscanf(rtc_type, "%d", &tickadj.RTC_type)
	     && tickadj.RTC_type == 0 || tickadj.RTC_type == 500) {
	fRTC_Identified = 1;
	fclose(rtc_type);
    }
    else {
	fRTC_Identified = 0;
	fclose(rtc_type);
    }

    /* check for drift file */
    if (!(drift = fopen("drift", "r")))
	fDrift = 0;                     /* file doesn't exist */
    else if (2 == fscanf(drift,
			 "%u,%lf",
			 &current_drift.last_time,
			 &current_drift.freq_offset)) {
	current_drift.freq_offset *= -1.0; /* reverse sign */
	fDrift = 1;
	fclose(drift);
    }
    else {
	/* bad file format */
	fDrift = 0;
	fclose(drift);
	current_drift.last_time = 0;
	current_drift.freq_offset = 0.0;
    }

    Init_PeerStats();

    tzset();                            /* Get timezone from TZ, not needed for ftime() */
    sock_init();                        /* Initialize sockets */

    /* get above the noise */
    apiret = DosSetPriority(PRTYS_PROCESS, PRTYC_REGULAR, NORMAL_PRIORITY, 0);
    if (apiret)
	report_fatal_error("DosSetPriority failed with error %u", apiret);
    /* semaphore to allow main() and print_thread to coordinate log file access */
    apiret = DosCreateMutexSem(NULL, &hmtxLog, 0L, FALSE);
    if (apiret)
	report_fatal_error("DosCreateMutexSem failed with error %u", apiret);
    /* semaphore for display and pkt gathering */
    apiret = DosCreateMutexSem(NULL, &hmtxNTP_Pkt, 0L, FALSE);
    if (apiret)
	report_fatal_error("DosCreateMutexSem failed with error %u", apiret);
    /* semaphore for shifttime_subseconds */
    apiret = DosCreateEventSem(NULL, &hevTickAdj, 0L, FALSE);
    if (apiret)
	report_fatal_error("DosCreateEventSem failed with error %u", apiret);
    /* semaphore for print_thread - init posted */
    apiret = DosCreateEventSem(NULL, &hevPrint, 0L, TRUE);
    if (apiret)
	report_fatal_error("DosCreateEventSem failed with error %u", apiret);

    if (!running_detached) {
	/* set up user interface menu */
	ntp_menu_setup(&C80, menu_desc);
	/* put up program info and registration screen */
	About();
	/* user interface thread, never terminates */
	tidFKeyThread = _beginthread(fkey_interface_thread,
				     NULL,
				     DEFAULT_STKSIZE,
				     NULL);
	if (tidFKeyThread == (TID)-1) {
	    printf_fatal_status("Can not start fkey_interface thread, error %d",
				errno);
	  }
    } // if !detached

   /******************** MAIN LOOP **********************/
   /*****************************************************/

    if (running_detached)
	log_close();                    // Will reopen in loop

    for (loop_num = 0; loop_num < send_N; loop_num += loop_inc) {

	apiret = DosRequestMutexSem(hmtxLog, SEM_INDEFINITE_WAIT);
	if (apiret)
	    printf_fatal_status("DosRequestMutexSem failed with error %u", apiret);

	if (running_detached)
	    log_open(FALSE);

	switch (logState) {
	case logOff:
	    break;
	case logOpening:
	    log_open(FALSE);
	    fprintf(hLogFile,
		    "There are %d peer NTP server hosts configured:\n\n",
		    peer_N);
	    for (j = 0; j < peer_N; j++) {
		peerdata[j].max = peerdata[j].min = 0.0;
		fprintf(hLogFile, "Server %2d = %s\n", j,
			peerdata[j].cp_ntp);
	    }
	    fprintf(hLogFile, "\n%67s", "");
	    for (j = 0; j < peer_N; j++) {
		fprintf(hLogFile, "  ----------- #%2d ----------", j);
	    }
	    fprintf(hLogFile, "\nTimeStamp        Ensemb.    Sigma   LoopFilt   FreqOffset     Corr.");
	    for (j = 0; j < peer_N; j++) {
		fprintf(hLogFile, "      S-C     Mean     Sigma");
	    }
	    fprintf(hLogFile, "\n");
	    log_close();
	    logState = logOpened;
	    // Drop thru
	case logOpened:
	    /* open for appending  - closed by print_thread */
	    log_open(TRUE);
	    if (hLogFile == stdout) {
		printf_fatal_status("Can not open %s log file, error %d",
				    szLogFileName, errno);
	    }
	    break;
	case logClosing:
	    logState = logOff;
	    break;
	default:
	    put_fatal_status("Unexpected logState");
	} // switch logState

	apiret = DosReleaseMutexSem(hmtxLog);
	if (apiret)
	    printf_fatal_status("DosReleaseMutexSem failed with error %u", apiret);

	/* protect since may be in use by fkey_menu_thread */
	apiret = DosRequestMutexSem(hmtxNTP_Pkt, SEM_INDEFINITE_WAIT);
	if (apiret)
	    printf_fatal_status("DosRequestMutexSem failed with error %u", apiret);

	/* counter of servers which returned valid packets */
	peer_N_actual = 0;

	apiret = DosReleaseMutexSem(hmtxNTP_Pkt);
	if (apiret)
	    printf_fatal_status("DosReleaseMutexSem failed with error %u", apiret);

	NTP_ftime(&last_time);          /* save time of poll start */
	/* poll all peer NTP hosts in config file */
	for (i = 0; i < peer_N; i++) {
	    if ((peerdata[i].s_ntp =
		 connectUDP(peerdata[i].cp_ntp, "123")) < 0) {
		DosSleep(REPLY_TIMEOUT_MS);
		continue;
	    }
	    peerdata[i].fBufferRdy = TIME_OUT; /* clear semaphore */

	    /* packet gatherer */
	    get_ntp_pkt(&peerdata[i]);
	    switch (peerdata[i].fBufferRdy) {
	    case TIME_OUT:
		printf_info_status(0, LEFT,
		   "*** Timed Out Waiting on Reply from Server %s ***",
		   peerdata[i].cp_ntp);
		DosSleep(REPLY_TIMEOUT_MS);
		break;
	    case IN_ALARM:
		printf_info_status(0, LEFT,
		   "*** LI Bits in Alarm State from Server %s ***",
		   peerdata[i].cp_ntp);
		DosSleep(REPLY_TIMEOUT_MS);
		break;
	    case GOOD_PKT:
		/* protect so fkey_menu_thread doesn't get mixed up */
		apiret = DosRequestMutexSem(hmtxNTP_Pkt, SEM_INDEFINITE_WAIT);
		if (apiret)
		  printf_fatal_status("DosRequestMutexSem failed with error %u", apiret);

		/* add current server to current ensemble */
		peer_current[peer_N_actual] = i;
		/* increment count of servers in current ensemble */
		peer_N_actual++;
		/* calculate mean and variance of ith peer */
		do_peer_stats(i);

		apiret = DosReleaseMutexSem(hmtxNTP_Pkt);
		if (apiret)
		  printf_fatal_status("DosReleaseMutexSem failed with error %u", apiret);
		break;
	    default:
		if (peerdata[i].fBufferRdy < 0) {
		    /* bad packet */
		    printf_info_status(0, LEFT,
		       "*** Bad Packet, Length = %d Received from Server %s ***",
		       -(peerdata[i].fBufferRdy - BAD_PKT),
		       peerdata[i].cp_ntp);
		    DosSleep(REPLY_TIMEOUT_MS);
		}
		break;
	    }
	    soclose(peerdata[i].s_ntp); /* close socket */
	}                               /* end server polling loop */

	/* calculate ensemble if have valid replies */
	if (peer_N_actual > 0) {
	    do_ensemble_stats(peer_N_actual,
			      &ensemble_mean,
			      &ensemble_variance);

	    /* update loop averaging time and update rate */
	    if (fRTC_Identified) {
		if (fabs(ensemble_mean) < .016
		    && send_interval < MAX_POLL_INTERVAL) {
		    send_interval *= 2;
		    ntp_request.Pkt.Poll++;
		}
		else if (fabs(ensemble_mean) > .25 && send_interval > 16) {
		    send_interval /= 2;
		    ntp_request.Pkt.Poll--;
		}
	    }
	    else {
		/* Identify Real Time Clock type */
		put_info_status("Identifying Real Time Clock type", 0, CENTER);
		Identify_RTC(ensemble_mean);
	    }
	}
	else {
	   /* no server replies, coast loop filter and move toward max polling interval */
	    ensemble_mean = 0.0;        /* coast the loop filter */
	    if (send_interval < MAX_POLL_INTERVAL) {
		send_interval *= 2;
		ntp_request.Pkt.Poll++;
	    }
	}

	if (!fRTC_Identified)
	    DosSleep(2000);             // Wait a bit
	else {
	    /* can make client clock adjustments */
	    /* Type II Loop Filter */
	    if (!fQuick_Sync && fDrift && abs(ensemble_mean) >= 60 * 60 * .9) {
	      /* Assume DST switch - force to prevent PLL from ringing */
#             define SYNC_FORCED 66
	      fQuick_Sync = SYNC_FORCED; // Indicate forced
	      while (send_interval > 16) {
		  send_interval /= 2;
		  ntp_request.Pkt.Poll--;
	      }
	    }

	    LoopFilter(&correction, ensemble_mean, send_interval);
	    /* make adjustment to local clock */
	    /* failure  modes which are recoverable */
	    test = ADJ_NOT_NOW | ADJ_BAD_SLEEP | ADJ_THREAD_TIMEOUT;
	    tries = 0;
	    while ((prt_buf.adj_rtn = adjust_clock(&correction)) & test
		   && tries < 5) {
		tries++;
		DosSleep(200);          /* try again */
	    }

	    /* print thread displays ensemble info to status area or
	      log file if running detached and terminates self
	      thread will close log file before terminating
	    */
	    /* wait one second */
	    apiret = DosWaitEventSem(hevPrint, 1000);
	    if (apiret) {
		if (apiret != ERROR_TIMEOUT)
		    printf_fatal_status("DosWaitEventSem failed with error %u", apiret);
		else {
		  /* should have been done by now - if not oh well time to die */
		  apiret = DosKillThread(tidPrintThread);
		  if (apiret)
		      printf_fatal_status("DosKillThread failed with error %u", apiret);
		  put_fatal_status("Print Thread still running");
		}
	    }

	    if (running_detached) {
		static time_t last_report;
		if (send_interval != last_send_interval) {
		    last_report = now.time;
		    printf_info_status(0, LEFT, "Poll interval changed to %u seconds", send_interval);
		    last_send_interval = send_interval;
		}
		// 2014-09-03 SHL Show initial interval as startup
		// 2014-09-03 SHL Show current interval every 6 hours
		else if (now.time - last_report > 6 * 60 * 60) {
		    last_report = now.time;
		    printf_info_status(0, LEFT, "Poll interval is %u seconds", send_interval);
		}
	    }

	    apiret = DosResetEventSem(hevPrint, &ulPostCnt);
	    if (apiret)
		printf_fatal_status("DosResetEventSem failed with error %u", apiret);

	    tidPrintThread = _beginthread(print_thread,
			     NULL,
			     DEFAULT_STKSIZE,
			     &prt_buf);
	    if (tidPrintThread == (TID)-1) {
		printf_fatal_status("Can not start print thread, error %d",
				    errno);
	    }

	    NTP_ftime(&now);            /* save time at end of poll cycle */
	    sleeptime = send_interval - (now.time - last_time.time); /* in seconds */
	    if (sleeptime < 0 || sleeptime > send_interval)
		sleeptime = send_interval * 1000;
	    else
		sleeptime *= 1000;
	    DosSleep(sleeptime);        /* sleep till next polling interval */
	} // if RTC
    } // for loops

    DosKillThread(tidPrintThread);      // Just in case - FIXME to be gone?

    if (!running_detached) {
	apiret = DosKillThread(tidFKeyThread);
	if (apiret)
	    printf_fatal_status("DosKillThread tidFKeyThread failed with error %u", apiret);
	for (i = 0; i < 25; i++) {
	    put_line_in_buffer("**************OS2_NTPD Terminating***************",
			       i + 1, CENTER);
	    puttext(1, 1, textinfo.screenwidth, textinfo.screenheight, screen_buffer);
	    DosSleep(125);
	}
	DosSleep(500);
	clrscr();
    } // if !detached
    return EXIT_SUCCESS;
}

/**
 * Show usage
 */

VOID Usage(VOID)
{
    puts("Usage: os2_ntpd [-l] [server [interval [request]]]");
    puts("");
    puts(" -l       Enable logging, repeat for more verbose logging");
    puts(" server   The NTP Server Host (name or dotted decimal)");
    puts(" interval The Client Polling Interval (integer power of two seconds; default is 16)");
    puts(" requests The number of NTP requests to send, integer; 0 means indefinite, default is 1");
    puts("");
    puts("This program requires three arguments when used without a cfgdata file:");
    puts("1)  The NTP Server Host (name or dotted decimal)");
    puts("2)  The Client Polling Interval (integer power of two seconds)");
    puts("3)  The number of NTP requests to send, integer; 0 means indefinite");
    /* how to use cfg_data file */
    puts("When run without arguments a \"cfg_data\" file must exist in the current directory.");
    puts("If the file \"cfg_data\" is present in the current directory,");
    puts("invoke the program with no arguments to use the data from that file.");
    /* specify cfg_data file format */
    puts("The \"cfg_data\" file format is:");
    puts("Line 1:cfg_data--configuration data");
    puts("Line 2:polling interval = X   (X is an integer number of seconds)");
    puts("Lines 3-24:S (S is either the NTP server name or its dotted decimal address)");
    /* show example file format */
    puts("A Sample File:");
    puts("cfg_data");
    puts("polling interval = 16");
    puts("tick.usno.navy.mil");
    puts("206.54.0.31");
    puts("time.nist.gov");
    report_fatal_error("");
}


/**
 * Get one NTP packet
 */

VOID get_ntp_pkt(get_ntp_pkt_buffer * ntp)
{
    struct timeb sendntptime;           /* hold send timestamp from ftime() */
    struct timeb recvntptime;           /* hold recv timestamp from ftime() */
    double deltarecv, deltasend;        /* measured path delays */
    NTP_Pkt_Buffer ntp_reply_n;         /* local received reply packet buffer, network order */
    INT length = 0;                     /* received packet length */

    APIRET apiret;
    INT numfds;                         /* number of file descriptors for select() to check */
    fd_set fdset;                       /* file descriptor set to hold sockets containing input pkts */
    struct timeval timeout;             /* select() timeout parameter */

    numfds = ntp->s_ntp + 1;
    FD_ZERO(&fdset);                    /* clear fdset */
    FD_SET(ntp->s_ntp, &fdset);         /* set bit corresponding to desired socket */

    timeout.tv_sec = 0;
    timeout.tv_usec = REPLY_TIMEOUT_MS * 1000;

    /* UNIX timetag the NTP client request using ftime(), which knows about TZ */
    /* This setting seems to cause keyboard lock-up when it is set too high, like TC-16 */
    apiret = DosSetPriority(PRTYS_PROCESS, PRTYC_TIMECRITICAL, 1, 0); /* boost during timestamping */
    if (apiret)
	printf_fatal_status("DosSetPriority failed with error %u", apiret);
    NTP_ftime(&sendntptime);
    /* format NTP client request timestamp for Tx field of packet */
    ntp_request.Pkt.TxTStamp[0] = htonl((ULONG)sendntptime.time);
    ntp_request.Pkt.TxTStamp[1] =
	htonl((ULONG)(sendntptime.millitm * TWO_TO_32_DIV_1000));
    /* send NTP request packet, don't send last 12 auth bit, some servers don't like */
    send(ntp->s_ntp, ntp_request.Buffer, sizeof(ntp_request.Buffer) - 12, 0);
    /* receive NTP reply packet from server */
    if (select(numfds, &fdset, 0, 0, &timeout) > 0
	&& FD_ISSET(ntp->s_ntp, &fdset)) {
	NTP_ftime(&recvntptime);        /* timetag the NTP server reply */
	length =
	    recv(ntp->s_ntp, ntp_reply_n.Buffer, sizeof(ntp_reply_n.Buffer),
		 0);
    }
    else
	length = -1;
    /* return to normal */
    apiret = DosSetPriority(PRTYS_PROCESS, PRTYC_REGULAR, NORMAL_PRIORITY, 0);
    if (apiret)
	printf_fatal_status("DosSetPriority failed with error %u", apiret);

    apiret = DosRequestMutexSem(hmtxNTP_Pkt, SEM_INDEFINITE_WAIT);
    if (apiret)
	printf_fatal_status("DosRequestMutexSem failed with error %u", apiret);

    if (length == -1) {
	ntp->fBufferRdy = TIME_OUT;     /* no packet received in time */
	DosReleaseMutexSem(hmtxNTP_Pkt);
	return;
    }

    if (length != 48 && length != 60) {
	/* tell main thread bad packet received */
	ntp->fBufferRdy = BAD_PKT - length;
	DosReleaseMutexSem(hmtxNTP_Pkt);
	return;
    }

    /* first pass, just copy, bytes will be OK */
    ntp->ntp_reply_h = ntp_reply_n.Pkt;
    /* host byte order the ints */
    ntoh_ntp(&ntp->ntp_reply_h, &ntp_reply_n.Pkt);

    ntp->t_org =
	ntp->ntp_reply_h.OrgTStamp[0] +
	ntp->ntp_reply_h.OrgTStamp[1] / TWO_TO_32;
    ntp->t_rx =
	ntp->ntp_reply_h.RxTStamp[0] +
	ntp->ntp_reply_h.RxTStamp[1] / TWO_TO_32;
    deltasend = ntp->t_rx - ntp->t_org; /* server recv - client send */
    ntp->t_tx =
	ntp->ntp_reply_h.TxTStamp[0] +
	ntp->ntp_reply_h.TxTStamp[1] / TWO_TO_32;
    ntp->t_recv =
	(ULONG)(recvntptime.time) + recvntptime.millitm / 1000.0;
    deltarecv = ntp->t_recv - ntp->t_tx; /* client recv - server send */
    ntp->pathdelay = (deltasend + deltarecv) / 2.0;
    ntp->correction = ntp->t_tx + ntp->pathdelay - ntp->t_recv;

    if ((ntp->ntp_reply_h.LI_VN_Mode & 0xC0) == 0xC0) {
	/* tell main thread server is in alarm state */
	ntp->fBufferRdy = IN_ALARM;
	DosReleaseMutexSem(hmtxNTP_Pkt);
	return;
    }

    /* tell main thread that reply was received */
    ntp->fBufferRdy = GOOD_PKT;

    apiret = DosReleaseMutexSem(hmtxNTP_Pkt);
    if (apiret)
	printf_fatal_status("DosReleaseMutexSem failed with error %u", apiret);
}

INT RTC_IRQ_rate[16] = { 0, 256, 128, 8192000, 4096000, 2048000, 1024000, 512,
    256, 128, 64, 32, 16, 8, 4, 2
};

/**
 * RTC Subseconds adjust thread
 */

VOID ShiftTime_subseconds_thread(PVOID ptickadj)
{
    APIRET apiret;
    ShiftTime_subseconds_buffer *tickadj =
	(ShiftTime_subseconds_buffer *)ptickadj;
    ULONG nesting = 0;                  /* for DosEnterMustComplete() */

    struct timeb timeb;                 /* for ftime() */
    ULONG trigger_time_ms, now_time_ms, sleeptime;
    long ticks;
    // 2014-04-21 SHL hide unreferenced to avoid warnings
    USHORT reg_A_norm,                  /* RTC A byte, normal operation */
	reg_A_reset,                    /* RTC A byte, reset divider chain operation */
	portsetup = 0x70,               /* RTC Setup Port */
	portio = 0x71,                  /* RTC I/O Port */
	reg_sec = 0x00,                 /* RTC seconds register */
#if 0 // unreferenced
	reg_min = 0x02,                 /* RTC minutes register */
	reg_hour = 0x04,                /* RTC hours register */
#endif
	reg_A = 0x0A,                   /* RTC A register */
#if 0 // unreferenced
	reg_B = 0x0B,                   /* RTC B register */
#endif
	reg_sec_status,                 /* buffer holding seconds byte */
#if 0 // unreferenced
	reg_min_status,                 /* buffer holding minutes byte */
	reg_hour_status,                /* buffer holding hours byte */
#endif
	reg_A_status;                   /* buffer holding A status byte */
#if 0 // unreferenced
	reg_B_status;                   /* buffer holding B status byte */
#endif
    float fticksize;
    INT IRQ_RATE_ndx = 0, halftick_ms;

    /* This is to determine what value of rate selection the OS has configured */
    /* the RTC to use.  OS/2 prior to Warp 4, Fixpack 1 used 0xb.  The */
    /* clock01.sys driver in Warp 4 Fixpack 1 changed this to 0x9.  This */
    /* changes the periodic interrupt rate from 31.25ms to 7.8125ms. */

    do {
	outp(portsetup, reg_A);
	reg_A_norm = inp(portio);
	IRQ_RATE_ndx = reg_A_norm & 0x0F; /* get RS0-RS3 */
    } while (!IRQ_RATE_ndx);            /* make sure to get valid reading */
    reg_A_norm &= 0x7f;                 /* we don't care what the UIP bit is right now */
    reg_A_reset = reg_A_norm | 0x60;    /* set the DV2, DV1 bits */
    fticksize = .03125;
    fticksize = 1.0 / RTC_IRQ_rate[IRQ_RATE_ndx]; /* floating ticksize */
    halftick_ms = fticksize * 500.0 + .5; /* round up */

    /* RTC_type is either 0 for the correct implementations of the Motorola */
    /* RTC chip, which update the time 500 ms after divisor reset is released, */
    /* or it is 500 for the incorrect implementations, which seem to update the */
    /* time immediately after divisor reset is released. */

    if (tickadj->correction > 0.0) {
	ticks = (long)(tickadj->correction / .03125) + 1;
	tickadj->correction = (ticks - 1) * .03125 + .015625;
    }
    else {
	ticks = (long)(tickadj->correction / .03125);
	tickadj->correction = ticks * .03125 - .015625;
    }
    /* trigger_time_ms = (ULONG)(500 + tickadj->RTC_type - (ticks * 125)/4); */
    trigger_time_ms = (ULONG)(500 + tickadj->RTC_type - (ticks * 125) / 4 +
				      (16 - halftick_ms)); /* new */

    apiret = DosSetPriority(PRTYS_THREAD, PRTYC_FOREGROUNDSERVER, 2, 0); /* get above PPP protocol */
    if (apiret)
	printf_fatal_status("DosSetPriority failed with error %u", apiret);

    ftime(&timeb);                      /* get current time */
    now_time_ms = timeb.millitm;        /* unbiased current time */
#if 0
    sleeptime = trigger_time_ms - now_time_ms - 16; /* original unbiased sleep time for incorrect imp */
#endif
    sleeptime = trigger_time_ms - now_time_ms - halftick_ms; /* new */
    /* Determine when to perform the correction */
    if (abs(now_time_ms - trigger_time_ms) < 16)
	;                               /* at the trigger point */
    else if (now_time_ms < trigger_time_ms)
	DosSleep(sleeptime);            /* can do in this second */
    else
	DosSleep(sleeptime + 1000);     /* wait till next second */

    /* Check that sleep was accurate (might have been pre-empted) */
    ftime(&timeb);                      /* get time after sleep */
    now_time_ms = timeb.millitm;        /* unbiased current time */
    sleep_error = now_time_ms - trigger_time_ms; /* global for probing this code */
    if (tickadj->RTC_type && tickadj->correction < 0)
	sleep_error += 1000;
    /* if(abs(sleep_error) > 16)) original *//* should be at trigger point, if not don't adjust */
    /* new, should be at trigger point, if not don't adjust */
    if (abs(sleep_error) > (32 - halftick_ms)) {
	/* tell caller correction not performed */
	tickadj->fSuccess = SHIFT_BAD_SLEEP;
	apiret = DosPostEventSem(hevTickAdj);
	if (apiret)
	    printf_fatal_status("DosPostEventSem failed with error %u", apiret);
	return;
    }

    /* Let's DO IT */
    outp(portsetup, reg_A);             /* check register A */
    reg_A_status = inp(portio);
    if (!(reg_A_status & 0x0080)) {
	/* make sure update not in progress */
	outp(portsetup, reg_sec);
	reg_sec_status = inp(portio);   /* get seconds byte, BCD */
    }
    else {
	/* tell caller correction not  performed */
	tickadj->fSuccess = SHIFT_NOT_NOW;
	DosPostEventSem(hevTickAdj);
	return;
    }
    /* need to advance seconds */
    if (tickadj->RTC_type && tickadj->correction > 0) {
	if (reg_sec_status == 0x0059) {
	    /* don't want to roll everything, wait till next time */
	    tickadj->fSuccess = SHIFT_NOT_NOW;
	    DosPostEventSem(hevTickAdj);
	    return;
	}
	/* need to roll tens ? */
	else if ((reg_sec_status & 0x000F) == 0x09) {
	    reg_sec_status += 16;       /* roll tens */
	    reg_sec_status &= 0x00F0;   /* roll ones */
	}
	else
	    reg_sec_status += 1;        /* just roll second */
	outp(portsetup, reg_sec);       /* set RTC seconds one ahead */
	outp(portio, reg_sec_status);
    }

    apiret = DosEnterMustComplete(&nesting); /* Avoid getting interrupted here */
    if (apiret)
	printf_fatal_status("DosEnterMustComplete failed with error %u", apiret);

    outp(portsetup, reg_A);
    outp(portio, reg_A_reset);          /* place divider chain in reset */
    outp(portsetup, reg_A);
    outp(portio, reg_A_norm);           /* restore divider chain to normal */

    apiret = DosExitMustComplete(&nesting);
    if (apiret)
	printf_fatal_status("DosExitMustComplete failed with error %u", apiret);

    /* tell caller correction successfully performed */
    tickadj->fSuccess = SHIFT_SUCCESS;
    DosPostEventSem(hevTickAdj);
    return;
}

/**
 * User control interface thread
 * Does not run if running detached
 */

VOID fkey_interface_thread(PVOID dummy)
{
    APIRET apiret;
    INT key, i = 0, j;
    INT quit = 0;
    char line[81];

    /* slightly above normal */
    apiret = DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, 8, 0);
    if (apiret)
	printf_fatal_status("DosSetPriority failed with error %u", apiret);

    while (!quit) {
	/* check for key press four times a second */
	while (!kbhit())
	    DosSleep(250);
	key = getch();
	// 2014-04-16 SHL support normal and extended keycodes
	if (key == 0 || key == 0xe0)
	    key = getch() << 8;

	apiret = DosRequestMutexSem(hmtxNTP_Pkt, SEM_INDEFINITE_WAIT);
	if (apiret)
	    printf_fatal_status("DosRequestMutexSem failed with error %u", apiret);

	switch (key) {
	case 59 << 8:                   // F1
	case 'n':
	case 'N':
	    // Show next packet
	    if (peer_N_actual > 0) {
		if (i < peer_N_actual - 1)
		    i++;
		else
		    i = 0;
		PrintNTP_Pkt(&peerdata[peer_current[i]]);
	    }
	    else {
		put_status("No Server Replies Received Yet!", 0, CENTER);
	    }
	    break;
	case 60 << 8:                   // F2
	case 'r':
	case 'R':
	    // Show previous packet
	    if (peer_N_actual > 0) {
		if (i > 0)
		    i--;
		else
		    i = peer_N_actual - 1;
		PrintNTP_Pkt(&peerdata[peer_current[i]]);
	    }
	    else {
		put_status("No Server Replies Received Yet!", 0, CENTER);
	    }
	    break;
	case 61 << 8:                   // F3
	case 'q':
	case 'Q':
	    quit = 1;
	    break;
	    break;
	case 64 << 8:                   // F6
	case 's':
	case 'S':
	    // Quick sync
	    fQuick_Sync = 1;
	    modify_menu_desc(5, "Quick ~Sync", BLINK_ON);
	    break;
	case 65 << 8:                   // F7
	case 'p':
	case 'P':
	    // Show peers
	    if (peer_N_actual > 0) {
		put_line_in_buffer("***Current NTP Peers***", 2, CENTER);
		for (j = 0; j < 12; j++)
		    put_line_in_buffer("", j + 3, CENTER);
		for (j = 0; j < peer_N_actual; j++) {
		    sprintf(line,
			    "Peer %2d: %s",
			    peer_current[j],
			    peerdata[peer_current[j]].cp_ntp);
		    if (peer_current[j] < 11)
			put_line_in_buffer(line, peer_current[j] + 4, LEFT);
		    else
			put_line_in_buffer(line, peer_current[j] - 7, RIGHTHALF);
		}
		puttext(1, 1, textinfo.screenwidth, textinfo.screenheight, screen_buffer);
	    }
	    else {
		put_status("No Server Replies Received Yet!", 0, CENTER);
	    }
	    break;
	case 68 << 8:                   // F10
	case 'd':
	case 'D':
	    // Enable/Disable
	    if (!fDebug) {
		fDebug = 1;
		modify_menu_desc(9, "Disable~Debug", BLINK_ON);
	    }
	    else {
		fDebug = 0;
		modify_menu_desc(9, "Enable ~Debug", BLINK_OFF);
	    }
	    break;
	case 133 << 8:                  // F11
	case 'l':
	case 'L':
	    // Enable/disable log
	    apiret = DosRequestMutexSem(hmtxLog, SEM_INDEFINITE_WAIT);
	    if (apiret)
	      printf_fatal_status("DosRequestMutexSem failed with error %u", apiret);

	    switch (logState) {
	    case logOff:
		logState = logOpening;
		modify_menu_desc(10, "Close~LogFile", BLINK_ON);
		break;
	    case logOpening:
		logState = logOff;
		modify_menu_desc(10, "Open ~LogFile", BLINK_OFF);
		break;
	    case logOpened:
		logState = logClosing;
		modify_menu_desc(10, "Open ~LogFile", BLINK_OFF);
		break;
	    case logClosing:
		logState = logOpened;
		modify_menu_desc(10, "Close~LogFile", BLINK_ON);
		break;
	    default:
		put_fatal_status("Unexpected logState");
	    } // switch logState

	    apiret = DosReleaseMutexSem(hmtxLog);
	    if (apiret)
	      printf_fatal_status("DosReleaseMutexSem failed with error %u", apiret);
	    break;
	case 134 << 8:                  // F12
	case 'a':
	case 'A':
	    About();
	    break;
	// default:
	    // Ignore unexpected
	    // break;
	} // switch
	apiret = DosReleaseMutexSem(hmtxNTP_Pkt);
	if (apiret)
	  printf_fatal_status("DosReleaseMutexSem failed with error %u", apiret);
    } // for !quit
    clrscr();
    DosExit(EXIT_PROCESS, 0);
}

/**
 * Update on-screen status if not running detached
 * Created by main to offload screen/log updates
 * Writes to log if running detached
 * Writes to log if logging
 * Log opened by thread creator
 * Closes log before terminating
 * Thread terminates after generating output
 */

VOID print_thread(PVOID print_buffer)
{
    print_thread_buffer *prt_buf = (print_thread_buffer *)print_buffer;
    INT i, j;
    APIRET apiret;

    // Log opened by main and closed here
    apiret = DosRequestMutexSem(hmtxLog, SEM_INDEFINITE_WAIT);
    if (apiret)
	printf_fatal_status("DosRequestMutexSem failed with error %u", apiret);

#   if 0
    /* print current ensemble NTP packets */
    for (i = 0; i < peer_N_actual; i++) {
	j = peer_current[i];
	PrintNTP_Pkt(&peerdata[j]);     /* print NTP packet fields, ascii format */
    }
#   endif

    if (!running_detached || logLevel >= 1) {
	if (fabs(ensemble_mean) > 999.999 || sqrt(ensemble_variance) > 999.999) {
	    /* Special characters are code page 437
	       if editing in code page 850:
	       æ = 0xe6 = lowercase Greek mu, å = 0xe5 = lowercase Greek sigma
	    */
	    printf_info_status(0, CENTER,
		"(Server-Client) Ensemble æ = %+.1e, å = %8.1e seconds using %d servers",
		ensemble_mean,
		sqrt(ensemble_variance),
		peer_N_actual);
	}
	else {
	    printf_info_status(0, CENTER,
		"(Server-Client) Ensemble æ = %+8.3f, å = %8.3f seconds using %d servers",
		ensemble_mean,
		sqrt(ensemble_variance),
		peer_N_actual);
	}

	if (fabs(correction) > 999.999) {
	    /* Special characters are code page 437
	       if editing in code page 850:
	       ç = 0xe7 = lowercase Greek tau, ë = 0xeb = lowercase Greek delta
	    */
	    printf_info_status(1, CENTER,
		"Loop Filter Output = %+.1e, Loop ç = %5d, Local Clock ëf/f = %+.1e PPM",
		correction,
		(INT)tau / 8,
		-current_drift.freq_offset * 1e6);
	}
	else {
	    printf_info_status(1, CENTER,
		"Loop Filter Output = %+8.3f, Loop ç = %5d, Local Clock ëf/f = %+.1e PPM",
		correction,
		(INT)tau / 8,
		-current_drift.freq_offset * 1e6);
	}
    } // if must output

    /* write stats to log file if logging */
    if (logState == logOpened) {
	// Timestamp
	fprintf(hLogFile, "\n%.3f", peerdata[peer_current[0]].t_org);
	// Ensemble
	fprintf(hLogFile, "  %+8.3f %8.3f   %+8.3f   %+.3e",
		ensemble_mean, sqrt(ensemble_variance), correction,
		-current_drift.freq_offset);
	/* print stats on all active servers */
	j = 0;
	for (i = 0; i < peer_N; i++) {
	    /* is server current ? */
	    if (i == peer_current[j]) {
		fprintf(hLogFile, "  %+8.3f %+8.3f %8.3f",
			peerdata[i].correction, peerdata[i].mean,
			sqrt(peerdata[i].variance));
		if (j < (peer_N_actual - 1))
		    j++;
	    }
	    else
		fprintf(hLogFile, "  %+8.3f %+8.3f %8.3f",
			peerdata[i].correction, peerdata[i].mean, 0.0);
	}
    } // if logging

    /* write correction info to screen, and log file if turned on */
    switch (prt_buf->adj_rtn) {
    case ADJ_NOT_NEEDED:
	if (logState == logOpened)
	    fprintf(hLogFile, "  %6d", 0);
	// 2014-07-23 SHL less verbose if running detached
	if (!running_detached)
	    put_info_status("No Local Clock Correction Necessary", 2, CENTER);
	break;
    case ADJ_NOT_NOW:
	if (logState == logOpened)
	    fprintf(hLogFile, "  %+8.3f", 0.0);
	put_info_status("Unable to perform Clock Adjustment now, RTC busy", 2, CENTER);
	break;
    case ADJ_BAD_SLEEP:
	if (logState == logOpened)
	    fprintf(hLogFile, "  %+8.3f", 0.0);
	printf_info_status(2, CENTER,
	    "Unable to perform Clock Adjustment now, interrupted--Sleep Error = %d",
	    sleep_error);
	break;
    case ADJ_THREAD_TIMEOUT:
	if (logState == logOpened)
	    fprintf(hLogFile, "  %+8.3f", 0.0);
	put_info_status("Unable to perform Clock Adjustment now, Thread Timed Out",
			2, CENTER);
	break;
    case COARSE_ADJ_FAILURE:
	if (logState == logOpened)
	    fprintf(hLogFile, "  %+8.3f", 0.0);
	printf_info_status(2, CENTER,
	    "Unable to Coarse Adjust Local Clock by %+8.3f seconds",
	    tickadj.correction);
	break;
    case COARSE_ADJ_SUCCESS:
	if (logState == logOpened)
	    fprintf(hLogFile, "  %+8.3f", tickadj.correction);
	printf_info_status(2, CENTER,
	    "Local Clock Coarse Adjusted by %+8.3f seconds", tickadj.correction);
	correction = 0.0;               /* dump integrator */
	break;
    case FINE_ADJ_SUCCESS:
	if (logState == logOpened)
	    fprintf(hLogFile, "  %+8.3f", tickadj.correction);
	if (!running_detached || logLevel >= 1) {
	    printf_info_status(2, CENTER,
		"Local Clock Fine Adjusted by %+8.3f seconds", tickadj.correction);
	}
	correction -= tickadj.correction; /* dump integrator */
	break;
    default:
	break;
    }

    // If logging, close for now - main will reopen next cycle
    if (hLogFile != stdout) {
	fclose(hLogFile);
	hLogFile = stdout;
    }

    apiret = DosReleaseMutexSem(hmtxLog);
    if (apiret)
      printf_fatal_status("DosReleaseMutexSem failed with error %u", apiret);
    apiret = DosPostEventSem(hevPrint); /* let main loop know we are done */
    if (apiret)
      printf_fatal_status("DosPostEventSem failed with error %u", apiret);
    return;                             // end thread
} // print_thread

/**
 * Adjust RTC clock
 * @called by main via adjust_clock
 */

INT ShiftTime(double correction)
{
    APIRET apiret;
    APIRET apiret2;
    DATETIME os2_time;                  /* for DosGetDateTime() */
    time_t newtimesecs;                 /* for localtime() */
    struct tm *tm;                      /* for localtime() */
    struct timeb timeb;                 /* for ftime() */
    double newtime;

    /* keep user interface thread from interfering */
    apiret = DosEnterCritSec();
    if (apiret)
	printf_fatal_status("DosEnterCritSec failed with error %u", apiret);

    ftime(&timeb);                      /* get time with 10 ms resolution */
    newtime = timeb.time + timeb.millitm / 1000.0 + correction;
    newtimesecs = (time_t) (newtime + .5); /* round to seconds */
    /* convert to time structure */
    if (!(tm = localtime(&newtimesecs))) {
	apiret = DosExitCritSec();
	if (apiret)
	    printf_fatal_status("DosExitCritSec failed with error %u", apiret);
	put_info_status("ShiftTime call to localtime() failed!!!", 2, CENTER);
	DosSleep(3000);
	return -1;
    }
    apiret = DosGetDateTime(&os2_time); /* Fill in unknown components */
    if (apiret)
	printf_fatal_status("DosGetDateTime failed with error %u", apiret);

    os2_time.hours = tm->tm_hour;
    os2_time.minutes = tm->tm_min;
    os2_time.seconds = tm->tm_sec;
    os2_time.hundredths = 0;
    os2_time.day = tm->tm_mday;
    os2_time.month = tm->tm_mon + 1;    // 0..11 -> 1..12
    os2_time.year = tm->tm_year + 1900;
    os2_time.weekday = tm->tm_wday;

    apiret2 = DosSetDateTime(&os2_time);
    apiret = DosExitCritSec();
    if (apiret)
	printf_fatal_status("DosExitCritSec failed with error %u", apiret);
    if (apiret) {
	/* should return 0 */
	put_info_status("ShiftTime call to DosSetDateTime failed!!!", 2, CENTER);
	DosSleep(3000);
    }

    return (INT)apiret;
}

VOID do_peer_stats(INT i)
{
    double delta;

    /* update max and min relative to mean */
    delta = peerdata[i].correction - peerdata[i].mean;

    /* test to save suspect timing packets */
    if (fDebug && fabs(delta) > SUSPECT_THRESHOLD)
	save_suspect_pkt(&peerdata[i]);

    if (delta > peerdata[i].max)
	peerdata[i].max = delta;
    else if (delta < peerdata[i].min)
	peerdata[i].min = delta;
    /* exponential filter to estimate mean from i_th peer */
    peerdata[i].mean *= K_STATS;
    peerdata[i].mean += ONE_MINUS_K_STATS * peerdata[i].correction;
    /* exponential filter to estimate mean of squares from i_th peer */
    peerdata[i].sumsqrs *= K_STATS;
    peerdata[i].sumsqrs +=
	ONE_MINUS_K_STATS * peerdata[i].correction * peerdata[i].correction;
    /* calculate variance of i_th peer */
    peerdata[i].variance =
	peerdata[i].sumsqrs - peerdata[i].mean * peerdata[i].mean;
}

/**
 * Calculate ensemble statistics
 */

VOID do_ensemble_stats(char peer_N_actual, double *ensemble_mean,
		       double *ensemble_variance)
{
    INT i;

    /* update statistics for weighting */
    *ensemble_variance = 0.0;
    for (i = 0; i < peer_N_actual; i++) {
	/* sum individual variances */
	*ensemble_variance += 1.0 / peerdata[peer_current[i]].variance;
    }
    /* ensemble variance of weighted mean */
    *ensemble_variance = 1.0 / *ensemble_variance;

    /* clear for next weighted accumulation */
    *ensemble_mean = 0.0;
    for (i = 0; i < peer_N_actual; i++) {
	/* calculate weighted ensemble mean */
	*ensemble_mean += peerdata[peer_current[i]].correction *
			  *ensemble_variance /
			  peerdata[peer_current[i]].variance;
    }
}

/**
 * Apply loop filter
 * @param correction is error integrator
 * @param clock_error is typically mean error or 0 if no server replies
 * @param send_interval is current time constant (2^4 .. 2^17)
 */

VOID LoopFilter(double *correction, double clock_error, INT send_interval)
{
    APIRET apiret;
    static double prev_clock_error, freq_offset;
    static const double tau_dilation = 16.0;

    double k, alpha, delta_T;
    double T = (double)send_interval;
    struct timeb timeb;                 /* for ftime() */

    /* 2nd order type II servo, characteristic eq. = s*s + k*s + k/tau */
    /* k = 2 * dampingratio * omega0, omega0 = sqrt(k/tau) */
    /* so, k = 4 * dampingratio * dampingratio / tau */
    /* alpha = tan (T/(2 * tau)); */
    /* positive freq_offset advances the local clock, so local clock */
    /* frequency is actually negative, or slow. */

    /* determine update time to calculate time since last update */
    NTP_ftime(&timeb);

    if (!fDrift) {
	/* No valid drift known */
	if (clock_error == 0.0) {
	    *correction = 0.0;          /* don't attempt drift correction */
	    return;
	}
	else
	    delta_T = T;                /* normal update interval */
    }
    else {
	/* Have valid drift */
	if (clock_error == 0.0) {
	    /* update based on drift and last_time */
	    delta_T = (double)((ULONG)timeb.time - current_drift.last_time);
	    *correction += delta_T * current_drift.freq_offset;
	    current_drift.last_time = timeb.time;
	    Update_Drift();
	    return;
	}
	else
	    delta_T = T;
    }

    if (!fQuick_Sync) {
	/* Quick_Sync is off, run thru loop filter */
	if (tau < TAU)
	    tau = TAU;                  /* ensure proper initialization of tau */
	else if (T == (double)MAX_POLL_INTERVAL && tau < tau_dilation * TAU)
	    tau *= 1.1;                 /* stretch */
	else if (T < (double)MAX_POLL_INTERVAL && tau > TAU)
	    tau /= 1.1;                 /* contract */

	/* calculate loop parameters */
	k = 4.0 * DAMPING * DAMPING / tau;
	alpha = tan(T / (2.0 * tau));

	/* lead compensated integrator */
	freq_offset += k * (1.0 + alpha) * clock_error +
		       k * (alpha - 1.0) * prev_clock_error;
	*correction += freq_offset * delta_T; /* pure integrator */

	prev_clock_error = clock_error; /* backshift */
	current_drift.last_time = timeb.time;

	/* save local oscillator (LO) phase drift if final tau has been reached */
	if (T == (double)MAX_POLL_INTERVAL && tau >= tau_dilation * TAU) {
	    /* Record local oscillator frequency offset */
	    current_drift.freq_offset = freq_offset;
	    Update_Drift();
	    fDrift = 1;                 /* set flag to show that drift is known */
	}
	/* if drift known, record last time drift applied */
	else if (fDrift)
	    Update_Drift();
    }
    else {
	/* Quick_Sync is on, apply full correction now, don't run thru loop filter */
	*correction = clock_error;
	freq_offset = 0.0;
	prev_clock_error = 0.0;
	tau = TAU;
	apiret = DosRequestMutexSem(hmtxNTP_Pkt, SEM_INDEFINITE_WAIT);
	if (apiret)
	    printf_fatal_status("DosRequestMutexSem failed with error %u", apiret);
	Init_PeerStats();
	apiret = DosReleaseMutexSem(hmtxNTP_Pkt);
	if (apiret)
	    printf_fatal_status("DosReleaseMutexSem failed with error %u", apiret);

	/* if drift known, update last time */
	current_drift.last_time = timeb.time;
	if (fDrift)
	    Update_Drift();
	if (fQuick_Sync != SYNC_FORCED)
	  modify_menu_desc(5, "Quick ~Sync", BLINK_OFF); // Requested by user
	fQuick_Sync = 0;
    }
}

/**
 * Adjust RTC clock
 * @called by main
 */

INT adjust_clock(double *correction)
{
    APIRET apiret;
    TID tidSTss;
    ULONG ulPostCnt;

    if (fabs(*correction) > .5) {
	/* do coarse correction */
	tickadj.correction = *correction; /* make copy for printing */
	if (ShiftTime(tickadj.correction))
	    return COARSE_ADJ_FAILURE;
	else
	    return COARSE_ADJ_SUCCESS;
    }
    else if ((!tickadj.RTC_type && fabs(*correction) > .008) || (tickadj.RTC_type && fabs(*correction) > .024)) {
	/* fine correction  necessary? */
	if (!tickadj.RTC_type && fabs(*correction) < .032) {
	    if (*correction > 0)
		tickadj.correction = .015625;
	    else
		tickadj.correction = -.015625;
	}
	else if (tickadj.RTC_type && fabs(*correction) < .032) {
	    if (*correction > 0)
		tickadj.correction = .046875;
	    else
		tickadj.correction = -.046875;
	}
	else
	    tickadj.correction = *correction;
	tickadj.fSuccess = SHIFT_IN_PROGRESS; /* set return flag to invalid */
	apiret = DosResetEventSem(hevTickAdj, &ulPostCnt); /* make sure is reset */
	// 2014-07-29 SHL FIXME to know how can be already reset
	if (apiret && apiret != ERROR_ALREADY_RESET)
	    printf_fatal_status("DosResetEventSem failed with error %u", apiret);
	tidSTss = _beginthread(ShiftTime_subseconds_thread,
			       NULL,
			       DEFAULT_STKSIZE,
			       &tickadj);
	if (tidSTss == (TID)-1) {
	    printf_fatal_status("Can not start ShiftTime_subseconds thread, error %d",
				errno);
	}
	/* wait 2 seconds for adjustment to be performed */
	apiret = DosWaitEventSem(hevTickAdj, 2000);
	if (apiret == NO_ERROR) {
	    if (tickadj.fSuccess == SHIFT_SUCCESS)
		return FINE_ADJ_SUCCESS;
	    else if (tickadj.fSuccess == SHIFT_BAD_SLEEP)
		return ADJ_BAD_SLEEP;
	    else
		return ADJ_NOT_NOW;
	}
	else if (apiret == ERROR_TIMEOUT) {
	    DosKillThread(tidSTss);     /* must be hung */
	    return ADJ_THREAD_TIMEOUT;
	}
	else {
	    printf_fatal_status("DosWaitEventSem failed with error %u", apiret);
	    return 0;                   // Avoid compile error
	}
    }
    else
	return ADJ_NOT_NEEDED;
}

/**
 * Connect to TCP host
 */

INT connectTCP(PSZ host, PSZ service)
{
    return connectsock(host, service, "tcp");
}

/**
 * Connect to UDP host
 */

INT connectUDP(PSZ host, PSZ service)
{
    return connectsock(host, service, "udp");
}

/**
 * Open socket connection
 */

INT connectsock(PSZ host, PSZ service, PSZ protocol)
{
    struct hostent *phe;                /* pointer to host information entry */
    struct servent *pse;                /* pointer to service information entry */
    struct protoent *ppe;               /* pointer to protocol information entry */
    struct sockaddr_in sin;             /* an Internet endpoint address */
    INT s, type;                        /* socket descriptor and socket type */

    bzero((PCHAR)&sin, sizeof(sin));
    sin.sin_family = AF_INET;

    /* Map service name to port number */
    pse = getservbyname(service, protocol);
    if (pse)
	sin.sin_port = pse->s_port;
    else if ((sin.sin_port = htons((USHORT)atoi(service))) == 0) {
	printf_info_status(0, CENTER,
	   "Can not get \"%s\" service entry", service);
	return -1;
    }

    /* Map host name to IP Address, allowing for dotted decimal */
    phe = gethostbyname(host);
    if (phe)
	bcopy(phe->h_addr, (PCHAR)&sin.sin_addr, phe->h_length);
    else if ((sin.sin_addr.s_addr = inet_addr(host)) == 0xFFFFFFFF) {
	printf_info_status(0, CENTER,
	    "Can not get \"%s\" host entry", host);
	return -1;
    }

    /* Map protocol name to protocol number */
    if ((ppe = getprotobyname(protocol)) == 0) {
	printf_info_status(0, CENTER,
	    "Can not get \"%s\" protocol entry", protocol);
	return -1;
    }

    /* Use protocol to set socket type */
    if (strcmp(protocol, "udp") == 0)
	type = SOCK_DGRAM;
    else
	type = SOCK_STREAM;

    /* Allocate a socket */
    s = socket(PF_INET, type, ppe->p_proto);
    if (s < 0) {
	printf_info_status(0, CENTER,
			   "Can not create socket: %s", service);
	return -1;
    }

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	printf_info_status(0, CENTER,
			   "Can not connect to %s.%s", host, service);
	return -1;
    }
    return s;
}

/**
 * Convert integers from network order to host order
 */

VOID ntoh_ntp(NTP_Pkt *ntp_reply_h, NTP_Pkt *ntp_reply_n)
{
    ntp_reply_h->RootDelay = ntohl(ntp_reply_n->RootDelay);
    ntp_reply_h->RootDisp = ntohl(ntp_reply_n->RootDisp);
    ntp_reply_h->RefTStamp[0] = ntohl(ntp_reply_n->RefTStamp[0]);
    ntp_reply_h->RefTStamp[1] = ntohl(ntp_reply_n->RefTStamp[1]);
    ntp_reply_h->OrgTStamp[0] = ntohl(ntp_reply_n->OrgTStamp[0]);
    ntp_reply_h->OrgTStamp[1] = ntohl(ntp_reply_n->OrgTStamp[1]);
    ntp_reply_h->RxTStamp[0] = ntohl(ntp_reply_n->RxTStamp[0]);
    ntp_reply_h->RxTStamp[1] = ntohl(ntp_reply_n->RxTStamp[1]);
    ntp_reply_h->TxTStamp[0] = ntohl(ntp_reply_n->TxTStamp[0]);
    ntp_reply_h->TxTStamp[1] = ntohl(ntp_reply_n->TxTStamp[1]);
}

/* Print NTP Transaction Results to the CRT */
VOID PrintNTP_Pkt(get_ntp_pkt_buffer * ntp)
{
    char scratch[5] = { 0, 0, 0, 0, 0 };
    char line[81];
    time_t now;

    sprintf(line, "***NTP Reply Packet from Server \"%s\"***", ntp->cp_ntp);
    put_line_in_buffer(line, 2, CENTER);
    put_line_in_buffer("", 3, CENTER);  /* blank line */

    if (ntp->ntp_reply_h.Stratum < 2) {
	sprintf(line,
		"       Ref ID     = %s",
		strncpy(scratch, (PCHAR)ntp->ntp_reply_h.RefID, 4));
    }
    else {
	sprintf(line,
		"       Ref ID     = %u.%u.%u.%u",
		ntp->ntp_reply_h.RefID[0],
		ntp->ntp_reply_h.RefID[1],
		ntp->ntp_reply_h.RefID[2],
		ntp->ntp_reply_h.RefID[3]);
    }
    put_line_in_buffer(line, 4, LEFT);

    sprintf(line,
	    "LI bits    = %d     VN bits = %d     Mode bits  = %2d   Stratum = %d",
	    (ntp->ntp_reply_h.LI_VN_Mode & 0xC0) / 64,
	    (ntp->ntp_reply_h.LI_VN_Mode & 0x38) / 8,
	    (ntp->ntp_reply_h.LI_VN_Mode & 0x07), ntp->ntp_reply_h.Stratum);
    put_line_in_buffer(line, 5, CENTER);

    // 2014-04-17 SHL FIXME to do what?
    sprintf(line, "Poll Int   = %9.5f seconds     Precision  = %f seconds",
	    pow(2.0, (double)ntp->ntp_reply_h.Poll),
	    pow(2.0, (double)ntp->ntp_reply_h.Precision));
    put_line_in_buffer(line, 6, CENTER);

    sprintf(line,
	    "Root Delay = %9.5f seconds     Root Disp  = %f seconds",
	    (double)ntp->ntp_reply_h.RootDelay / 65536.0,
	    (double)ntp->ntp_reply_h.RootDisp / 65536.0);

    put_line_in_buffer(line, 7, CENTER);

#   if 0 // FIXME Bruce debug
    fprintf(hLogFile, "Ref ID bytes are: %X, %X, %X, %X ",
	    ntp->ntp_reply_h.RefID[0],ntp->ntp_reply_h.RefID[1],
	    ntp->ntp_reply_h.RefID[2],ntp->ntp_reply_h.RefID[3]);
#   endif

    now = ntp->ntp_reply_h.RefTStamp[0];
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	sprintf(line,
		"RefTStamp  = %f seconds, %s",
		(double)(ntp->ntp_reply_h.RefTStamp[0] +
			 ntp->ntp_reply_h.RefTStamp[1] / TWO_TO_32),
		ctime(&now));
	line[strlen(line) - 1] = 0x00;  /* get rid of line feed appended by ctime() */
    }
    else
    {
	sprintf(line,
		"RefTStamp  = %f seconds",
		(double)(ntp->ntp_reply_h.RefTStamp[0] +
			 ntp->ntp_reply_h.RefTStamp[1] / TWO_TO_32));
    }
    put_line_in_buffer(line, 8, CENTER);

    now = ntp->ntp_reply_h.OrgTStamp[0];
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	sprintf(line,
		"OrgTStamp  = %f seconds, %s",
		ntp->t_org, ctime(&now));
	line[strlen(line) - 1] = 0x00;  /* get rid of line feed appended by ctime() */
    }
    else
	sprintf(line, "OrgTStamp  = %f seconds", ntp->t_org);
    put_line_in_buffer(line, 9, CENTER);

    now = ntp->ntp_reply_h.RxTStamp[0];
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	sprintf(line, "RxTStamp   = %f seconds, %s", ntp->t_rx, ctime(&now));
	line[strlen(line) - 1] = 0x00;  /* get rid of line feed appended by ctime() */
    }
    else
	sprintf(line, "RxTStamp   = %f seconds", ntp->t_rx);
    put_line_in_buffer(line, 10, CENTER);

    now = ntp->ntp_reply_h.TxTStamp[0];
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	sprintf(line, "TxTStamp   = %f seconds, %s", ntp->t_tx, ctime(&now));
	line[strlen(line) - 1] = 0x00;  /* get rid of line feed appended by ctime() */
    }
    else
	sprintf(line, "TxTStamp   = %f seconds", ntp->t_tx);
    put_line_in_buffer(line, 11, CENTER);

    now = (ULONG)ntp->t_recv;
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	sprintf(line,
		"RecvTStamp = %f seconds, %s", ntp->t_recv,
		ctime(&now));
	line[strlen(line) - 1] = 0x00;  /* get rid of line feed appended by ctime() */
    }
    else
	sprintf(line, "RecvTStamp = %f seconds", ntp->t_recv);
    put_line_in_buffer(line, 12, CENTER);
    put_line_in_buffer("", 13, CENTER); /* blank line */

    if (fabs(ntp->correction) > 999.999 || sqrt(ntp->variance) > 999.999 ||
	fabs(ntp->max) > 999.999 || fabs(ntp->min) > 999.999) {
	sprintf(line,
		"(Server - Client) = %+.1e, å = %8.1e, Max = %+.1e, Min = %+.1e",
		ntp->correction, sqrt(ntp->variance), ntp->max, ntp->min);
    }
    else {
	sprintf(line,
		"(Server - Client) = %+8.3f, å = %8.3f, Max = %+8.3f, Min = %+8.3f",
		ntp->correction, sqrt(ntp->variance), ntp->max, ntp->min);
    }
    put_line_in_buffer(line, 14, CENTER);

    puttext(1, 1, textinfo.screenwidth, textinfo.screenheight, screen_buffer);
}

/**
 * Report configuration file error
 */

VOID report_fatal_cfgfile_error(INT line_number)
{
    report_fatal_error("Configuration file line %d is incorrect", line_number);
}

/**
 * Get NTP timestamp
 * @note relative to 1/1/1900 not 1/1/1970
 */

VOID NTP_ftime(struct timeb *timeb)
{
    ftime(timeb);
    timeb->time += UNIX_EPOCH_SECONDS;
}

/**
 * Identify RTC type
 */

char Identify_RTC(double ensemble_mean)
{
    TID tidSTss;
    static double prev_ensemble_mean;   /* time prior to our adjustment */
    static char fFirst_time_through = 1; /* adjustment not made */

    if (fFirst_time_through) {
	/* haven't made our adjustment yet */
	prev_ensemble_mean = ensemble_mean; /* save time */

	tickadj.correction = .25;       /* adjust clock by 250 ms */
	tickadj.fSuccess = SHIFT_IN_PROGRESS;
	tidSTss = _beginthread(ShiftTime_subseconds_thread,
			       NULL,
			       DEFAULT_STKSIZE,
			       &tickadj);
	if (tidSTss == (TID)-1) {
	    printf_fatal_status("Can not start ShiftTime_subseconds thread, error %d",
				errno);
	}
	DosSleep(2000);
	if (tickadj.fSuccess == SHIFT_SUCCESS)
	    fFirst_time_through = 0;    /* adjustment made */
    }
    /* see if adjustment is within 50 ms of expected value */
    else if ((prev_ensemble_mean - ensemble_mean) > .2
	     && (prev_ensemble_mean - ensemble_mean) < .3) {
	/* save rtc type to hard disk */
	if (!(rtc_type = fopen("rtc_type", "w")))
	    report_fatal_fopen_fail("rtc_type");
	fprintf(rtc_type, "%d", tickadj.RTC_type);
	fclose(rtc_type);
	fRTC_Identified = 1;
	put_info_status(tickadj.RTC_type == 0 ?
			  "Real Time Clock Identified is IBM AT-Standard" :
			  "Real Time Clock Identified is NOT IBM AT-Standard",
			0, CENTER);
	DosSleep(2000);
    }
    else {
	/* toggle RTC_type and try again */
	prev_ensemble_mean = ensemble_mean;
	fFirst_time_through = 1;
	if (!tickadj.RTC_type)
	    tickadj.RTC_type = 500;
	else
	    tickadj.RTC_type = 0;
    }
    return 0;
}

/**
 * Save Suspect Timing NTP Packets to the suspects file
 */

VOID save_suspect_pkt(get_ntp_pkt_buffer * ntp)
{
    char scratch[5] = { 0, 0, 0, 0, 0 };
    time_t now;

    if (!(suspect_pkt = fopen("suspects", "a")))
	report_fatal_fopen_fail("suspects");

    fprintf(suspect_pkt, "***NTP Reply Packet from Server \"%s\"***\n\n",
	    ntp->cp_ntp);

    if (ntp->ntp_reply_h.Stratum < 2)
	fprintf(suspect_pkt, "Ref ID     = %s\n",
		strncpy(scratch, (PCHAR)ntp->ntp_reply_h.RefID, 4));
    else
	fprintf(suspect_pkt, "Ref ID     = %u.%u.%u.%u\n",
		ntp->ntp_reply_h.RefID[0], ntp->ntp_reply_h.RefID[1],
		ntp->ntp_reply_h.RefID[2], ntp->ntp_reply_h.RefID[3]);

    fprintf(suspect_pkt,
	    "LI bits    = %d     VN bits = %d     Mode bits  = %2d   Stratum = %d\n",
	    (ntp->ntp_reply_h.LI_VN_Mode & 0xC0) / 64,
	    (ntp->ntp_reply_h.LI_VN_Mode & 0x38) / 8,
	    (ntp->ntp_reply_h.LI_VN_Mode & 0x07), ntp->ntp_reply_h.Stratum);
    fprintf(suspect_pkt,
	    "Poll Int   = %9.5f seconds     Precision  = %f seconds\n",
	    pow(2.0, ntp->ntp_reply_h.Poll), pow(2.0,
						 ntp->ntp_reply_h.Precision));
    fprintf(suspect_pkt,
	    "Root Delay = %9.5f seconds     Root Disp  = %f seconds\n",
	    ntp->ntp_reply_h.RootDelay / 65536.0,
	    ntp->ntp_reply_h.RootDisp / 65536.0);

    now = ntp->ntp_reply_h.RefTStamp[0];
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	fprintf(suspect_pkt, "RefTStamp  = %f seconds, %s",
		(double)(ntp->ntp_reply_h.RefTStamp[0] +
			 ntp->ntp_reply_h.RefTStamp[1] / TWO_TO_32),
		ctime(&now));
    }
    else
	fprintf(suspect_pkt, "RefTStamp  = %f seconds\n",
		(double)(ntp->ntp_reply_h.RefTStamp[0] +
			 ntp->ntp_reply_h.RefTStamp[1] / TWO_TO_32));

    now = ntp->ntp_reply_h.OrgTStamp[0];
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	fprintf(suspect_pkt, "OrgTStamp  = %f seconds, %s", ntp->t_org,
		ctime(&now));
    }
    else
	fprintf(suspect_pkt, "OrgTStamp  = %f seconds\n", ntp->t_org);

    now = ntp->ntp_reply_h.RxTStamp[0];
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	fprintf(suspect_pkt, "RxTStamp   = %f seconds, %s", ntp->t_rx,
		ctime(&now));
    }
    else
	fprintf(suspect_pkt, "RxTStamp   = %f seconds\n", ntp->t_rx);

    now = ntp->ntp_reply_h.TxTStamp[0];
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	fprintf(suspect_pkt, "TxTStamp   = %f seconds, %s", ntp->t_tx,
		ctime(&now));
    }
    else
	fprintf(suspect_pkt, "TxTStamp   = %f seconds\n", ntp->t_tx);

    now = (ULONG)ntp->t_recv;
    if (now >= UNIX_EPOCH_SECONDS) {
	now -= UNIX_EPOCH_SECONDS;
	fprintf(suspect_pkt, "RecvTStamp = %f seconds, %s", ntp->t_recv,
		ctime(&now));
    }
    else
	fprintf(suspect_pkt, "RecvTStamp = %f seconds\n", ntp->t_recv);

    fprintf(suspect_pkt,
	    "(Server - Client) = %+8.3f, å = %8.3f, Max = %+8.3f, Min = %+8.3f\n\n\n",
	    ntp->correction, sqrt(ntp->variance), ntp->max, ntp->min);

    fclose(suspect_pkt);
}

/**
 * Initialize peer statistics
 */

VOID Init_PeerStats(VOID)
{
    INT i;

    /* initialize variance and sumsqrs to small non-zero values
       so that loop filter does not coast on 1st call
       2014-05-17 SHL FIXME to be sure this is why
     */

    for (i = 0; i < peer_N; i++) {
	peerdata[i].variance = .00025;
	peerdata[i].mean = 0.0;
	peerdata[i].max = 0.0;
	peerdata[i].min = 0.0;
	peerdata[i].sumsqrs = .00025;
    }
}

/**
 * Update drift file
 */

VOID Update_Drift(VOID)
{
    if (!(drift = fopen("drift", "w")))
	report_fatal_fopen_fail("drift");
    else {
	fprintf(drift, "%u,%lf\n", current_drift.last_time,
		-current_drift.freq_offset);
	fclose(drift);
    }
}

/**
 * Display about info
 */

VOID About(VOID)
{
    put_line_in_buffer(title_string, 2, CENTER);
    put_line_in_buffer("", 3, CENTER);
    put_line_in_buffer("written by", 4, CENTER);
    put_line_in_buffer("", 5, CENTER);
    put_line_in_buffer(AUTHOR, 6, CENTER);
    put_line_in_buffer("", 7, CENTER);

    // 2014-04-16 SHL Sync with 6/21/98 binary
    // put_line_in_buffer(COMPANY, 8, CENTER);
    // put_line_in_buffer(ADDRESS1, 9, CENTER);
    // put_line_in_buffer(ADDRESS2, 10, CENTER);

    put_line_in_buffer("maintained by", 8, CENTER);
    put_line_in_buffer("", 9, CENTER);
    put_line_in_buffer(MAINTAINER, 10, CENTER);

    put_line_in_buffer("", 11, CENTER);
    put_line_in_buffer(WEBSITE, 12, CENTER);
    put_line_in_buffer("", 13, CENTER);
    // put_line_in_buffer(REGISTRATION, 14, CENTER);

    puttext(1, 1, textinfo.screenwidth, textinfo.screenheight, screen_buffer);
}

/**
 * Open log file
 */

BOOL log_open(BOOL return_on_error)
{
    BOOL ok;
    struct timeb timeb;
    INT c;
    NTP_ftime(&timeb);
    if (!*szLogFileName) {
	time_t now;
	struct tm *tm;
	PSZ psz;
	time(&now);
        // 2014-12-29 SHL Support LOGFILES
	psz = getenv("LOGFILES");
	if (!psz)
	    psz = getenv("TMP");
	if (!psz)
	    psz = getenv("TEMP");
	strcpy(szLogFileName, psz);
	c = strlen(szLogFileName);
	if (c > 0 && szLogFileName[c - 1] != '\\')
	    szLogFileName[c++] = '\\';
	tm = localtime(&now);
	sprintf(szLogFileName + c,
		"os2_ntpd-%u%02u%02u-%02u%02u.log",
		 tm->tm_year + 1900,
		 tm->tm_mon + 1,        // 0..11
		 tm->tm_mday,
		 tm->tm_hour,
		 tm->tm_min);
    }
    hLogFile = fopen(szLogFileName, "a");
    ok = hLogFile != NULL;
    if (!ok) {
	hLogFile = stdout;
	if (!return_on_error)
	    report_fatal_fopen_fail(szLogFileName);
    }
    return ok;
}

/**
 * Close log file
 */

VOID log_close()
{
    fclose(hLogFile);
    hLogFile = stdout;
}

/**
 * Output put_status message at selected line and die or
 * to log file if running detached
 */

VOID put_fatal_status(PCSZ status_line)
{
    char msg[81];
    sprintf(msg, "%.58s, press a key to exit", status_line);

    if (running_detached)
	fprintf(hLogFile, "%s\n", msg);
    else
	put_status(msg, 0, CENTER);
    report_fatal_error(NULL);
}

/**
 * format info status message for put_info_status
 * @param pszFmt is format string
 */


VOID printf_info_status(UINT line_number, INT just, PCSZ pszFmt, ...)
{
    va_list pVA;
    char status_line[81];

    va_start(pVA, pszFmt);
    vsprintf(status_line, pszFmt, pVA);

    put_info_status(status_line, line_number, just);
}

/**
 * format fatal status message for put_fatal_status
 * @param pszFmt is format string
 * @note does not return
 */

VOID printf_fatal_status(PCSZ pszFmt, ...)
{
    va_list pVA;
    char status_line[81];

    va_start(pVA, pszFmt);
    vsprintf(status_line, pszFmt, pVA);

    put_fatal_status(status_line);
}

/**
 * Output put_status message at selected line or
 * to log file if running detached
 */

VOID put_info_status(PCSZ status_line, UINT line_number, INT just)
{
    if (running_detached) {
	time_t now;
	static time_t last_now;
	static CHAR timestamp[22];
	time(&now);
	if (now != last_now) {
	  struct tm *tm = localtime(&now);
	  last_now = now;
	  sprintf(timestamp,
		  "%u/%02u/%02u %02u:%02u:%02u: ",
		 tm->tm_year + 1900,
		 tm->tm_mon + 1,        // 0..11
		 tm->tm_mday,
		 tm->tm_hour,
		 tm->tm_min,
		 tm->tm_sec);
	}
	fputs(timestamp, hLogFile);
	fputs(status_line, hLogFile);
	fputc('\n', hLogFile);
	fflush(hLogFile);
    }
    else
	put_status(status_line, line_number, just);
}

/**
 * Report file open failure
 */

VOID report_fatal_fopen_fail(PSZ filename)
{
    report_fatal_error("Can not option open %s, error %u", filename, errno);
}

/**
 * Report fatal error to console at current row/column
 * @param pszFmt is format string or NULL if all output already on screen or
 *        empty string if want press a key message
 * @note does not return
 */

VOID report_fatal_error(char* pszFmt, ...)
{
    va_list pVA;

    if (pszFmt && *pszFmt) {
	va_start(pVA, pszFmt);
	vprintf(pszFmt, pVA);
	putchar('\n');
    }

    if (!running_detached) {
	if (pszFmt)
	    printf("Press a key to exit\n");
	getch();
    }
    exit(1);
}


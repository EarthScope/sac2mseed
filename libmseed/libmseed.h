
/***************************************************************************
 * libmseed.h:
 * 
 * Interface declarations for the Mini-SEED library (libmseed).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License (GNU-LGPL) for more details.  The
 * GNU-LGPL and further information can be found here:
 * http://www.gnu.org/
 *
 * Written by Chad Trabant
 * IRIS Data Management Center
 ***************************************************************************/


#ifndef LIBMSEED_H
#define LIBMSEED_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "lmplatform.h"

#define LIBMSEED_VERSION "1.8"
#define LIBMSEED_RELEASE "2006.208"

#define MINRECLEN   256      /* Minimum Mini-SEED record length, 2^8 bytes */
#define MAXRECLEN   1048576  /* Maximum Mini-SEED record length, 2^20 bytes */

/* SEED data encoding types */
#define ASCII      0
#define INT16      1
#define INT32      3
#define FLOAT32    4
#define FLOAT64    5
#define STEIM1     10
#define STEIM2     11

/* Library return and error code values, error values should always be negative */
#define MS_ENDOFFILE        1        /* End of file reached return value */
#define MS_NOERROR          0        /* No error */
#define MS_GENERROR        -1        /* Generic unspecified error */
#define MS_NOTSEED         -2        /* Data not SEED */
#define MS_WRONGLENGTH     -3        /* Length of data read was not correct */
#define MS_OUTOFRANGE      -4        /* SEED record length out of range */
#define MS_UNKNOWNFORMAT   -5        /* Unknown data encoding format */
#define MS_STBADCOMPFLAG   -6        /* Steim, invalid compression flag(s) */

/* Define the high precision time tick interval as 1/modulus seconds */
#define HPTMODULUS 1000000

/* Error code for routines that normally return a high precision time.
 * The time value corresponds to '1902/1/1 00:00:00.000000' with the
 * default HPTMODULUS */
#define HPTERROR -2145916800000000LL

/* Macros to scale between Unix/POSIX epoch time & high precision time */
#define MS_EPOCH2HPTIME(X) X * (hptime_t) HPTMODULUS
#define MS_HPTIME2EPOCH(X) X / HPTMODULUS

/* Macro to test a character for data record indicators */
#define MS_ISDATAINDICATOR(X) (X=='D' || X=='R' || X=='Q')

/* Macro to test default sample rate tolerance: abs(1-sr1/sr2) < 0.0001 */
#define MS_ISRATETOLERABLE(A,B) (ms_dabs (1.0 - (A / B)) < 0.0001)

/* Require a large (>= 64-bit) integer type for hptime_t */
typedef int64_t hptime_t;

/* A single byte flag type */
typedef int8_t flag;

/* SEED binary time */
typedef struct btime_s
{
  uint16_t  year;
  uint16_t  day;
  uint8_t   hour;
  uint8_t   min;
  uint8_t   sec;
  uint8_t   unused;
  uint16_t  fract;
}
BTime;

/* Fixed section data of header */
struct fsdh_s
{
  char           sequence_number[6];
  char           dataquality;
  char           reserved;
  char           station[5];
  char           location[2];
  char           channel[3];
  char           network[2];
  BTime          start_time;
  uint16_t       numsamples;
  int16_t        samprate_fact;
  int16_t        samprate_mult;
  uint8_t        act_flags;
  uint8_t        io_flags;
  uint8_t        dq_flags;
  uint8_t        numblockettes;
  int32_t        time_correct;
  uint16_t       data_offset;
  uint16_t       blockette_offset;
};

/* Blockette 100, Sample Rate (without header) */
struct blkt_100_s
{
  float     samprate;
  int8_t    flags;
  uint8_t   reserved[3];
};

/* Blockette 200, Generic Event Detection (without header) */
struct blkt_200_s
{
  float     amplitude;
  float     period;
  float     background_estimate;
  uint8_t   flags;
  uint8_t   reserved;
  BTime     time;
  char      detector[24];
};

/* Blockette 201, Murdock Event Detection (without header) */
struct blkt_201_s
{
  float     amplitude;
  float     period;
  float     background_estimate;
  uint8_t   flags;
  uint8_t   reserved;
  BTime     time;
  uint8_t   snr_values[6];
  uint8_t   loopback;
  uint8_t   pick_algorithm;
  char      detector[24];
};

/* Blockette 300, Step Calibration (without header) */
struct blkt_300_s
{
  BTime     time;
  uint8_t   numcalibrations;
  uint8_t   flags;
  uint32_t  step_duration;
  uint32_t  interval_duration;
  float     amplitude;
  char      input_channel[3];
  uint8_t   reserved;
  uint32_t  reference_amplitude;
  char      coupling[12];
  char      rolloff[12];
};

/* Blockette 310, Sine Calibration (without header) */
struct blkt_310_s
{
  BTime     time;
  uint8_t   reserved1;
  uint8_t   flags;
  uint32_t  duration;
  float     period;
  float     amplitude;
  char      input_channel[3];
  uint8_t   reserved2;
  uint32_t  reference_amplitude;
  char      coupling[12];
  char      rolloff[12];
};

/* Blockette 320, Pseudo-random Calibration (without header) */
struct blkt_320_s
{
  BTime     time;
  uint8_t   reserved1;
  uint8_t   flags;
  uint32_t  duration;
  float     ptp_amplitude;
  char      input_channel[3];
  uint8_t   reserved2;
  uint32_t  reference_amplitude;
  char      coupling[12];
  char      rolloff[12];
  char      noise_type[8];
};
  
/* Blockette 390, Generic Calibration (without header) */
struct blkt_390_s
{
  BTime     time;
  uint8_t   reserved1;
  uint8_t   flags;
  uint32_t  duration;
  float     amplitude;
  char      input_channel[3];
  uint8_t   reserved2;
};

/* Blockette 395, Calibration Abort (without header) */
struct blkt_395_s
{
  BTime     time;
  uint8_t   reserved[2];
};

/* Blockette 400, Beam (without header) */
struct blkt_400_s
{
  float     azimuth;
  float     slowness;
  uint16_t  configuration;
  uint8_t   reserved[2];
};

/* Blockette 405, Beam Delay (without header) */
struct blkt_405_s
{
  uint16_t  delay_values[1];
};

/* Blockette 500, Timing (without header) */
struct blkt_500_s
{
  float     vco_correction;
  BTime     time;
  int8_t    usec;
  uint8_t   reception_qual;
  uint32_t  exception_count;
  char      exception_type[16];
  char      clock_model[32];
  char      clock_status[128];
};

/* Blockette 1000, Data Only SEED (without header) */
struct blkt_1000_s
{
  uint8_t   encoding;
  uint8_t   byteorder;
  uint8_t   reclen;
  uint8_t   reserved;
};

/* Blockette 1001, Data Extension (without header) */
struct blkt_1001_s
{
  uint8_t   timing_qual;
  int8_t    usec;
  uint8_t   reserved;
  uint8_t   framecnt;
};

/* Blockette 2000, Opaque Data (without header) */
struct blkt_2000_s
{
  uint16_t  length;
  uint16_t  data_offset;
  uint32_t  recnum;
  uint8_t   byteorder;
  uint8_t   flags;
  uint8_t   numheaders;
  char      payload[1];
};

/* Blockette chain link, generic linkable blockette index */
typedef struct blkt_link_s
{
  uint16_t            blkt_type;     /* Blockette type */
  uint16_t            next_blkt;     /* Offset to next blockette */
  void               *blktdata;      /* Blockette data */
  uint16_t            blktdatalen;   /* Length of blockette data in bytes */
  struct blkt_link_s *next;
}
BlktLink;

typedef struct MSRecord_s {
  char           *record;            /* Mini-SEED record */
  int32_t         reclen;            /* Length of Mini-SEED record in bytes */
  
  /* Pointers to SEED data record structures */
  struct fsdh_s      *fsdh;          /* Fixed Section of Data Header */
  BlktLink           *blkts;         /* Root of blockette chain */
  struct blkt_100_s  *Blkt100;       /* Blockette 100, if present */
  struct blkt_1000_s *Blkt1000;      /* Blockette 1000, if present */
  struct blkt_1001_s *Blkt1001;      /* Blockette 1001, if present */
  
  /* Common header fields in accessible form */
  int32_t         sequence_number;   /* SEED record sequence number */
  char            network[11];       /* Network designation, NULL terminated */
  char            station[11];       /* Station designation, NULL terminated */
  char            location[11];      /* Location designation, NULL terminated */
  char            channel[11];       /* Channel designation, NULL terminated */
  char            dataquality;       /* Data quality indicator */
  hptime_t        starttime;         /* Record start time, corrected (first sample) */
  double          samprate;          /* Nominal sample rate (Hz) */
  int32_t         samplecnt;         /* Number of samples in record */
  int8_t          encoding;          /* Data encoding format */
  int8_t          byteorder;         /* Orignal/Final byte order of record */
  
  /* Data sample fields */
  void           *datasamples;       /* Data samples, 'numsamples' of type 'sampletype'*/
  int32_t         numsamples;        /* Number of data samples in datasamples */
  char            sampletype;        /* Sample type code: a, i, f, d */
}
MSRecord;

/* Container for a continuous trace, linkable */
typedef struct MSTrace_s {
  char            network[11];       /* Network designation, NULL terminated */
  char            station[11];       /* Station designation, NULL terminated */
  char            location[11];      /* Location designation, NULL terminated */
  char            channel[11];       /* Channel designation, NULL terminated */
  char            dataquality;       /* Data quality indicator */ 
  char            type;              /* MSTrace type code */
  hptime_t        starttime;         /* Time of first sample */
  hptime_t        endtime;           /* Time of last sample */
  double          samprate;          /* Nominal sample rate (Hz) */
  int32_t         samplecnt;         /* Number of samples in trace coverage */
  void           *datasamples;       /* Data samples, 'numsamples' of type 'sampletype'*/
  int32_t         numsamples;        /* Number of data samples in datasamples */
  char            sampletype;        /* Sample type code: a, i, f, d */
  void           *private;           /* Private pointer for general use, unused by libmseed */
  struct MSTrace_s *next;            /* Pointer to next trace */
}
MSTrace;

/* Container for a group (chain) of traces */
typedef struct MSTraceGroup_s {
  int32_t           numtraces;     /* Number of MSTraces in the trace chain */
  struct MSTrace_s *traces;        /* Root of the trace chain */
}
MSTraceGroup;

/* Mini-SEED record related functions */
extern int          msr_unpack (char *record, int reclen, MSRecord **ppmsr,
				flag dataflag, flag verbose);

extern int          msr_pack (MSRecord *msr, void (*record_handler) (char *, int),
			      int *packedsamples, flag flush, flag verbose );

extern int          msr_pack_header (MSRecord *msr, flag verbose);

extern MSRecord*    msr_init (MSRecord *msr);
extern void         msr_free (MSRecord **ppmsr);
extern void         msr_free_blktchain (MSRecord *msr);
extern BlktLink*    msr_addblockette (MSRecord *msr, char *blktdata, int length,
                                      int blkttype, int chainpos);
extern double       msr_samprate (MSRecord *msr);
extern double       msr_nomsamprate (MSRecord *msr);
extern hptime_t     msr_starttime (MSRecord *msr);
extern hptime_t     msr_starttime_uc (MSRecord *msr);
extern hptime_t     msr_endtime (MSRecord *msr);
extern char*        msr_srcname (MSRecord *msr, char *srcname);
extern void         msr_print (MSRecord *msr, flag details);
extern double       msr_host_latency (MSRecord *msr);


/* MSTrace related functions */
extern MSTrace*     mst_init (MSTrace *mst);
extern void         mst_free (MSTrace **ppmst);
extern MSTraceGroup*  mst_initgroup (MSTraceGroup *mstg);
extern void         mst_freegroup (MSTraceGroup **ppmstg);
extern MSTrace*     mst_findmatch (MSTrace *startmst, char dataquality,
				   char *network, char *station, char *location, char *channel);
extern MSTrace*     mst_findadjacent (MSTraceGroup *mstg, flag *whence, char dataquality,
				      char *network, char *station, char *location, char *channel,
				      double samprate, double sampratetol,
				      hptime_t starttime, hptime_t endtime, double timetol);
extern int          mst_addmsr (MSTrace *mst, MSRecord *msr, flag whence);
extern int          mst_addspan (MSTrace *mst, hptime_t starttime,  hptime_t endtime,
				 void *datasamples, int numsamples,
				 char sampletype, flag whence);
extern MSTrace*     mst_addmsrtogroup (MSTraceGroup *mstg, MSRecord *msr, flag dataquality,
				       double timetol, double sampratetol);
extern MSTrace*     mst_addtracetogroup (MSTraceGroup *mstg, MSTrace *mst);
extern int          mst_groupheal (MSTraceGroup *mstg, double timetol, double sampratetol);
extern int          mst_groupsort (MSTraceGroup *mstg);
extern char *       mst_srcname (MSTrace *mst, char *srcname);
extern void         mst_printtracelist (MSTraceGroup *mstg, flag timeformat,
					flag details, flag gaps);
extern void         mst_printgaplist (MSTraceGroup *mstg, flag timeformat,
				      double *mingap, double *maxgap);
extern int          mst_pack (MSTrace *mst, void (*record_handler) (char *, int),
			      int reclen, flag encoding, flag byteorder,
			      int *packedsamples, flag flush, flag verbose,
			      MSRecord *mstemplate);
extern int          mst_packgroup (MSTraceGroup *mstg, void (*record_handler) (char *, int),
				   int reclen, flag encoding, flag byteorder,
				   int *packedsamples, flag flush, flag verbose,
				   MSRecord *mstemplate);


/* Reading Mini-SEED records from files */
extern int          ms_readmsr (MSRecord **ppmsr, char *msfile, int reclen, off_t *fpos, int *last,
				flag skipnotdata, flag dataflag, flag verbose);
extern int          ms_readtraces (MSTraceGroup **ppmstg, char *msfile, int reclen, double timetol, double sampratetol,
				     flag dataquality, flag skipnotdata, flag dataflag, flag verbose);
extern int            ms_find_reclen (const char *recbuf, int recbuflen, FILE *fileptr);


/* General use functions */
extern int      ms_verify_header (struct fsdh_s *fsdh);
extern int      ms_strncpclean (char *dest, const char *source, int length);
extern int      ms_strncpopen (char *dest, const char *source, int length);
extern int      ms_doy2md (int year, int jday, int *month, int *mday);
extern int      ms_md2doy (int year, int month, int mday, int *jday);
extern hptime_t ms_btime2hptime (BTime *btime);
extern char*    ms_btime2isotimestr (BTime *btime, char *isotimestr);
extern char*    ms_btime2seedtimestr (BTime *btime, char *seedtimestr);
extern int      ms_hptime2btime (hptime_t hptime, BTime *btime);
extern char*    ms_hptime2isotimestr (hptime_t hptime, char *isotimestr);
extern char*    ms_hptime2seedtimestr (hptime_t hptime, char *seedtimestr);
extern hptime_t ms_time2hptime (int year, int day, int hour, int min, int sec, int usec);
extern hptime_t ms_seedtimestr2hptime (char *seedtimestr);
extern hptime_t ms_timestr2hptime (char *timestr);
extern int      ms_genfactmult (double samprate, int16_t *factor, int16_t *multiplier);
extern int      ms_ratapprox (double real, int *num, int *den, int maxval, double precision);
extern int      ms_bigendianhost ();
extern double   ms_dabs (double val);

/* Lookup functions */
extern uint8_t   get_samplesize (const char sampletype);
extern char     *get_encoding (const char encoding);
extern char     *get_blktdesc (uint16_t blkttype);
extern uint16_t  get_blktlen (uint16_t blkttype, const char *blktdata, flag swapflag);
extern char *    get_errorstr (int errorcode);


/* Generic byte swapping routines */
extern void   gswap2 ( void *data2 );
extern void   gswap3 ( void *data3 );
extern void   gswap4 ( void *data4 );
extern void   gswap8 ( void *data8 );

/* Generic byte swapping routines for memory aligned quantities */
extern void   gswap2a ( void *data2 );
extern void   gswap4a ( void *data4 );
extern void   gswap8a ( void *data8 );

/* Byte swap macro for the BTime struct */
#define SWAPBTIME(x) \
  gswap2 (x.year);   \
  gswap2 (x.day);    \
  gswap2 (x.fract);


#ifdef __cplusplus
}
#endif

#endif /* LIBMSEED_H */

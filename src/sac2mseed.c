/***************************************************************************
 * sac2mseed.c
 *
 * Simple waveform data conversion from SAC timeseries to Mini-SEED.
 * No support is included for SAC spectral or generic X-Y data.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified 2007.284
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include <libmseed.h>

#include "sacformat.h"

#define VERSION "1.5"
#define PACKAGE "sac2mseed"

struct listnode {
  char *key;
  char *data;
  struct listnode *next;
};

static void packtraces (flag flush);
static int sac2group (char *sacfile, MSTraceGroup *mstg);
static int parsesac (FILE *ifp, struct SACHeader *sh, float **data, int format,
		     int verbose, char *sacfile);
static int readbinaryheader (FILE *ifp, struct SACHeader *sh, int *format,
			     int *swapflag, int verbose, char *sacfile);
static int readbinarydata (FILE *ifp, float *data, int datacnt,
			   int swapflag, int verbose, char *sacfile);
static int readalphaheader (FILE *ifp, struct SACHeader *sh);
static int readalphadata (FILE *ifp, float *data, int datacnt);
static int swapsacheader (struct SACHeader *sh);
static int writemetadata (struct SACHeader *sh);
static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static int readlistfile (char *listfile);
static void addnode (struct listnode **listroot, char *key, char *data);
static void record_handler (char *record, int reclen);
static void usage (void);

static int   verbose     = 0;
static int   packreclen  = -1;
static int   encoding    = 11;
static int   byteorder   = -1;
static int   sacformat   = 0;
static char  srateblkt   = 0;
static char *forcenet    = 0;
static char *forceloc    = 0;
static char *outputfile  = 0;
static FILE *ofp         = 0;
static char *metafile    = 0;
static FILE *mfp         = 0;
static long long int datascaling = 0;

/* A list of input files */
struct listnode *filelist = 0;

static MSTraceGroup *mstg = 0;

static int packedtraces  = 0;
static int packedsamples = 0;
static int packedrecords = 0;

int
main (int argc, char **argv)
{
  struct listnode *flp;
  
  /* Process given parameters (command line and parameter file) */
  if (parameter_proc (argc, argv) < 0)
    return -1;
  
  /* Init MSTraceGroup */
  mstg = mst_initgroup (mstg);
  
  /* Open the output file if specified */
  if ( outputfile )
    {
      if ( strcmp (outputfile, "-") == 0 )
        {
          ofp = stdout;
        }
      else if ( (ofp = fopen (outputfile, "wb")) == NULL )
        {
          fprintf (stderr, "Cannot open output file: %s (%s)\n",
                   outputfile, strerror(errno));
          return -1;
        }
    }
  
  /* Open the metadata output file if specified */
  if ( metafile )
    {
      if ( strcmp (metafile, "-") == 0 )
        {
          mfp = stdout;
        }
      else if ( (mfp = fopen (metafile, "wb")) == NULL )
        {
          fprintf (stderr, "Cannot open metadata output file: %s (%s)\n",
                   metafile, strerror(errno));
          return -1;
        }
    }
  
  /* Read input SAC files into MSTraceGroup */
  flp = filelist;
  while ( flp != 0 )
    {
      if ( verbose )
	fprintf (stderr, "Reading %s\n", flp->data);

      sac2group (flp->data, mstg);
      
      flp = flp->next;
    }
  
  fprintf (stderr, "Packed %d trace(s) of %d samples into %d records\n",
	   packedtraces, packedsamples, packedrecords);
  
  /* Make sure everything is cleaned up */
  if ( ofp )
    fclose (ofp);
  
  if ( mfp )
    fclose (mfp);
  
  return 0;
}  /* End of main() */


/***************************************************************************
 * packtraces:
 *
 * Pack all traces in a group using per-MSTrace templates.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
packtraces (flag flush)
{
  MSTrace *mst;
  int trpackedsamples = 0;
  int trpackedrecords = 0;
  
  mst = mstg->traces;
  while ( mst )
    {
      
      if ( mst->numsamples <= 0 )
	{
	  mst = mst->next;
	  continue;
	}
      
      trpackedrecords = mst_pack (mst, &record_handler, packreclen, encoding, byteorder,
				  &trpackedsamples, flush, verbose-2, (MSRecord *) mst->private);
      
      if ( trpackedrecords < 0 )
	{
	  fprintf (stderr, "Error packing data\n");
	}
      else
	{
	  packedrecords += trpackedrecords;
	  packedsamples += trpackedsamples;
	}
      
      mst = mst->next;
    }
}  /* End of packtraces() */


/***************************************************************************
 * sac2group:
 * Read a SAC file and add data samples to a MSTraceGroup.  As the SAC
 * data is read in a MSRecord struct is used as a holder for the input
 * information.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static int
sac2group (char *sacfile, MSTraceGroup *mstg)
{
  FILE *ifp = 0;
  MSRecord *msr = 0;
  MSTrace *mst;
  struct blkt_100_s Blkt100;
  
  struct SACHeader sh;
  float *fdata = 0;
  int32_t *idata = 0;
  int dataidx;
  int datacnt;
  long long int scaling = datascaling;
  
  /* Open input file */
  if ( (ifp = fopen (sacfile, "rb")) == NULL )
    {
      fprintf (stderr, "Cannot open input file: %s (%s)\n",
	       sacfile, strerror(errno));
      return -1;
    }
  
  /* Parse input SAC file into a header structure and data buffer */
  if ( (datacnt = parsesac (ifp, &sh, &fdata, sacformat, verbose, sacfile)) < 0 )
    {
      fprintf (stderr, "Error parsing %s\n", sacfile);
      
      return -1;
    }

  /* Write metadata to file if requested */
  if ( mfp )
    {
      if ( verbose )
	fprintf (stderr, "[%s] Writing metadata to %s\n", sacfile, metafile);
      
      if ( writemetadata (&sh) )
	{
	  fprintf (stderr, "Error writing metadata to file '%s'\n", metafile);
	  
	  return -1;
	}
    }
  
  /* Open output file if needed */
  if ( ! ofp )
    {
      char mseedoutputfile[1024];
      int namelen;
      strncpy (mseedoutputfile, sacfile, sizeof(mseedoutputfile)-6 );
      namelen = strlen (sacfile);
      
      /* Truncate file name if .sac is at the end */
      if ( namelen > 4 )
	if ( (*(mseedoutputfile + namelen - 1) == 'c' || *(mseedoutputfile + namelen - 1) == 'C') &&
             (*(mseedoutputfile + namelen - 2) == 'a' || *(mseedoutputfile + namelen - 2) == 'A') &&
             (*(mseedoutputfile + namelen - 3) == 's' || *(mseedoutputfile + namelen - 3) == 'S') &&
             (*(mseedoutputfile + namelen - 4) == '.') )
	  
	  {
	    *(mseedoutputfile + namelen - 4) = '\0';
	  }
      
      /* Add .mseed to the file name */
      strcat (mseedoutputfile, ".mseed");
      
      if ( (ofp = fopen (mseedoutputfile, "wb")) == NULL )
        {
          fprintf (stderr, "Cannot open output file: %s (%s)\n",
                   mseedoutputfile, strerror(errno));
          return -1;
        }
    }
  
  if ( ! (msr = msr_init(msr)) )
    {
      fprintf (stderr, "Cannot initialize MSRecord strcture\n");
      return -1;
    }
  
  /* Determine autoscaling */
  if ( scaling == 0 && encoding != 4 )
    {
      float datamin, datamax;
      int fractional;
      long long int autoscale;
      
      /* Determine data sample minimum and maximum
       * Detect if scaling by 1 will result in truncation (fractional=1) */
      datamin = datamax = *fdata;
      fractional = 0;
      for ( dataidx=1; dataidx < datacnt; dataidx++ )
	{
	  if ( *(fdata+dataidx) < datamin ) datamin =  *(fdata+dataidx);
	  if ( *(fdata+dataidx) > datamax ) datamax =  *(fdata+dataidx);
	  
	  if ( ! fractional )
	    if ( *(fdata+dataidx) - (int) *(fdata+dataidx) > 0.000001 )
	      fractional = 1;
	}
      
      autoscale = 1;
      
      if ( fractional )
	{
	  for (autoscale=1; abs ((int32_t) (datamax * autoscale)) < 100000; autoscale *= 10) {}
	  
	  if ( abs ((int32_t) (datamin * autoscale)) < 10 )
	    fprintf (stderr, "WARNING Large sample value range (%g/%g), autoscaling might be a bad idea\n",
		     datamax, datamin);
	}
      
      scaling = autoscale;
    }

  /* Populate MSRecord structure with header details */
  if ( strncmp (SUNDEF, sh.knetwk, 8) ) ms_strncpclean (msr->network, sh.knetwk, 2);
  if ( strncmp (SUNDEF, sh.kstnm, 8) ) ms_strncpclean (msr->station, sh.kstnm, 5);
  if ( strncmp (SUNDEF, sh.khole, 8) ) ms_strncpclean (msr->location, sh.khole, 2);
  if ( strncmp (SUNDEF, sh.kcmpnm, 8) ) ms_strncpclean (msr->channel, sh.kcmpnm, 3);
  
  if ( forcenet )
    ms_strncpclean (msr->network, forcenet, 2);
  
  if ( forceloc )
    ms_strncpclean (msr->location, forceloc, 2);
  
  msr->starttime = ms_time2hptime (sh.nzyear, sh.nzjday, sh.nzhour, sh.nzmin, sh.nzsec, sh.nzmsec * 1000);
  
  /* Adjust for Begin ('B' SAC variable) time offset */
  msr->starttime += (double) sh.b * HPTMODULUS;

  /* Calculate sample rate from interval(period) rounding to nearest 0.000001 Hz */
  msr->samprate = (double) ((int)((1 / sh.delta) * 100000 + 0.5)) / 100000;
  
  msr->samplecnt = msr->numsamples = datacnt;
  
  /* Data sample type and sample array */
  if ( encoding == 4 )
    {
      msr->sampletype = 'f';
      msr->datasamples = fdata;
    }
  else
    {
      /* Create an array of scaled integers */
      idata = (int32_t *) malloc (datacnt * sizeof(int32_t));
      
      if ( verbose )
	fprintf (stderr, "[%s] Creating integer data scaled by: %lld\n", sacfile, scaling);
      
      for ( dataidx=0; dataidx < datacnt; dataidx++ )
	*(idata + dataidx) = (int32_t) (*(fdata + dataidx) * scaling);
      
      msr->sampletype = 'i';
      msr->datasamples = idata;
    }
  
  if ( verbose >= 1 )
    {
      fprintf (stderr, "[%s] %d samps @ %.6f Hz for N: '%s', S: '%s', L: '%s', C: '%s'\n",
	       sacfile, msr->numsamples, msr->samprate,
	       msr->network, msr->station,  msr->location, msr->channel);
    }
  
  if ( ! (mst = mst_addmsrtogroup (mstg, msr, 0, -1.0, -1.0)) )
    {
      fprintf (stderr, "[%s] Error adding samples to MSTraceGroup\n", sacfile);
    }
  
  /* Create an MSRecord template for the MSTrace by copying the current holder */
  if ( ! mst->private )
    {
      mst->private = malloc (sizeof(MSRecord));
    }
  
  memcpy (mst->private, msr, sizeof(MSRecord));
  
  /* If a blockette 100 is requested add it */
  if ( srateblkt )
    {
      memset (&Blkt100, 0, sizeof(struct blkt_100_s));
      Blkt100.samprate = (float) msr->samprate;
      msr_addblockette ((MSRecord *) mst->private, (char *) &Blkt100,
			sizeof(struct blkt_100_s), 100, 0);
    }
  
  /* Create a FSDH for the template, UNUSED FOR NOW */
  /*
  if ( ! ((MSRecord *)mst->private)->fsdh )
    {
      ((MSRecord *)mst->private)->fsdh = malloc (sizeof(struct fsdh_s));
      memset (((MSRecord *)mst->private)->fsdh, 0, sizeof(struct fsdh_s));
    }
  */

  packtraces (1);
  packedtraces += mstg->numtraces;
  
  fclose (ifp);
  
  if ( ofp  && ! outputfile )
    {
      fclose (ofp);
      ofp = 0;
    }

  if ( fdata )
    free (fdata);
  
  if ( idata )
    free (idata);
  
  msr->datasamples = 0;
  
  if ( msr )
    msr_free (&msr);
  
  return 0;
}  /* End of sac2group() */


/***************************************************************************
 * parsesac:
 *
 * Parse a SAC file, autodetecting format dialect (ALPHA,
 * binary, big or little endian).  Results will be placed in the
 * supplied SAC header struct and data (float sample array in host
 * byte order).  The data array will be allocated by this routine and
 * must be free'd by the caller.  The data array will contain the
 * number of samples indicated in the SAC header (sh->npts).
 *
 * The format argument is interpreted as:
 * 0 : Unknown, detection needed
 * 1 : ALPHA
 * 2 : Binary, byte order detection needed
 * 3 : Binary, little endian
 * 4 : Binary, big endian
 *
 * Returns number of data samples in file or -1 on failure.
 ***************************************************************************/
static int
parsesac (FILE *ifp, struct SACHeader *sh, float **data, int format,
	  int verbose, char *sacfile)
{
  char fourc[4];
  int swapflag = 0;
  int rv;
  
  /* Argument sanity */
  if ( ! ifp || ! sh || ! data )
    return -1;
  
  /* Read the first 4 characters */
  if ( fread (&fourc, 4, 1, ifp) < 1 )
    return -1;
  
  /* Determine if the file is ALPHA or binary SAC,
   * if the first 4 characters are spaces assume ALPHA SAC */
  if ( format == 0 )
    {
      if ( fourc[0] == ' ' && fourc[1] == ' ' && fourc[2] == ' ' && fourc[3] == ' ' )
	format = 1;
      else
	format = 2;  /* Byte order detection will occur below */
    }
  
  /* Rewind the file position pointer to the beginning */
  rewind (ifp);
  
  
  /* Read the header */
  if ( format == 1 )  /* Process SAC ALPHA header */
    {
      if ( (rv = readalphaheader (ifp, sh)) )
	{
	  fprintf (stderr, "[%s] Error parsing SAC ALPHA header at line %d\n",
		   sacfile, rv);
	  return -1;
	}
    }
  else if ( format >= 2 && format <= 4 ) /* Process SAC binary header */
    {
      if ( readbinaryheader (ifp, sh, &format, &swapflag, verbose, sacfile) )
	{
	  fprintf (stderr, "[%s] Error parsing SAC header\n", sacfile);
	  return -1;
	}
    }
  else
    {
      fprintf (stderr, "[%s] Unrecognized format value: %d\n", sacfile, format);
      return -1;
    }
  
  /* Sanity check the start time */
  if ( sh->nzyear < 1900 || sh->nzyear >3000 ||
       sh->nzjday < 1 || sh->nzjday > 366 ||
       sh->nzhour < 0 || sh->nzhour > 23 ||
       sh->nzmin < 0 || sh->nzmin > 59 ||
       sh->nzsec < 0 || sh->nzsec > 60 ||
       sh->nzmsec < 0 || sh->nzmsec > 999999 )
    {
      fprintf (stderr, "[%s] Unrecognized format (not SAC?)\n", sacfile);
      return -1;
    }
  
  if ( verbose )
    {
      if ( format == 1 )
	fprintf (stderr, "[%s] Reading SAC ALPHA format\n", sacfile);
      if ( format == 3 )
	fprintf (stderr, "[%s] Reading SAC binary format (little-endian)\n", sacfile);
      if ( format == 4 )
	fprintf (stderr, "[%s] Reading SAC binary format (big-endian)\n", sacfile);
    }
  
  if ( verbose > 2 )
    fprintf (stderr, "[%s] SAC header version number: %d\n", sacfile, sh->nvhdr);
  
  if ( sh->nvhdr != 6 )
    fprintf (stderr, "[%s] WARNING SAC header version (%d) not expected value of 6\n",
	     sacfile, sh->nvhdr);
  
  if ( sh->npts <= 0 )
    {
      fprintf (stderr, "[%s] No data, number of samples: %d\n", sacfile, sh->npts);
      return -1;
    }
  
  if ( sh->iftype != ITIME )
    {
      fprintf (stderr, "[%s] Data is not time series (IFTYPE=%d), cannot convert other types\n",
	       sacfile, sh->iftype);
      return -1;
    }
  
  if ( ! sh->leven )
    {
      fprintf (stderr, "[%s] Data is not evenly spaced (LEVEN not true), cannot convert\n", sacfile);
      return -1;
    }
  

  /* Allocate space for data samples */
  *data = (float *) malloc (sizeof(float) * sh->npts);
  memset (*data, 0, (sizeof(float) * sh->npts));
  
  /* Read the data samples */
  if ( format == 1 )  /* Process SAC ALPHA data */
    {
      if ( (rv = readalphadata (ifp, *data, sh->npts)) )
	{
	  fprintf (stderr, "[%s] Error parsing SAC ALPHA data at line %d\n",
		   sacfile, rv);
	  return -1;
	}
    }
  else if ( format >= 2 && format <= 4 ) /* Process SAC binary data */
    {
      if ( readbinarydata (ifp, *data, sh->npts, swapflag, verbose, sacfile) )
	{
	  fprintf (stderr, "[%s] Error reading SAC data samples\n", sacfile);
	  return -1;
	}
    }
  else
    {
      fprintf (stderr, "[%s] Unrecognized format value: %d\n", sacfile, format);
      return -1;
    }      
  
  return sh->npts;
}  /* End of parsesac() */


/***************************************************************************
 * readbinaryheader:
 *
 * Read a binary header from a file and parse into a SAC header
 * struct.  Also determines byte order and sets the swap flag unless
 * already dictated by the format.
 *
 * Returns 0 on sucess or -1 on failure.
 ***************************************************************************/
static int
readbinaryheader (FILE *ifp, struct SACHeader *sh, int *format,
		  int *swapflag, int verbose, char *sacfile)
{
  int bigendianhost;
  int32_t hdrver;
  
  /* Read the binary header into memory */
  if ( fread (sh, sizeof(struct SACHeader), 1, ifp) != 1 )
    {
      fprintf (stderr, "[%s] Could not read SAC header from file\n", sacfile);
      
      if ( ferror (ifp) )
	fprintf (stderr, "[%s] Error reading from file\n", sacfile);
      
      return -1;
    }
  
  /* Determine if host is big-endian */
  bigendianhost = ms_bigendianhost();
  
  *swapflag = 0;
  
  /* Test byte order using the header version if unknown */
  /* Also set the swapflag appropriately */
  if ( *format == 2 )
    {
      memcpy (&hdrver, &sh->nvhdr, 4);
      if ( hdrver < 1 || hdrver > 10 )
	{
	  gswap4 (&hdrver);
	  if ( hdrver < 1 || hdrver > 10 )
	    {
	      fprintf (stderr, "[%s] Cannot determine byte order (not SAC?)\n", sacfile);
	      return -1;
	    }
	  
	  *format = ( bigendianhost ) ? 3 : 4;
	  *swapflag = 1;
	}
      else
	{
	  *format =  ( bigendianhost ) ? 4 : 3;
	}
    }
  else if ( *format == 3 && bigendianhost ) *swapflag = 1;
  else if ( *format == 4 && ! bigendianhost ) *swapflag = 1;
  
  if ( verbose > 1 )
    {
      if ( *swapflag )
	fprintf (stderr, "[%s] Byte swapping required\n", sacfile);
      else
	fprintf (stderr, "[%s] Byte swapping NOT required\n", sacfile);
    }
  
  /* Byte swap all values in header */
  if ( *swapflag )
    swapsacheader (sh);  
  
  return 0;
}  /* End of readbinaryheader() */


/***************************************************************************
 * readbinarydata:
 *
 * Read binary data from a file and add to an array, the array
 * must already be allocated with datacnt floats.
 *
 * Returns 0 on sucess or -1 on failure.
 ***************************************************************************/
static int
readbinarydata (FILE *ifp, float *data, int datacnt, int swapflag,
		int verbose, char *sacfile)
{
  int samplesread = 0;
  int dataidx;
  
  /* Read in data samples */
  if ( (samplesread = fread (data, sizeof(float), datacnt, ifp)) != datacnt )
    {
      fprintf (stderr, "[%s] Only read %d of %d expected data samples\n",
	       sacfile, samplesread, datacnt);
      return -1;
    }
  
  /* Swap data samples */
  if ( swapflag )
    {
      for ( dataidx = 0; dataidx < datacnt; dataidx++ ) 
	{
	  gswap4 (data + dataidx);
	}
    }
  
  return 0;
}   /* End of readbinarydata() */


/***************************************************************************
 * readalphaheader:
 *
 * Read a alphanumeric header from a file and parse into a SAC header
 * struct.
 *
 * Returns 0 on sucess or a positive number indicating line number of
 * parsing failure.
 ***************************************************************************/
static int
readalphaheader (FILE *ifp, struct SACHeader *sh)
{
  char line[1025];
  int linecnt = 1;  /* The header starts at line 1 */
  int lineidx;
  int count;
  int hvidx = 0;
  char *cp;
  
  if ( ! ifp || ! sh )
    return -1;
  
  /* The first 14 lines x 5 values are floats */
  for (lineidx=0; lineidx < 14; lineidx++)
    {
      if ( ! fgets(line, sizeof(line), ifp) )
	return linecnt;
      
      count = sscanf (line, " %f %f %f %f %f ", (float *) sh + hvidx,
		      (float *) sh + hvidx + 1, (float *) sh + hvidx + 2,
		      (float *) sh + hvidx + 3, (float *) sh + hvidx + 4);
      
      if ( count != 5 )
	return linecnt;
      
      hvidx += 5;
      linecnt++;
    }
  
  /* The next 8 lines x 5 values are integers */
  for (lineidx=0; lineidx < 8; lineidx++)
    {
      if ( ! fgets(line, sizeof(line), ifp) )
	return linecnt;
      
      count = sscanf (line, " %d %d %d %d %d ", (int32_t *) sh + hvidx,
		      (int32_t *) sh + hvidx + 1, (int32_t *) sh + hvidx + 2,
		      (int32_t *) sh + hvidx + 3, (int32_t *) sh + hvidx + 4);
      
      if ( count != 5 )
	return linecnt;
      
      hvidx += 5;
      linecnt++;
    }
  
  /* Set pointer to start of string variables */
  cp =  (char *) sh + (hvidx * 4);
  
  /* The next 8 lines each contain 24 bytes of string data */
  for (lineidx=0; lineidx < 8; lineidx++)
    {
      memset (line, 0, sizeof(line));
      if ( ! fgets(line, sizeof(line), ifp) )
	return linecnt;
      
      memcpy (cp, line, 24);
      cp += 24;
      
      linecnt++;
    }
  
  /* Make sure each of the 23 string variables are left justified */
  cp =  (char *) sh + (hvidx * 4);  
  for (count=0; count < 24; count++)
    {
      int ridx, widx, width;
      char *fcp;
      
      /* Each string variable is 8 characters with one exception */
      if ( count != 1 )
	{
	  width = 8;
	}
      else
	{
	  width = 16;
	  count++;
	}
      
      /* Pointer to field */
      fcp = cp + (count * 8);

      /* Find first character that is not a space */
      ridx = 0;
      while ( *(fcp + ridx) == ' ' )
	ridx++;
      
      /* Remove any leading spaces */
      if ( ridx > 0 )
	{
	  for (widx=0; widx < width; widx++, ridx++)
	    {
	      if ( ridx < width )
		*(fcp + widx) = *(fcp + ridx);
	      else
		*(fcp + widx) = ' ';
	    }
	}
    }
  
  return 0;
}  /* End of readalphaheader() */


/***************************************************************************
 * readalphadata:
 *
 * Read a alphanumeric data from a file and add to an array, the array
 * must already be allocated with datacnt floats.
 *
 * Returns 0 on sucess or a positive number indicating line number of
 * parsing failure.
 ***************************************************************************/
static int
readalphadata (FILE *ifp, float *data, int datacnt)
{
  char line[1025];
  int linecnt = 31; /* Data samples start on line 31 */
  int samplesread = 0;
  int count;
  int dataidx = 0;
  
  if ( ! ifp || ! data || ! datacnt)
    return -1;
  
  /* Each data line should contain 5 floats unless the last */
  for (;;)
    {
      if ( ! fgets(line, sizeof(line), ifp) )
	return linecnt;
      
      count = sscanf (line, " %f %f %f %f %f ", (float *) data + dataidx,
		      (float *) data + dataidx + 1, (float *) data + dataidx + 2,
		      (float *) data + dataidx + 3, (float *) data + dataidx + 4);
      
      samplesread += count;
      
      if ( samplesread >= datacnt )
	break;
      else if ( count != 5 )
	return linecnt;
      
      dataidx += 5;
      linecnt++;
    }
  
  return 0;
}  /* End of readalphadata() */


/***************************************************************************
 * swapsacheader:
 *
 * Byte swap all multi-byte quantities (floats and ints) in SAC header
 * struct.
 *
 * Returns 0 on sucess and -1 on failure.
 ***************************************************************************/
static int
swapsacheader (struct SACHeader *sh)
{
  int32_t *ip;
  int idx;
  
  if ( ! sh )
    return -1;
  
  for ( idx=0; idx < (NUMFLOATHDR + NUMINTHDR); idx++ )
    {
      ip = (int32_t *) sh + idx;
      gswap4 (ip);
    }
  
  return 0;
}  /* End of swapsacheader() */


/***************************************************************************
 * writemetadata:
 *
 * Write a single line of metadata into the metadata output file
 * containing the following fields comma-separated in this order:
 *
 *   Network (knetwk)
 *   Station (kstnm)
 *   Location (khole)
 *   Channel (kcmpnm)
 *   Scale Factor (scale)
 *   Latitude (stla)
 *   Longitude (stlo)
 *   Elevation (stel) [not currently used by SAC]
 *   Depth (stdp) [not currently used by SAC]
 *   Component Azimuth (cmpaz), degrees clockwise from north
 *   Component Incident Angle (cmpinc), degrees from vertical
 *   Instrument Name (kinst)
 *
 * Returns 0 on sucess and -1 on failure.
 ***************************************************************************/
static int
writemetadata (struct SACHeader *sh)
{
  static flag wroteheader = 0;
  char network[9];
  char station[9];
  char location[9];
  char channel[9];
  
  if ( ! sh || ! mfp )
    return -1;
  
  if ( ! wroteheader )
    {
      wroteheader = 1;

      if ( ! fprintf (mfp, "Net,Sta,Loc,Chan,Scaling,Lat,Lon,Elev,Depth,Az,Inc,Inst\n") )
	{
	  fprintf (stderr, "Error writing to metadata output file\n");
	  return -1;
	}
    }

  if ( strncmp (SUNDEF, sh->knetwk, 2) ) ms_strncpclean (network, sh->knetwk, 2);
  else network[0] = '\0';
  if ( strncmp (SUNDEF, sh->kstnm, 2) ) ms_strncpclean (station, sh->kstnm, 5);
  else station[0] = '\0';
  if ( strncmp (SUNDEF, sh->khole, 2) ) ms_strncpclean (location, sh->khole, 2);
  else location[0] = '\0';
  if ( strncmp (SUNDEF, sh->kcmpnm, 2) ) ms_strncpclean (channel, sh->kcmpnm, 3);
  else channel[0] = '\0';
  
  /* LINE: Net,Sta,Loc,Chan,Scale,Lat,Lon,Elev,Dep,Az,Inc,Inst */
  
  /* Write the source parameters */
  if ( ! fprintf (mfp, "%s,%s,%s,%s,", network, station, location, channel) )
    {
      fprintf (stderr, "Error writing to metadata output file\n");
      return -1;
    }
  
  /* Write scale, lat, lon, elev, depth, azimuth and incident */
  if ( sh->scale != FUNDEF ) fprintf (mfp, "%.g,", sh->scale);
  else fprintf (mfp, ",");
  if ( sh->stla != FUNDEF ) fprintf (mfp, "%.5f,", sh->stla);
  else fprintf (mfp, ",");
  if ( sh->stlo != FUNDEF ) fprintf (mfp, "%.5f,", sh->stlo);
  else fprintf (mfp, ",");
  if ( sh->stel != FUNDEF ) fprintf (mfp, "%g,", sh->stel);
  else fprintf (mfp, ",");
  if ( sh->stdp != FUNDEF ) fprintf (mfp, "%g,", sh->stdp);
  else fprintf (mfp, ",");
  if ( sh->cmpaz != FUNDEF ) fprintf (mfp, "%g,", sh->cmpaz);
  else fprintf (mfp, ",");
  if ( sh->cmpinc != FUNDEF ) fprintf (mfp, "%g,", sh->cmpinc);
  else fprintf (mfp, ",");
  
  /* Write the instrument name and newline */
  if ( strncmp (SUNDEF, sh->kinst, 2) ) fprintf (mfp, "%.8s\n", sh->kinst);
  else fprintf (mfp, "\n");
  
  return 0;
}  /* End of writemetadata() */


/***************************************************************************
 * parameter_proc:
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure.
 ***************************************************************************/
static int
parameter_proc (int argcount, char **argvec)
{
  int optind;

  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
    {
      if (strcmp (argvec[optind], "-V") == 0)
	{
	  fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);
	  exit (0);
	}
      else if (strcmp (argvec[optind], "-h") == 0)
	{
	  usage();
	  exit (0);
	}
      else if (strncmp (argvec[optind], "-v", 2) == 0)
	{
	  verbose += strspn (&argvec[optind][1], "v");
	}
      else if (strcmp (argvec[optind], "-S") == 0)
	{
	  srateblkt = 1;
	}
      else if (strcmp (argvec[optind], "-n") == 0)
	{
	  forcenet = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-l") == 0)
	{
	  forceloc = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-r") == 0)
	{
	  packreclen = strtoul (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strcmp (argvec[optind], "-e") == 0)
	{
	  encoding = strtoul (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strcmp (argvec[optind], "-b") == 0)
	{
	  byteorder = strtoul (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strcmp (argvec[optind], "-o") == 0)
	{
	  outputfile = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-m") == 0)
	{
	  metafile = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-s") == 0)
	{
	  datascaling = strtoull (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strcmp (argvec[optind], "-f") == 0)
	{
	  sacformat = strtoul (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strncmp (argvec[optind], "-", 1) == 0 &&
	       strlen (argvec[optind]) > 1 )
	{
	  fprintf(stderr, "Unknown option: %s\n", argvec[optind]);
	  exit (1);
	}
      else
	{
	  addnode (&filelist, NULL, argvec[optind]);
	}
    }

  /* Make sure an input files were specified */
  if ( filelist == 0 )
    {
      fprintf (stderr, "No input files were specified\n\n");
      fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
      fprintf (stderr, "Try %s -h for usage\n", PACKAGE);
      exit (1);
    }

  /* Report the program version */
  if ( verbose )
    fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);

  /* Check the input files for any list files, if any are found
   * remove them from the list and add the contained list */
  if ( filelist )
    {
      struct listnode *prevln, *ln;
      char *lfname;
      
      prevln = ln = filelist;
      while ( ln != 0 )
	{
	  lfname = ln->data;
	  
	  if ( *lfname == '@' )
	    {
	      /* Remove this node from the list */
	      if ( ln == filelist )
		filelist = ln->next;
	      else
		prevln->next = ln->next;
	      
	      /* Skip the '@' first character */
	      if ( *lfname == '@' )
		lfname++;

	      /* Read list file */
	      readlistfile (lfname);
	      
	      /* Free memory for this node */
	      if ( ln->key )
		free (ln->key);
	      free (ln->data);
	      free (ln);
	    }
	  else
	    {
	      prevln = ln;
	    }
	  
	  ln = ln->next;
	}
    }

  return 0;
}  /* End of parameter_proc() */


/***************************************************************************
 * getoptval:
 * Return the value to a command line option; checking that the value is 
 * itself not an option (starting with '-') and is not past the end of
 * the argument list.
 *
 * argcount: total arguments in argvec
 * argvec: argument list
 * argopt: index of option to process, value is expected to be at argopt+1
 *
 * Returns value on success and exits with error message on failure
 ***************************************************************************/
static char *
getoptval (int argcount, char **argvec, int argopt)
{
  if ( argvec == NULL || argvec[argopt] == NULL ) {
    fprintf (stderr, "getoptval(): NULL option requested\n");
    exit (1);
    return 0;
  }
  
  /* Special case of '-o -' usage */
  if ( (argopt+1) < argcount && strcmp (argvec[argopt], "-o") == 0 )
    if ( strcmp (argvec[argopt+1], "-") == 0 )
      return argvec[argopt+1];
  
  if ( (argopt+1) < argcount && *argvec[argopt+1] != '-' )
    return argvec[argopt+1];
  
  fprintf (stderr, "Option %s requires a value\n", argvec[argopt]);
  exit (1);
  return 0;
}  /* End of getoptval() */


/***************************************************************************
 * readlistfile:
 *
 * Read a list of files from a file and add them to the filelist for
 * input data.  The filename is expected to be the last
 * space-separated field on the line.
 *
 * Returns the number of file names parsed from the list or -1 on error.
 ***************************************************************************/
static int
readlistfile (char *listfile)
{
  FILE *fp;
  char  line[1024];
  char *ptr;
  int   filecnt = 0;
  
  char  filename[1024];
  char *lastfield = 0;
  int   fields = 0;
  int   wspace;
  
  /* Open the list file */
  if ( (fp = fopen (listfile, "rb")) == NULL )
    {
      if (errno == ENOENT)
        {
          fprintf (stderr, "Could not find list file %s\n", listfile);
          return -1;
        }
      else
        {
          fprintf (stderr, "Error opening list file %s: %s\n",
		   listfile, strerror (errno));
          return -1;
        }
    }
  
  if ( verbose )
    fprintf (stderr, "Reading list of input files from %s\n", listfile);
  
  while ( (fgets (line, sizeof(line), fp)) !=  NULL)
    {
      /* Truncate line at first \r or \n, count space-separated fields
       * and track last field */
      fields = 0;
      wspace = 0;
      ptr = line;
      while ( *ptr )
	{
	  if ( *ptr == '\r' || *ptr == '\n' || *ptr == '\0' )
	    {
	      *ptr = '\0';
	      break;
	    }
	  else if ( *ptr != ' ' )
	    {
	      if ( wspace || ptr == line )
		{
		  fields++; lastfield = ptr;
		}
	      wspace = 0;
	    }
	  else
	    {
	      wspace = 1;
	    }
	  
	  ptr++;
	}
      
      /* Skip empty lines */
      if ( ! lastfield )
	continue;
      
      if ( fields >= 1 && fields <= 3 )
	{
	  fields = sscanf (lastfield, "%s", filename);
	  
	  if ( fields != 1 )
	    {
	      fprintf (stderr, "Error parsing file name from: %s\n", line);
	      continue;
	    }
	  
	  if ( verbose > 1 )
	    fprintf (stderr, "Adding '%s' to input file list\n", filename);
	  
	  addnode (&filelist, NULL, filename);
	  filecnt++;
	  
	  continue;
	}
    }
  
  fclose (fp);
  
  return filecnt;
}  /* End readlistfile() */


/***************************************************************************
 * addnode:
 *
 * Add node to the specified list.
 ***************************************************************************/
static void
addnode (struct listnode **listroot, char *key, char *data)
{
  struct listnode *lastlp, *newlp;
  
  if ( data == NULL )
    {
      fprintf (stderr, "addnode(): No file name specified\n");
      return;
    }
  
  lastlp = *listroot;
  while ( lastlp != 0 )
    {
      if ( lastlp->next == 0 )
        break;
      
      lastlp = lastlp->next;
    }
  
  newlp = (struct listnode *) malloc (sizeof (struct listnode));
  memset (newlp, 0, sizeof (struct listnode));
  if ( key ) newlp->key = strdup(key);
  else newlp->key = key;
  if ( data) newlp->data = strdup(data);
  else newlp->data = data;
  newlp->next = 0;
  
  if ( lastlp == 0 )
    *listroot = newlp;
  else
    lastlp->next = newlp;
  
}  /* End of addnode() */


/***************************************************************************
 * record_handler:
 * Saves passed records to the output file.
 ***************************************************************************/
static void
record_handler (char *record, int reclen)
{
  if ( fwrite(record, reclen, 1, ofp) != 1 )
    {
      fprintf (stderr, "Error writing to output file\n");
    }
}  /* End of record_handler() */


/***************************************************************************
 * usage:
 * Print the usage message and exit.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "%s version: %s\n\n", PACKAGE, VERSION);
  fprintf (stderr, "Convert SAC waveform data to Mini-SEED.\n\n");
  fprintf (stderr, "Usage: %s [options] file1 [file2 file3 ...]\n\n", PACKAGE);
  fprintf (stderr,
	   " ## Options ##\n"
	   " -V             Report program version\n"
	   " -h             Show this usage message\n"
	   " -v             Be more verbose, multiple flags can be used\n"
	   " -S             Include SEED blockette 100 for very irrational sample rates\n"
	   " -n netcode     Specify the SEED network code, default is blank\n"
	   " -l loccode     Specify the SEED location code, default is blank\n"
	   " -r bytes       Specify record length in bytes for packing, default: 4096\n"
	   " -e encoding    Specify SEED encoding format for packing, default: 11 (Steim2)\n"
	   " -b byteorder   Specify byte order for packing, MSBF: 1 (default), LSBF: 0\n"
	   " -o outfile     Specify the output file, default is <inputfile>.mseed\n"
	   " -m metafile    Specify the metadata output file\n"
	   " -s factor      Specify scaling factor for sample values, default is autoscale\n"
	   " -f format      Specify input SAC file format (default is autodetect):\n"
	   "                  0=autodetect, 1=alpha, 2=binary (detect byte order),\n"
	   "                  3=binary (little-endian), 4=binary (big-endian)\n"
	   "\n"
	   " file(s)        File(s) of SAC input data\n"
	   "                  If a file is prefixed with an '@' it is assumed to contain\n"
	   "                  a list of data files to be read\n"
	   "\n"
	   "Supported Mini-SEED encoding formats:\n"
           " 3  : 32-bit integers, scaled\n"
           " 4  : 32-bit floats (C float)\n"
           " 10 : Steim 1 compression of scaled 32-bit integers\n"
           " 11 : Steim 2 compression of scaled 32-bit integers\n"
           "\n"
           "For any of the non-floating point encoding formats the data samples\n"
           "will be scaled either by the specified scaling factor or autoscaling\n"
	   "where the magnitude of the maximum sample will be 6 digits.\n"
	   "\n");
}  /* End of usage() */

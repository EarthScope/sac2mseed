/***************************************************************************
 * sac2mseed.c
 *
 * Simple waveform data conversion from SAC to Mini-SEED.
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified 2006.047
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include <libmseed.h>

#include "sacformat.h"

#define VERSION "0.1"
#define PACKAGE "sac2mseed"

struct listnode {
  char *key;
  char *data;
  struct listnode *next;
};

static void packtraces (flag flush);
static int sac2group (char *sacfile, TraceGroup *mstg);
static int parsesac (FILE *ifp, struct SACHeader *sh, float **data, int format, 
		     char *sacfile);
static int swapsacheader (struct SACHeader *sh);
static int32_t *mkhostdata (char *data, int datalen, int datasamplesize, flag swapflag);
static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static int readlistfile (char *listfile);
static void addnode (struct listnode **listroot, char *key, char *data);
static void record_handler (char *record, int reclen);
static void usage (void);

static int   verbose     = 0;
static int   packreclen  = -1;
static int   encoding    = -1;
static int   byteorder   = -1;
static int   sacformat   = 0;
static char  srateblkt   = 0;
static char *forcenet    = 0;
static char *forceloc    = 0;
static char *outputfile  = 0;
static FILE *ofp         = 0;

/* A list of input files */
struct listnode *filelist = 0;

static TraceGroup *mstg = 0;

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
  
  /* Init TraceGroup */
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
  
  /* Read input SAC files into TraceGroup */
  flp = filelist;
  while ( flp != 0 )
    {
      if ( verbose )
	fprintf (stderr, "Reading %s\n", flp->data);

      sac2group (flp->data, mstg);
      
      flp = flp->next;
    }

  /* Pack any remaining, possibly all data */
  packtraces (1);
  packedtraces += mstg->numtraces;

  fprintf (stderr, "Packed %d trace(s) of %d samples into %d records\n",
	   packedtraces, packedsamples, packedrecords);
  
  /* Make sure everything is cleaned up */
  mst_freegroup (&mstg);
  
  if ( ofp )
    fclose (ofp);
  
  return 0;
}  /* End of main() */


/***************************************************************************
 * packtraces:
 *
 * Pack all traces in a group using per-Trace templates.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
packtraces (flag flush)
{
  Trace *mst;
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
				  &trpackedsamples, flush, verbose-2, (MSrecord *) mst->private);
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
 * Read a SAC file and add data samples to a TraceGroup.  As the SAC
 * data is read in a MSrecord struct is used as a holder for the input
 * information.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static int
sac2group (char *sacfile, TraceGroup *mstg)
{
  FILE *ifp = 0;
  MSrecord *msr = 0;
  Trace *mst;
  struct blkt_100_s Blkt100;
  
  struct SACHeader sh;
  float *data = 0;
  int datacnt;
  
  int dataidx;

  flag uctimeflag = 0;

  /* Open input file */
  if ( (ifp = fopen (sacfile, "rb")) == NULL )
    {
      fprintf (stderr, "Cannot open input file: %s (%s)\n",
	       sacfile, strerror(errno));
      return -1;
    }
  
  /* Parse input SAC file into a header structure and data buffer */
  if ( (datacnt = parsesac (ifp, &sh, &data, sacformat, sacfile)) < 0 )
    {
      fprintf (stderr, "Error parsing %s\n", sacfile);
      
      return -1;
    }
  
  /* Open output file if needed */
  if ( ! ofp )
    {
      char *extptr;
      char mseedoutputfile[1024];
      int namelen = strlen (sacfile);
      
      /* If name ends in .sac replace it with .mseed */
      extptr = ( namelen > 4 ) ? &sacfile[namelen-4] : 0;
      if ( strcasecmp (extptr, ".sac") == 0 )
	{
	  strncpy (mseedoutputfile, sacfile, sizeof(mseedoutputfile));
	  mseedoutputfile[namelen-4] = '\0'; /* Truncate at extension */
	  snprintf (mseedoutputfile, sizeof(mseedoutputfile), "%s.mseed", mseedoutputfile);
	}
      else
	{
	  snprintf (mseedoutputfile, sizeof(mseedoutputfile), "%s.mseed", sacfile);
	}
      
      if ( (ofp = fopen (mseedoutputfile, "wb")) == NULL )
        {
          fprintf (stderr, "Cannot open output file: %s (%s)\n",
                   mseedoutputfile, strerror(errno));
          return -1;
        }
    }
  
  if ( ! (msr = msr_init(msr)) )
    {
      fprintf (stderr, "Cannot initialize MSrecord strcture\n");
      return -1;
    }
  
  printf ("DATA: %f\n\n", *data);

  for ( dataidx=0; dataidx < 40; dataidx++ )
    {
      printf ("DATA%02d: %f\n", dataidx, *(data+dataidx));
    }

  return 1;
  //CHAD, populate group here...
  

  if ( ! (mst = mst_addmsrtogroup (mstg, msr, -1.0, -1.0)) )
    {
      fprintf (stderr, "[%s] Error adding samples to TraceGroup\n", sacfile);
    }
	  
  /* Create an MSrecord template for the Trace by copying the current holder */
  if ( ! mst->private )
    {
      mst->private = malloc (sizeof(MSrecord));
    }
  
  memcpy (mst->private, msr, sizeof(MSrecord));
  
  /* If a blockette 100 is requested add it */
  if ( srateblkt )
    {
      memset (&Blkt100, 0, sizeof(struct blkt_100_s));
      Blkt100.samprate = (float) msr->samprate;
      msr_addblockette ((MSrecord *) mst->private, (char *) &Blkt100,
			sizeof(struct blkt_100_s), 100, 0);
    }
  
  /* Create a FSDH for the template */
  if ( ! ((MSrecord *)mst->private)->fsdh )
    {
      ((MSrecord *)mst->private)->fsdh = malloc (sizeof(struct fsdh_s));
      memset (((MSrecord *)mst->private)->fsdh, 0, sizeof(struct fsdh_s));
    }
  
  /* Set bit 7 (time tag questionable) in the data quality flags appropriately */
  if ( uctimeflag )
    ((MSrecord *)mst->private)->fsdh->dq_flags |= 0x80;
  else
    ((MSrecord *)mst->private)->fsdh->dq_flags &= ~(0x80);
  
  packtraces (1);
  packedtraces += mstg->numtraces;
  mst_initgroup (mstg);
  
  /* Cleanup and reset state */
  msr->datasamples = 0;
  msr = msr_init (msr);
  
  fclose (ifp);
  
  if ( ofp  && ! outputfile )
    {
      fclose (ofp);
      ofp = 0;
    }
  
  if ( data )
    free (data);
  
  if ( msr )
    msr_free (&msr);
  
  return 0;
}  /* End of sac2group() */


/***************************************************************************
 * parsesac:
 *
 * Parse a SAC file, autodetecting format dialect (ASCII/ALPHA,
 * binary, big or little endian).  Results will be placed in the
 * supplied SAC header struct and data (float sample array in host
 * byte order).  The data array will be allocated by this routine and
 * must be free'd by the caller.  The data array will contain the
 * number of samples indicated in the SAC header (sh->npts).
 *
 * The format argument is interpreted as:
 * 0 : Unknown, detection needed
 * 1 : ASCII/ALPHA
 * 2 : Binary, byte order detection needed
 * 3 : Binary, little endian
 * 4 : Binary, big endian
 *
 * Returns number of data samples in file or -1 on failure.
 ***************************************************************************/
static int
parsesac (FILE *ifp, struct SACHeader *sh, float **data, int format,
	  char *sacfile)
{
  char fourc[4];
  
  int bigendianhost;
  int swapflag = 0;
  
  float *test;

  int datacnt; /* Number of samples read from file */
  int dataidx; /* Iterator for data samples */
  
  /* Argument sanity */
  if ( ! ifp || ! sh || ! data )
    return -1;
  
  /* Read the first 4 characters */
  if ( fread (&fourc, 4, 1, ifp) < 1 )
    {
      return -1;
    }
  
  /* Determine if the file is ASCII or binary SAC,
   * if the first 4 characters are spaces assume ASCII SAC */
  if ( format == 0 )
    {
      if ( fourc[0] == ' ' && fourc[1] == ' ' && fourc[2] == ' ' && fourc[3] == ' ' )
	format = 1;
      else
	format = 2;  /* Byte order detection will occur below */
    }
  
  /* Rewind the file position pointer to the beginning */
  rewind (ifp);
  
  if ( format == 1 )  /* Process SAC ASCII format */
    {
      if ( verbose > 1 )
	fprintf (stderr, "[%s] Detected SAC ASCII/ALPHA format\n", sacfile);
      
    }
  else if ( format >= 2 && format <= 4 ) /* Process SAC binary file */
    {
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
      
      /* Test byte order using the header version if unknown */
      /* Also set the swapflag  */
      if ( format == 2 )
	{
	  int32_t hdrver;
	  
	  memcpy (&hdrver, &sh->nvhdr, 4);
	  if ( hdrver < 1 || hdrver > 10 )
	    {
	      gswap4 (&hdrver);
	      if ( hdrver < 1 || hdrver > 10 )
		{
		  fprintf (stderr, "[%s] Cannot determine byte order (not SAC?)\n", sacfile);
		  return -1;
		}

	      format = ( bigendianhost ) ? 3 : 4;
	      swapflag = 1;
	    }
	  else
	    {
	      format =  ( bigendianhost ) ? 4 : 3;
	    }
	}
      else if ( format == 3 && bigendianhost ) swapflag = 1;
      else if ( format == 4 && ! bigendianhost ) swapflag = 1;
      
      if ( verbose > 2 )
	{
	  if ( swapflag )
	    fprintf (stderr, "[%s] Byte swapping required\n", sacfile);
	  else
	    fprintf (stderr, "[%s] Byte swapping NOT required\n", sacfile);
	}

      /* Byte swap all values in header */
      if ( swapflag )
	swapsacheader (sh);
      
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
	  if ( format == 3 )
	    fprintf (stderr, "[%s] Reading SAC binary format (little-endian)\n", sacfile);
	  if ( format == 4 )
	    fprintf (stderr, "[%s] Reading SAC binary format (big-endian)\n", sacfile);
	}
      
      if ( verbose > 2 )
	fprintf (stderr, "[%s] SAC Header version number: %d\n", sacfile, sh->nvhdr);
      
      if ( sh->npts <= 0 )
	{
	  fprintf (stderr, "[%s] No data, number of samples: %d\n", sacfile, sh->npts);
	  return -1;
	}
      
      /* Allocate space for data samples */
      *data = (float *) malloc (sizeof(float) * sh->npts);
      
      /* Read in data samples */
      if ( (datacnt = fread (*data, sizeof(float), sh->npts, ifp)) != sh->npts )
	{
	  fprintf (stderr, "[%s] Only read %d of %d expected data samples\n", sacfile, datacnt, sh->npts);
	  return -1;
	}
      
      printf ("0/%d DATA: %f\n\n", datacnt, **data);

      /* Swap data samples */
      if ( swapflag )
	{
	  for ( dataidx = 0; dataidx < sh->npts; dataidx++ ) 
	    {
	      gswap4 (*data + dataidx);
	    }
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
 * swapsacheader:
 *
 * Byte swap a SAC header struct.
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
 * mkhostdata:
 *
 * Given the raw input data return a buffer of 32-bit integers in host
 * byte order.  The routine may modified the contents of the supplied
 * data sample buffer.
 *
 * A buffer used for 16->32 bit conversions is statically maintained
 * for re-use.  If 'data' is specified as 0 this internal buffer will
 * be released.
 *
 * Returns a pointer on success and 0 on failure or reset.
 ***************************************************************************/
static int32_t *
mkhostdata (char *data, int datalen, int datasamplesize, flag swapflag)
{
  static int32_t *samplebuffer = 0;
  static int maxsamplebufferlen = 0;
  
  int32_t *hostdata = 0;
  int32_t *sampleptr4;
  int16_t *sampleptr2;
  int numsamples;
  
  if ( ! data )
    {
      if ( samplebuffer )
	free (samplebuffer);
      samplebuffer = 0;
      maxsamplebufferlen = 0;
      
      return 0;
    }
  
  if ( datasamplesize == 2 )
    {
      if ( (datalen * 2) > maxsamplebufferlen )
	{
	  if ( (samplebuffer = realloc (samplebuffer, (datalen*2))) == NULL )
	    {
	      fprintf (stderr, "Error allocating memory for sample buffer\n");
	      return 0;
	    }
	  else
	    maxsamplebufferlen = datalen * 2;
	}
      
      sampleptr2 = (int16_t *) data;
      sampleptr4 = samplebuffer;
      numsamples = datalen / datasamplesize;
      
      /* Convert to 32-bit and swap data samples if needed */
      while (numsamples--)
	{
	  if ( swapflag )
	    gswap2a (sampleptr2);
	  
	  *(sampleptr4++) = *(sampleptr2++);
	}
      
      hostdata = samplebuffer;
    }
  else if ( datasamplesize == 4 )
    {
      /* Swap data samples if needed */
      if ( swapflag )
	{
	  sampleptr4 = (int32_t *) data;
	  numsamples = datalen / datasamplesize;
	  
	  while (numsamples--)
	    gswap4a (sampleptr4++);
	}
      
      if ( verbose > 1 && encoding == 1 )
	fprintf (stderr, "WARNING: attempting to pack 32-bit integers into 16-bit encoding\n");
      
      hostdata = (int32_t *) data;
    }
  else
    {
      fprintf (stderr, "Error, unknown data sample size: %d\n", datasamplesize);
      return 0;
    }
  
  return hostdata;
}  /* End of mkhostdata() */


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
	  packreclen = atoi (getoptval(argcount, argvec, optind++));
	}
      else if (strcmp (argvec[optind], "-e") == 0)
	{
	  encoding = atoi (getoptval(argcount, argvec, optind++));
	}
      else if (strcmp (argvec[optind], "-b") == 0)
	{
	  byteorder = atoi (getoptval(argcount, argvec, optind++));
	}
      else if (strcmp (argvec[optind], "-o") == 0)
	{
	  outputfile = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-f") == 0)
	{
	  sacformat = atoi (getoptval(argcount, argvec, optind++));
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
 * space-separated field on the line, in this way both simple lists
 * and various dirf (filenr.lis) formats are supported.
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
	   " -f format      Specify input SAC file format (default is autodetect):\n"
	   "                  0=autodetect, 1=ASCII/ALPHA, 2=binary (detect byte order),\n"
	   "                  3=binary (little-endian), 4=binary (big-endian)\n"
	   "\n"
	   " file(s)        File(s) of SAC input data\n"
	   "                  If a file is prefixed with an '@' it is assumed to contain\n"
	   "                  a list of data files to be read\n"
	   "\n"
	   "Supported Mini-SEED encoding formats:\n"
           " 3  : 32-bit integers, scaled\n"
           " 4  : 32-bit floats (C float)\n"
           " 5  : 64-bit floats (C double)\n"
           " 10 : Steim 1 compression of scaled 32-bit integers\n"
           " 11 : Steim 2 compression of scaled 32-bit integers\n"
           "\n"
           "For any of the non-floating point encoding formats the data samples\n"
           "will be scaled by 1,000,000 to avoid precision truncation.\n"
	   "\n");
}  /* End of usage() */

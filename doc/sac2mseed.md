# <p >SAC to miniSEED converter</p>

1. [Name](#)
1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [Seed Location Ids](#seed-location-ids)
1. [List Files](#list-files)
1. [About Sac](#about-sac)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre >
sac2mseed [options] file1 [file2 file3 ...]
</pre>

## <a id='description'>Description</a>

<p ><b>sac2mseed</b> converts SAC waveform data to miniSEED format.  By default the format of the input files is automatically detected: alpha or binary (byte order autodetected).  The format can also be forced with the <i>-f</i> option.  If an input file name is prefixed with an '@' character the file is assumed to contain a list of input data files, see <i>LIST FILES</i> below.</p>

<p >If the input file name ends in ".sac" (not case sensitive) the default output file name will be the same with the extension replace with ".mseed".  The output data may be re-directed to a single file or stdout using the -o option.</p>

## <a id='options'>Options</a>

<b>-V</b>

<p style="padding-left: 30px;">Print program version and exit.</p>

<b>-h</b>

<p style="padding-left: 30px;">Print program usage and exit.</p>

<b>-v</b>

<p style="padding-left: 30px;">Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.</p>

<b>-S</b>

<p style="padding-left: 30px;">Include SEED blockette 100 in each output record with the sample rate in floating point format.  The basic format for storing sample rates in SEED data records is a rational approximation (numerator/denominator).  Precision will be lost if a given sample rate cannot be well approximated.  This option should be used in those cases.</p>

<b>-n </b><i>netcode</i>

<p style="padding-left: 30px;">Specify the SEED network code to use, if not specified the network code will be the value of the KNETWK variable in the SAC header, if KNETWK is not specified the network code will be blank.  It is highly recommended to specify a network code if no network is defined in the SAC file.</p>

<b>-s </b><i>stacode</i>

<p style="padding-left: 30px;">Specify the SEED station code to use, if not specified the station code will be the value of the KSTNM variable in the SAC header, if KSTNM is not specified the location ID will be blank.</p>

<b>-l </b><i>locid</i>

<p style="padding-left: 30px;">Specify the SEED location ID to use, if not specified the location ID will be the value of the KHOLE variable in the SAC header, if KHOLE is not specified the location ID will be blank.</p>

<b>-c </b><i>chancodes</i>

<p style="padding-left: 30px;">Specify the SEED channel codes to use, if not specified the channel code will be the value of the KCMPNM variable in the SAC header, if KCMPNM is not specified the location ID will be blank.  As a special case a dot (.) will be interpreted as the same character as the input channel name, for example, "L.." can be specified to only replace the first code with 'L' and leave the other two codes as they are.</p>

<b>-r </b><i>bytes</i>

<p style="padding-left: 30px;">Specify the miniSEED record length in <i>bytes</i>, default is 4096.</p>

<b>-e </b><i>encoding</i>

<p style="padding-left: 30px;">Specify the miniSEED data encoding format, default is 11 (Steim-2 compression).  Other supported encoding formats include 10 (Steim-1 compression), 1 (16-bit integers) and 3 (32-bit integers).  The 16-bit integers encoding should only be used if all data samples can be represented in 16 bits.</p>

<b>-b </b><i>byteorder</i>

<p style="padding-left: 30px;">Specify the miniSEED byte order, default is 1 (big-endian or most significant byte first).  The other option is 0 (little-endian or least significant byte first).  It is highly recommended to always create big-endian SEED.</p>

<b>-o </b><i>outfile</i>

<p style="padding-left: 30px;">Write all miniSEED records to <i>outfile</i>, if <i>outfile</i> is a single dash (-) then all miniSEED output will go to stdout.  All diagnostic output from the program is written to stderr and should never get mixed with data going to stdout.</p>

<b>-m </b><i>metafile</i>

<p style="padding-left: 30px;">For each input SAC file write a one-line summary of channel metadata <i>metafile</i>.  The one-line summary is a comma-separated list containing: network, station, location, channel, latitude, longitude, elevation, depth, azimuth, incidence, instrument name, scale factor, sampling rate and start and end times.  In SAC the component azimuth is in degrees clockwise from north, the component incident angle is in degrees from vertical and the elevation and depth are both in meters.</p>

<b>-me</b>

<p style="padding-left: 30px;">When writing out a metadata file include the event name (kevnm) and user strings 0, 1 and 2 (kuser0, kuser1 and kuser2).</p>

<b>-s </b><i>factor</i>

<p style="padding-left: 30px;">When writing data to an integer (miniSEED) encoding format apply this scaling <i>factor</i> to each input floating point data sample before truncating to an integer.  By default autoscaling is used and a scaling factor is determined that will scale the maximum sample value to a minimum of 6 digits.  If none of the input sample values include fractional components the scaling factor will be 1 and the floating point data will simply be truncated to their integer components.</p>

<b>-f </b><i>format</i>

<p style="padding-left: 30px;">By default the format of each input file is autodetected, either alpha or binary (little or big endian byte order autodetected as well). This option forces the format for every input file:</p>

<pre style="padding-left: 30px;">
0 : Autodetect SAC format (default)
1 : Alphanumeric SAC format
2 : Binary SAC format, autodetect byte order
3 : Binary SAC format, little-endian
4 : Binary SAC format, big-endian
</pre>

## <a id='seed-location-ids'>Seed Location Ids</a>

<p >The contents of the SAC header variable KHOLE is used as the SEED location ID if it is set.  While the definition of KHOLE and SEED location ID are not officially the same, this is a known convention when converting between these two formats.</p>

## <a id='list-files'>List Files</a>

<p >If an input file is prefixed with an '@' character the file is assumed to contain a list of file for input.  Multiple list files can be combined with multiple input files on the command line.  The last, space separated field on each line is assumed to be the file name to be read.</p>

<p >An example of a simple text list:</p>

<pre >
TA.ELFS..LHE.SAC
TA.ELFS..LHN.SAC
TA.ELFS..LHZ.SAC
</pre>

## <a id='about-sac'>About Sac</a>

<p >Seismic Analysis Code (SAC) is a general purpose interactive program designed for the study of sequential signals, especially timeseries data.  Originally developed at the Lawrence Livermore National Laboratory the SAC software package is also available from IRIS.</p>

## <a id='author'>Author</a>

<pre >
Chad Trabant
IRIS Data Management Center
</pre>


(man page 2017/04/03)

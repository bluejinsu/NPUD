/*++++ FILE DESCRIPTION ++++++++++++++++++++++++++++++++++++++++++++++++++++++
                       
@file                   $Workfile: rs_gx40x_specdata_if_defs.h $
@copyright              (c) 2005 Rohde & Schwarz, Munich

@language               ANSI C

compiler                ANSI C conform

@description            Definition of data structure for spectrum data 
                        streams. 

------ End of file description ---------------------------------------------*/

#ifndef _RS_GX40X_SPECDATA_IF_DEFS_H_
#define _RS_GX40X_SPECDATA_IF_DEFS_H_

/*++++ INCLUDE FILES +++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "rs_gx40x_p_types.h"
#include "rs_gx40x_global_frame_header_if_defs.h"

/*---- End of include files ------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*++++ GLOBAL DEFINES ++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/** Bit mask for accessing the 'scan spectrum index' in the
spectral data 'status' word */
#define kSPECDATA_SCANINDEX_FLAG 0x007C0000UL

/** Bit mask for accessing the 'fragment of a complete spectrum' in the 
  spectral data 'status' word */
#define kSPECDATA_FRAGMENT_FLAG  0x00020000UL

/** Bit mask for accessing the 'blanking' flag in the 
  spectral data 'status' word */
#define kSPECDATA_BLANKING_FLAG  0x00010000UL

/** Bit mask for accessing the 'eSPECDATA_SAMPLESOURCE' type in the 
    spectral data 'status' word */
#define kSPECDATA_SAMPLESOURCE   0x0000F000UL

/** Bit mask for accessing the 'dBFS' flag in the 
    spectral data 'status' word */
#define kSPECDATA_LEVELINFO      0x00000400UL

/** Bit mask for accessing the 'invalid' flag in the 
    spectral data 'status' word */
#define kSPECDATA_INVALIDFLAG    0x00000200UL

/** Bit mask for accessing the spectrum level calculation mode 
    (linear or logarithmic) in the spectral data 'status' word */
#define kSPECDATA_LEVELTYPE      0x00000100UL

/** Bit mask for accessing the window(ing) mode field in
    the spectral data 'status' word */
#define kSPECDATA_WINDOWTYPE     0x000000F0UL

/** Bit mask for accessing the display mode field in the 
    spectral data 'status' word */
#define kSPECDATA_DISPLAYMODE    0x0000000FUL

/** Bit mask for marking the kFactor value (intKFactor) within the extended 
    spectrum header (typSPECDATA_HEADER_EXTENDED) as not valid value */
#define kK_FACTOR_NOT_VALID     0x80000000

/*---- End of global defines -----------------------------------------------*/


/*++++ GLOBAL TYPES DECLARATION ++++++++++++++++++++++++++++++++++++++++++++*/

/** Enumeration of the spectral data display modes */
typedef enum
{
  ekSPECDATA_DISPLAY_MODE_AVERAGING             = 0x0,  /**< Display mode 'averaging'          */
  ekSPECDATA_DISPLAY_MODE_MINHOLD               = 0x1,  /**< Display mode 'minhold'            */
  ekSPECDATA_DISPLAY_MODE_PEAKHOLD              = 0x2,  /**< Display mode 'peakhold'           */
  ekSPECDATA_DISPLAY_MODE_PEAKHOLDSHORTTIME     = 0x3,  /**< Display mode 'peakhold shorttime' */
  ekSPECDATA_DISPLAY_MODE_MINHOLDSHORTTIME      = 0x4,  /**< Display mode 'minhold shorttime'  */
  ekSPECDATA_DISPLAY_MODE_DIFFERENCE            = 0x5,  /**< Display mode 'difference'         */
  ekSPECDATA_DISPLAY_MODE_CLEARWRITE            = 0x6   /**< Display mode 'clear write'        */

} eSPECDATA_DISPLAY_MODES; /**< Spectral data display modes */

/** Enumeration of the spectral data window(ing) modes */
typedef enum
{
  ekSPECDATA_WINTYPE_RECT     = 0x00,  /**< Rectangular window(ing) mode */
  ekSPECDATA_WINTYPE_HAMMING  = 0x10,  /**< Hamming     window(ing) mode */
  ekSPECDATA_WINTYPE_HANN     = 0x20,  /**< Hanning     window(ing) mode */
  ekSPECDATA_WINTYPE_KAISER   = 0x30,  /**< Kaiser      window(ing) mode */
  ekSPECDATA_WINTYPE_BLACKMAN = 0x40   /**< Blackman    window(ing) mode */

} eSPECDATA_WINTYPES;  /**< Spectral data window(ing) modes */

/** Enumeration of the spectral data level calculation modes */
typedef enum
{
  ekSPECDATA_LEVELTYPE_LIN    =  0x000, /**< Level calculation mode 'linear'      */
  ekSPECDATA_LEVELTYPE_LOG    =  0x100  /**< Level calculation mode 'logarithmic' */

} eSPECDATA_LEVELTYPE;  /**< Spectral data level calculation modes */

/** Enumeration of the spectral data level information */
typedef enum
{
  ekSPECDATA_LEVELINFO_STD    =  0x000, /**< Level in uV (linear) or dBm (logarithmic)            */
  ekSPECDATA_LEVELINFO_DBFS   =  0x400  /**< Spectrum in full scale (linear) or dBFS (logarithmic)*/

} eSPECDATA_LEVELINFO;  /**< Spectral data level information */

/** Enumeration of the samples used for the fft */
typedef enum
{
  ekSPECDATA_SAMPLESOURCE_BASEBAND  =  0x0000, /**< I/Q baseband samples     */
  ekSPECDATA_SAMPLESOURCE_FM        =  0x1000, /**< instantaneous frequency  */
  ekSPECDATA_SAMPLESOURCE_ENVELOPE  =  0x2000, /**< envelope                 */
  ekSPECDATA_SAMPLESOURCE_BB_SQUARE =  0x3000  /**< baseband squared         */

} eSPECDATA_SAMPLESOURCE;  /**< Spectral data level information */

/** Header structure type for spectral data */
typedef struct struSPECDATA_HEADER
{
  /** Time stamp of first sample of the data from which the spectrum was calculated */
  ptypBIGTIME bigtimeStamp;
  
  /** Spectrum center frequency in Hz. -- 64bit representation */
  ptypUINT uintCenterFrequency_Low;     /**< least significant 32 Bit */
  ptypUINT uintCenterFrequency_High;    /**< most  significant 32 Bit */

  /** Sample rate of the data from which the spectrum was calculated -- in samples/Second (Hz) */
  ptypUINT uintSampleRate;

  /** Number of points ('bins') in the Fast Fourier Transform (FFT) window */
  ptypUINT uintFFTLength;

  /** Status indicator for the attached spectral data.
      This status is bit-coded as follows (bit #31 is the bit of most significance):
        Bits #31...#23: Reserved
        
        Bits #22...#18  Bits indicating the scan ID 

        Bit  #17:       Flag indicating that this is a fragment of a complete spectrum (e.g. scan spectrum) and the 
                        bins between uintLeftDispInterval and uintRightDispInterval should be overwritten, other 
                        bins shall stay unaffected. If the flag is set the bandwidth is equal to the sample rate.

        Bit  #16:       Flag indicating that the spectrum has been calculated using data that 
                        was flagged as 'blanking'.

        Bit  #15...#12: Bit field indicating the type of samples used for fft calculation

        Bit  #11:       Reserved

        Bit  #10:       Flag indicating that the spectrum was used with samples without level info ('full scale')

        Bit  #9:        Flag indicating that the spectrum has been calculated using data that 
                        was flagged as 'invalid'.
        
        Bit  #8:        Flag indicating the mode with which the spectral data 'level' was calculated.
                        '1' indicates level calculation mode 'logarithmic'
        
        Bits #7...#4:   Bit field indicating the spectral data window(ing) mode   
        
        Bits #3...#0:   Bit field indicating the spectral data display mode */

  ptypUINT uintStatusWord;  /**< Status */

  /** Level reference value.
      Units:  uVolt or 'full scale' -- level calculation mode 'linear' or,
              dBm or dBFS -- level calculation mode 'logarithmic' */
  ptypFLOAT_SP float_spReferenceValue;

  /** The number (starting at 0) of the 'leftmost' bin (FFT point) to be sent (for display). */ 
  ptypUINT uintLeftDispInterval;

  /** The number of the 'rightmost' bin (FFT point) to be sent (for display) */ 
  ptypUINT uintRightDispInterval;

} typSPECDATA_HEADER;

/** Structure type for a spectral data stream data-block - floating point format */
typedef struct struSPECDATA_FLOAT
{
  typFRH_FRAMEHEADER frameheaderHeader;     /**< Generic data stream frame header */
  
  typSPECDATA_HEADER specdataheaderHeader;  /**< Spectrum data specific header    */

  /** Data block containing (uintRightDispInterval-uintLeftDispInterval+1) floating 
      point values */
  ptypFLOAT_SP float_spFFTBin[1/*uintRightDispInterval-uintLeftDispInterval+1*/];

} typSPECDATA_FLOAT;


/** Structure type for a spectral data stream data-block - 16bit fixed point format */
typedef struct struSPECDATA_16BIT
{
  typFRH_FRAMEHEADER frameheaderHeader;     /**< Generic data stream frame header */
  typSPECDATA_HEADER specdataheaderHeader;  /**< Spectrum data specific header    */

  /** Data block containing ceil((uintRightDispInterval-uintLeftDispInterval+1)/2) value pairs.
      Each value pair contains 2x16 Bit signed fractional values.
      Values with even indices are placed in the more significant 16bits.
      Values with odd indices are placed in the less significant 16bits.
      If the number of values is odd, the the last value ('stuffing' value) is zero */
  ptypUINT uintFFTBin[1/* ceil((uintRightDispInterval-uintLeftDispInterval+1)/2) */];

} typSPECDATA_16BIT;


/** Header structure type for spectral data with extended information for samplerates > 4 GHz (e.g. scans) and K faktor*/
typedef struct struSPECDATA_HEADER_EXTENDED
{
  typSPECDATA_HEADER specdataheader;

    /** kFactor of the current antenna to determine field strength in 0.1dB/m.
        kK_FACTOR_NOT_VALID if no kFactor is defined */
  ptypINT  intKFactor;

  /** Sample rate of the data from which the spectrum was calculated -- in samples/Second (Hz) 
      -- 64bit representation - upper 32 Bit */
  ptypUINT uintSampleRate_High;         /**< most  significant 32 Bit use */

} typSPECDATA_HEADER_EXTENDED;




/*---- End of global types declaration -------------------------------------*/

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* ifndef for file (multi)inclusion lock */
/***** End of File ***********************************************************/

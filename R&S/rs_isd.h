/*++++ FILE DESCRIPTION +++++++++++++++++++++++++++++++++++++++++++++++++++++++

@file                   $Workfile: rs_isd.h $
@copyright              (c) 2002 Rohde & Schwarz, Munich
@version                $Revision: \main\6 $


@language               ANSI-C++

COMPILER:               -

@description            Project type-definitions

@see

------ End of file description ----------------------------------------------*/

#ifndef _RS_ISD_H
#define _RS_ISD_H

#include <memory>
#include "rs_gx40x_p_types.h"
#include "rs_gx40x_global_frame_header_if_defs.h"
#include "rs_gx40x_global_ifdata_header_if_defs.h"
#include "rs_gx40x_specdata_if_defs.h"

class CFrameMemory                                                              // Abstract class representing memory for IF data frames.
{
public:
  CFrameMemory() {};
  virtual ~CFrameMemory() {};
  virtual const typFRH_FRAMEHEADER* getFrameHeader() const = 0;                 // Get a pointer to the (pre-filled) frame header.
  virtual typIFD_IFDATAHEADER*      getIQHeader() = 0;                          // Get a pointer to the IQ header of the frame.
  virtual typIFD_DATABLOCK*         getDatablock(unsigned long Datablock) = 0;  // Get a pointer to the given datablock of the frame (must be in the valid range defined in the IQ header).

private:
  CFrameMemory(const CFrameMemory &);
  CFrameMemory&operator=(const CFrameMemory&);
};

enum eSendFrame_Faults          //	The faults raised by the function SendFrame().
{
  kSEND_FRAME__OK        = 0,   // No fault indicated
  kSEND_FRAME__TIMEOUT   = 1,   // Sending IQ data frame timed out
  kSEND_FRAME__FAILED    =-1,   // Sending IQ data frame raised an unknown fault
  kSEND_FRAME__INVALID   =-2,   // Frame content inconsistent
  kSEND_FRAME__UNKNOWN   =-3,   // Unknown error
  kSEND_FRAME__UNEXPECTED=-4,   // Tuner not started
};


// Structure representing memory for IF or spectral data frames.
// All members are for internal use.
typedef struct
{
  HANDLE    h;
  ptypUINT  l;
  void*     p;
} typISD_RAW_MEMORY;

#define ISD_FRAME_MEMORY_2_FRAME_HEADER(memory)   ((typFRH_FRAMEHEADER*)(((ptypUINT*)((memory)->p))+1))
#define ISD_FRAME_MEMORY_2_IQ_HEADER(memory)      ((typIFD_IFDATAHEADER*)(ISD_FRAME_MEMORY_2_FRAME_HEADER(memory)+1))
#define ISD_FRAME_MEMORY_2_IQ_DATABLOCK(memory,i) ((typIFD_DATABLOCK*)(((ptypUINT*)(ISD_FRAME_MEMORY_2_IQ_HEADER(memory)+1)) + ((i)*(ISD_FRAME_MEMORY_2_IQ_HEADER(memory)->uintDatablockLength+sizeof(typIFD_DATABLOCKHEADER)/sizeof(ptypUINT)))))
#define ISD_FRAME_MEMORY_2_SPEC_HEADER(memory)    ((typSPECDATA_HEADER_EXTENDED*)(ISD_FRAME_MEMORY_2_FRAME_HEADER(memory)+1))
#define ISD_FRAME_MEMORY_2_SPEC_DATA(memory)      ((ptypFLOAT_SP*)(ISD_FRAME_MEMORY_2_SPEC_HEADER(memory)+1))

// 'C' methods used from the library 'rs_isd.lib'.

/***** FUNCTION **************************************************************

SPECIFICATION:  Reserve memory for an IF signal data-frame. 

@param          DatablockCount        Amount of datablocks (see typIFD_IFDATAHEADER in rs_gx40x_global_ifdata_header_if_defs.h).
@param          DatablockSizeInWords  Size of a datablock in words (see typIFD_IFDATAHEADER in rs_gx40x_global_ifdata_header_if_defs.h).
@param          Frame                 Reference to a auto_ptr which will represent the memory reserved for the IQ signal data-frame on successful execution.

@retval         true                  Successfully reserved memory, the parameter 'Frame' contains a valid memory block.
@retval         false                 Failure, no memory block was reserved.

*****************************************************************************/
extern "C" bool __declspec(deprecated dllimport) ReserveFrameMemory(unsigned long DatablockCount, 
                                                         unsigned long DatablockSizeInWords, 
                                                         std::auto_ptr<CFrameMemory> &Frame);


/***** FUNCTION **************************************************************

SPECIFICATION:  Send a (previously filled) IF signal data-frame to the Digital Channel Processor (DCP)
                in the sub-system, with which this tuner is associated.

@param          Frame    Reference to the memory reserved for the IQ signal data-frame.
@param          timeout  The timeout time in milliseconds. The value of INFINITE implies infinite timeout time.

@return         See enum ::eSendFrame_Faults above.

*****************************************************************************/
extern "C" eSendFrame_Faults __declspec(deprecated dllimport) SendFrame(std::auto_ptr<CFrameMemory> &Frame, DWORD timeout);


/***** FUNCTION **************************************************************

SPECIFICATION:  Emit a fault indication to the Digital Channel Processor (DCP)
                in the sub-system, to which this tuner associated.

@param          FaultId     The identifier for the fault being emitted.
@param          file        The path by which the pre-processor opened input file which contains the location where the fault was detected, typically '__FILE__'. 
@param          line        The number of the line within the input file, where the fault was detected, typically '__LINE__'.
@param          FaultText   The supplementary text to be emitted, explaining the nature of the detected fault. 

*****************************************************************************/
extern "C" void __declspec(dllimport) EmitFault(ptypEX FaultId, char* file, size_t line, const char *FaultText);


/***** FUNCTION **************************************************************

SPECIFICATION:  Reserve memory for an IF signal data-frame. 

@param          DatablockCount        Amount of datablocks (see typIFD_IFDATAHEADER in rs_gx40x_global_ifdata_header_if_defs.h).
@param          DatablockSizeInWords  Size of a datablock in words (see typIFD_IFDATAHEADER in rs_gx40x_global_ifdata_header_if_defs.h).
@param          memory                Pointer to a structure representing memory for data-frames which will represent the memory reserved for the IQ signal data-frame on successful execution.

@retval         true                  Successfully reserved memory, the parameter 'memory' contains a valid memory block.
@retval         false                 Failure, no memory block was reserved.

*****************************************************************************/
extern "C" bool __declspec(dllimport) ReserveFrameMemoryIQ(unsigned long DatablockCount, 
                                                           unsigned long DatablockSizeInWords, 
                                                           typISD_RAW_MEMORY *memory);

/***** FUNCTION **************************************************************

SPECIFICATION:  Reserve memory for a tuner panorama data-frame.
                The frame should contain the whole panorama.

@param          Bins        Amount of bins of the whole panorama.
@param          memory      Pointer to a structure representing memory for data-frames which will represent the memory reserved for the tuner panorama data-frame on successful execution.

@retval         true        Successfull reserved memory, the parameter 'memory' contains a valid memory block.
@retval         false       Failure, no memory block was reserved.

*****************************************************************************/
extern "C" bool __declspec(dllimport) ReserveFrameMemoryPanorama(unsigned long Bins, 
                                                                 typISD_RAW_MEMORY *memory);


/***** FUNCTION **************************************************************

SPECIFICATION:  Reserve memory for a data-frame for tuner scan.
                The frame may contain only a part of the whole scan range.

@param          Bins        Amount of bins of this part of the scan range.
@param          memory      Pointer to a structure representing memory for data-frames which will represent the memory reserved for the tuner scan data-frame on successful execution.

@retval         true        Successfully reserved memory, the parameter 'memory' contains a valid memory block.
@retval         false       Failure, no memory block was reserved.

*****************************************************************************/
extern "C" bool __declspec(dllimport) ReserveFrameMemoryScan(unsigned long Bins, 
                                                             typISD_RAW_MEMORY *memory);


/***** FUNCTION **************************************************************

SPECIFICATION:  Send a (previously filled) data-frame to the Digital Channel Processor (DCP)
                in the sub-system, with which this tuner is associated.

@param          memory     Pointer to a structure representing memory for a data-frame.
@param          timeout    The timeout time in milliseconds. The value of INFINITE implies infinite timeout time.

@retval         kSEND_FRAME__OK    The data-frame was successfully sent, the structure representing the memory was invalidated.
@retval         other              An error occured, the structure representing the memory is not touched, i.e. the caller must free the memory. See enum ::eSendFrame_Faults above.

*****************************************************************************/
extern "C" eSendFrame_Faults __declspec(dllimport) SendFrameMemory(typISD_RAW_MEMORY *memory, DWORD timeout);


/***** FUNCTION **************************************************************

SPECIFICATION:  Release memory of a data-frame in the case it is not successfully sent via SendFrameMemory().

@param          memory     Pointer to a structure representing memory for a data-frame, which is invalidated on return.

*****************************************************************************/
extern "C" void __declspec(dllimport) ReleaseFrameMemory(typISD_RAW_MEMORY *memory);


/***** FUNCTION **************************************************************

SPECIFICATION:  Report autonomously changed tuner parameters to the system.

@param          parameters    A string with termination '\0' which contains the XML directives constituting the updated tuner parameters.

*****************************************************************************/
extern "C" void __declspec(dllimport) UpdateParameters(const char *parameters);


#endif /* ifndef for file (multi)inclusion lock */
/***** End of File **********************************************************/

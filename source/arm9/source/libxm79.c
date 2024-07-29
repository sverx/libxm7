#include <nds.h>
#include <stdlib.h>  // for malloc()
#include <string.h>  // for memcpy(), memcmp()

#include "../../arm7/source/libxm7.h"

// MOD octave 0 difference
#define AMIGABASEOCTAVE 2
// AmigaPeriods for MOD "Octave ZERO"
u16 AmigaPeriods[12] = { 1712, 1616, 1525, 1440, 1357, 1281, 
                          1209, 1141, 1077, 1017, 961, 907 };

u16 GetAmigaPeriod (u8 note) {  // note from 0 to 95

  u16 period = AmigaPeriods[note % 12];
  int octave = (note / 12) - AMIGABASEOCTAVE;

  if (octave>0)
    period >>= octave;
  else if (octave<0)
    period <<= -octave;
    
  return(period);
}

u8 FindClosestNoteToAmigaPeriod (u16 period) {  // note from 0 to 95
  u8 note=0;
  u16 bottomperiod;
  u16 topperiod;
  
  // jump octaves
  topperiod = GetAmigaPeriod(note);
  while (topperiod >= (period*2) ) {
    note += 12;
    topperiod = GetAmigaPeriod(note);
  }
  
  // jump notes
  bottomperiod=topperiod;
  while (topperiod > period ) {
    bottomperiod=topperiod;
    note++;
    topperiod = GetAmigaPeriod(note);
  }
  
  // find closest
  if ((period-topperiod) <= (bottomperiod-period))
    return (note);
  else
    return (note-1);
}

u16 SwapBytes (u16 in) {
  return ((in & 0x00FF) << 8 | (in >> 8));
}

u8 IdentifyMOD (char c1, char c2, char c3, char c4) {

  // 4 channels MOD
  if ((c1=='M') && (c2=='.') && (c3=='K') && (c4=='.'))   // M.K.
    return (4);

  if ((c1=='M') && (c2=='!') && (c3=='K') && (c4=='!'))   // M!K!
    return (4);
    
  if ((c1=='F') && (c2=='L') && (c3=='T') && (c4=='4'))   // FLT4 [Startrekker]
    return (4);
    
  //if ((c1=='E') && (c2=='X') && (c3=='O') && (c4=='4'))   // EXO4 (should be FM)
  //  return (4);


  // 8 channels MOD  
  if ((c1=='O') && (c2=='C') && (c3=='T') && (c4=='A'))   // OCTA
    return (8);
    
  if ((c1=='C') && (c2=='D') && (c3=='8') && (c4=='1'))   // CD81 [Falcon]
    return (8);
    
  // FLT8 support (!!!)
  if ((c1=='F') && (c2='L') && (c3='T') && (c4='8'))       // FLT8 [Startrekker]
    return (8 | 0x80);
    
  // if ((c1=='E') && (c2='X') && (c3='O') && (c4='8'))       // EXO8 (same as FLT8) (should be FM)
  //   return (8 | 0x80);
    
    
  // xCHN (2-4-6-8 chns & 5-7-9 chns)
  if ((c2=='C') && (c3=='H') && (c4=='N')) {                  // ?CHN
    if ((c1=='2') || (c1=='4') || (c1=='6') || (c1=='8') ||  // 2CHN - 4CHN(?) - 6CHN - 8CHN
        (c1=='5') || (c1=='7') || (c1=='9'))                  // 5CHN - 7CHN - 9CHN [TakeTracker]
      return (c1-'0');
  }
  
  // xxCH  (10-12-14-16-18-20-22-24-26-28-30-32 chns & 11-13-15 [TakeTracker])
  if ((c3=='C') && (c4=='H')) {
    if ((c1>='1') && (c1<='3') && (c2>='0') && (c2<='8')) {
      return ((c1-'0')*10+(c2-'0'));
    }
  }
  
  // TDZx (1-2-3 chns [TakeTracker])
  if ((c1=='T') && (c2=='D') && (c3=='Z')) {                  // TDZ?
    if ((c4=='1') || (c4=='2') || (c4=='3'))                  // TDZ1 - TDZ2 - TDZ3 [TakeTracker]
      return (c4-'0');
  }

  return (0);
}

XM7_SingleNoteArray_Type* PrepareNewPattern (u16 len, u8 chn) {
  // prepares a new EMPTY pattern with LEN lines and CNH channels

  XM7_SingleNoteArray_Type* ptr;
  u16 i,cnt;

  cnt= len*chn;
  ptr= (XM7_SingleNoteArray_Type*) malloc ( sizeof(XM7_SingleNote_Type) * cnt );

  // check if memory has been allocated before using it  
  if (ptr!=NULL) {
    for (i=0;i<cnt;i++) {
      ptr->Noteblock[i].Note=0x00;
	  ptr->Noteblock[i].Instrument=0x00;
	  ptr->Noteblock[i].Volume=0x00;
	  ptr->Noteblock[i].EffectType=0x00;
	  ptr->Noteblock[i].EffectParam=0x00;
    }
  }
  
  return (ptr);
}

XM7_Instrument_Type* PrepareNewInstrument (void) {
  // prepares a new EMPTY instrument
  
  XM7_Instrument_Type* ptr = (XM7_Instrument_Type*) malloc ( sizeof (XM7_Instrument_Type));
  
  // check if memory has been allocated before using it
  if (ptr!=NULL) {
  
    ptr->NumberofSamples=0;            // 0 samples when empty
    
    int i;
    for (i=0;i<16;i++)
      ptr->Sample[i]=NULL;             // all the pointer to the samples are NULL

    // set the name to empty  
    memset (ptr->Name, 0, 22);         // 22 asciizero
  }
  
  return (ptr);
}

XM7_Sample_Type* PrepareNewSample (u32 len, u32 looplen, u8 flags) {
  // prepares a new EMPTY sample
  
  XM7_SampleData_Type* data_ptr;
  XM7_Sample_Type* ptr;
  
  u32 malloclen = len;
  
  if ((flags & 0x03) == 0x02) {  // it's a ping-pong loop so malloclen should be calculated 'better'
  
    /*
    if ((flags & 0x10) == 0) {
	  // 8 bit/sample
      malloclen += (looplen - 2);         // -2 samples because of the 1st and last which shouldn't be duplicated
	} else {
	  // 16 bit/sample
	  malloclen += (looplen - 4);         // -2 samples (-4 bytes) because of the 1st and last which shouldn't be duplicated
	}
	
	*/
	
	malloclen += looplen;         // adds the portion that gets reverted
	
  }
  
  ptr = (XM7_Sample_Type*) malloc ( sizeof (XM7_Sample_Type) );
	
  // check if memory has been allocated before using it
  if (ptr!=NULL) {

    data_ptr = (XM7_SampleData_Type*) malloc ( sizeof (u8) * malloclen);
     
    // check if SAMPLE memory has been allocated before using it
    if (data_ptr!=NULL) {
     
      ptr->SampleData = data_ptr;
      ptr->Length = len;            // yes, len, not malloclen.
                                    //  this will be fixed later
     
      // set the name to empty  
      memset (ptr->Name, 0, 22);   // 22 asciizero
	} else {
	  // SAMPLE memory not allocated, REMOVE the parent
	  free (ptr);
      ptr=NULL;
	}
  }
    
  return (ptr);
}

u16 XM7_LoadXM (XM7_ModuleManager_Type* Module, XM7_XMModuleHeader_Type* XMModule) {  // returns 0 if OK, an error otherwise

  // reset these values
  Module->NumberofPatterns = 0;
  Module->NumberofInstruments = 0;

  // check the ID text and the 0x1a
  if ((memcmp(XMModule->FixedText,"Extended Module: ",17)!=0) || (XMModule->FixedChar!=0x1a))  {
    Module->State = XM7_STATE_ERROR | XM7_ERR_NOT_A_VALID_MODULE;
    return (XM7_ERR_NOT_A_VALID_MODULE);
  }	

  // check the version of the module
  if ((XMModule->Version!=0x103) && (XMModule->Version!=0x104))  {
    Module->State = XM7_STATE_ERROR | XM7_ERR_UNKNOWN_MODULE_VERSION;
    return (XM7_ERR_UNKNOWN_MODULE_VERSION);
  }	

  // check how may channels are in the module
  if (XMModule->NumberofChannels>16)  {
    Module->State = XM7_STATE_ERROR | XM7_ERR_UNSUPPORTED_NUMBER_OF_CHANNELS;
    return (XM7_ERR_UNSUPPORTED_NUMBER_OF_CHANNELS);
  }
  
  // load all the needed info from the header
  Module->ModuleLength = XMModule->SongLength;
  Module->RestartPoint = XMModule->RestartPosition;
  Module->NumberofChannels = XMModule->NumberofChannels;
  Module->NumberofInstruments = XMModule->NumberofInstruments;
  Module->NumberofPatterns = XMModule->NumberofPatterns;
  Module->FreqTable = XMModule->XMModuleFlags;
  Module->DefaultTempo = XMModule->DefaultTempo;
  Module->DefaultBPM = XMModule->DefaultBPM;

  memcpy (Module->ModuleName,XMModule->XMModuleName,20);  // char[20]
  memcpy (Module->TrackerName,XMModule->TrackerName,20);  // char[20]
  memcpy (Module->PatternOrder,XMModule->PatternOrder,XMModule->HeaderSize-20);  // u8[]
  
  // the MODULE header is finished!
  
  // BETA TEST
  // return (0);
  
  // now working on the patterns
  u16 CurrentPattern;
  XM7_XMPatternHeader_Type* XMPatternHeader = (XM7_XMPatternHeader_Type*) &(XMModule->PatternOrder[XMModule->HeaderSize-20]);
  
  // BETA TEST
  // Module->NumberofPatterns=1;
  
  for (CurrentPattern=0;CurrentPattern<(Module->NumberofPatterns);CurrentPattern++) {
  
    // check if the PATTERN header is ok
    if ((XMPatternHeader->HeaderLength!=9) || (XMPatternHeader->PackingType!=0)) {
      Module->NumberofPatterns = CurrentPattern;
	  Module->NumberofInstruments = 0;
      Module->State = XM7_STATE_ERROR | XM7_ERR_UNSUPPORTED_PATTERN_HEADER;
      return (XM7_ERR_UNSUPPORTED_PATTERN_HEADER);
    }	
  
    // check if the PATTERN lenght is ok
    if ((XMPatternHeader->NumberofLinesinThisPattern<1) || (XMPatternHeader->NumberofLinesinThisPattern>256)) {
      Module->NumberofPatterns = CurrentPattern;
	  Module->NumberofInstruments = 0;
      Module->State = XM7_STATE_ERROR | XM7_ERR_UNSUPPORTED_PATTERN_HEADER;
      return (XM7_ERR_UNSUPPORTED_PATTERN_HEADER);
    }	
  
    // pattern is ok! Get the length
    Module->PatternLength[CurrentPattern]=XMPatternHeader->NumberofLinesinThisPattern;
	
	// Prepare an empty pattern for the data
	Module->Pattern[CurrentPattern] = PrepareNewPattern (Module->PatternLength[CurrentPattern],Module->NumberofChannels);
	if (Module->Pattern[CurrentPattern]==NULL) {
	  Module->NumberofPatterns=CurrentPattern;
	  Module->NumberofInstruments = 0;
	  Module->State = XM7_STATE_ERROR | XM7_ERR_NOT_ENOUGH_MEMORY;
	  return (XM7_ERR_NOT_ENOUGH_MEMORY);
	}
    
    // decode the PackedData
    u16 i=0;
	u16 wholenote=0;
	u8 firstbyte;
	
	XM7_SingleNoteArray_Type* thispattern=Module->Pattern[CurrentPattern];
	
    while (i<(XMPatternHeader->PackedPatterndataLength)) {
	  firstbyte=XMPatternHeader->PatternData[i];
	  
	  if (firstbyte & 0x80) {
	    // it's compressed: skip to the next byte
	  	i++;
      } else {
	    // if "it's NOT compressed" then there are 5 bytes, simulate it's compressed with 5 bytes following
		firstbyte=0x1F;
	  }
     
	  // if next is a NOTE:
	  if (firstbyte & 0x01) {
		// read the note
		thispattern->Noteblock[wholenote].Note = XMPatternHeader->PatternData[i];
		i++;
	  }
		
	  // if next is a INSTRUMENT:
	  if (firstbyte & 0x02) {
		// read the instrument
		thispattern->Noteblock[wholenote].Instrument = XMPatternHeader->PatternData[i];
		i++;
	  }
     
	  // if next is a VOLUME:
	  if (firstbyte & 0x04) {
	    // read the volume
		thispattern->Noteblock[wholenote].Volume = XMPatternHeader->PatternData[i];
		i++;
	  }
		
      // if next is an EFFECT TYPE:
	  if (firstbyte & 0x08) {
		// read the effect type
		thispattern->Noteblock[wholenote].EffectType = XMPatternHeader->PatternData[i];
		i++;
      }
		
	  // if next is an EFFECT PARAM:
	  if (firstbyte & 0x10) {
		// read the effect param
		thispattern->Noteblock[wholenote].EffectParam = XMPatternHeader->PatternData[i];
		i++;
	  }
     
	  wholenote++;  // get ready for the next note
	  
    } // end of patterndata
	
	// if the pattern contains data...
	if (XMPatternHeader->PackedPatterndataLength>0) {
	  // ... check if the pattern contained all the notes it should!
	  if (wholenote!=((Module->PatternLength[CurrentPattern]) * (Module->NumberofChannels))) {
	  	Module->NumberofPatterns=CurrentPattern + 1;
	    Module->NumberofInstruments = 0;
	    Module->State = XM7_STATE_ERROR | XM7_ERR_INCOMPLETE_PATTERN;
        return (XM7_ERR_INCOMPLETE_PATTERN);
	  }
	}
	
	// get ready for next pattern!
	XMPatternHeader = (XM7_XMPatternHeader_Type*) &(XMPatternHeader->PatternData[i]);

  }  // end 'pattern' for
  
  // BETA TEST
  // return (0);
  
  // patterns are finished
  
  // set all the instrumen pointer to NULL
  u16 CurrentInstrument;
  for (CurrentInstrument=0;CurrentInstrument<128;CurrentInstrument++)
    Module->Instrument[CurrentInstrument]=NULL;
  
  // let's load the instruments!
  XM7_XMInstrument1stHeader_Type* XMInstrument1Header = (XM7_XMInstrument1stHeader_Type*) XMPatternHeader;
  
  // BETA TEST
  // Module->NumberofInstruments=1;
  
  for (CurrentInstrument=0;CurrentInstrument<((Module->NumberofInstruments));CurrentInstrument++) {
  
    // check if the INSTRUMENT header is ok  (I'm unsure of the header length...)
	// NOTE: I've found some XM with HeaderLength!=0x107 and Type!=0 (Type=80) so I'm trashing the following check...
	//       ... then found also type=anything which has meaning so really don't trust this 'Type' field
	/*
    if ((XMInstrument1Header->HeaderLength!=0x107) && (XMInstrument1Header->Type!=0)) {
      Module->State = STATE_ERROR | ERR_UNSUPPORTED_INSTRUMENT_HEADER;
      return (ERR_UNSUPPORTED_INSTRUMENT_HEADER);
    }
    */

    // check if the INSTRUMENT lenght is ok  (max 16 samples!) (and can be ZERO!)
    if (XMInstrument1Header->NumberofSamples>16) {
	  Module->NumberofInstruments = CurrentInstrument;
      Module->State = XM7_STATE_ERROR | XM7_ERR_UNSUPPORTED_INSTRUMENT_HEADER;
      return (XM7_ERR_UNSUPPORTED_INSTRUMENT_HEADER);
    }
	
	// allocate the new instrument
	Module->Instrument[CurrentInstrument] = PrepareNewInstrument ();
	if (Module->Instrument[CurrentInstrument]==NULL) {
	  Module->NumberofInstruments=CurrentInstrument;
	  Module->State = XM7_STATE_ERROR | XM7_ERR_NOT_ENOUGH_MEMORY;
	  return (XM7_ERR_NOT_ENOUGH_MEMORY);
	}
	
	XM7_Instrument_Type* CurrentInstrumentPtr = Module->Instrument[CurrentInstrument];
	
	// load the info from the header
	CurrentInstrumentPtr->NumberofSamples=XMInstrument1Header->NumberofSamples;
	memcpy(CurrentInstrumentPtr->Name,XMInstrument1Header->Name,22);    // char[22]
	
	if (XMInstrument1Header->NumberofSamples>0) {
	  // get the 2nd part of the header	
	  XM7_XMInstrument2ndHeader_Type* XMInstrument2Header = (XM7_XMInstrument2ndHeader_Type*) &(XMInstrument1Header->NextHeaderPart[0]);
	  
	  // 2009! HNY!
	  // check the length of the instrument header before proceed!
	  
	  if (XMInstrument1Header->InstrumentHeaderLength >= 33+96) {
	    // copy the 96 notes' sample numbers
	    memcpy (CurrentInstrumentPtr->SampleforNote,XMInstrument2Header->SampleforNotes,96);  // u8[96]
	  } else {
        // fill with 0 all the samples numbers
      	memset (CurrentInstrumentPtr->SampleforNote,0,96);  
      }
		
	  // if (XMInstrument1Header->InstrumentHeaderLength >= 33+96+123) {
      if (XMInstrument1Header->InstrumentHeaderLength >= 33+96+123-22) {
	    // read all the data about Volume&Envelope points (the 22 'reserved' bytes can be absent)
	    memcpy ((u8*)CurrentInstrumentPtr->VolumeEnvelopePoint,(u8*)XMInstrument2Header->VolumeEnvelopePoints,48);   // 12x2x2=u8[48]
	    memcpy ((u8*)CurrentInstrumentPtr->PanningEnvelopePoint,(u8*)XMInstrument2Header->PanningEnvelopePoints,48); // 12x2x2=u8[48]
      
	    CurrentInstrumentPtr->NumberofVolumeEnvelopePoints=XMInstrument2Header->NumberofVolumePoints;
        CurrentInstrumentPtr->NumberofPanningEnvelopePoints=XMInstrument2Header->NumberofPanningPoints;
	  
	    CurrentInstrumentPtr->VolumeSustainPoint=XMInstrument2Header->VolumeSustainPoint;
	    CurrentInstrumentPtr->VolumeLoopStartPoint=XMInstrument2Header->VolumeLoopStartPoint;
	    CurrentInstrumentPtr->VolumeLoopEndPoint=XMInstrument2Header->VolumeLoopEndPoint;
     
	    CurrentInstrumentPtr->PanningSustainPoint=XMInstrument2Header->PanningSustainPoint;
	    CurrentInstrumentPtr->PanningLoopStartPoint=XMInstrument2Header->PanningLoopStartPoint;
	    CurrentInstrumentPtr->PanningLoopEndPoint=XMInstrument2Header->PanningLoopEndPoint;
	  
	    CurrentInstrumentPtr->VolumeType=XMInstrument2Header->VolumeType;    //  bit 0: On; 1: Sustain; 2: Loop
	    CurrentInstrumentPtr->PanningType=XMInstrument2Header->PanningType;  //  bit 0: On; 1: Sustain; 2: Loop
	  
	    // Instrument Vibrato
        CurrentInstrumentPtr->VibratoType  = XMInstrument2Header->VibratoType;
        CurrentInstrumentPtr->VibratoSweep = 0x10000/(XMInstrument2Header->VibratoSweep+1);
        CurrentInstrumentPtr->VibratoDepth = XMInstrument2Header->VibratoDepth;
        CurrentInstrumentPtr->VibratoRate  = XMInstrument2Header->VibratoRate;
	  
	    // envelope volume fadeout
	    CurrentInstrumentPtr->VolumeFadeout = XMInstrument2Header->VolumeFadeOut;
	  } else {
        // there are NO envelopes in the file...
        CurrentInstrumentPtr->VolumeType  =0;
        CurrentInstrumentPtr->PanningType =0;
	    CurrentInstrumentPtr->NumberofVolumeEnvelopePoints  =0;
	    CurrentInstrumentPtr->NumberofPanningEnvelopePoints =0;
        // if there's no vibrato in the file...
        CurrentInstrumentPtr->VibratoType  =0;
        CurrentInstrumentPtr->VibratoSweep =0;
        CurrentInstrumentPtr->VibratoDepth =0;
        CurrentInstrumentPtr->VibratoRate  =0;
        // if there's no fadeout in the file...
        CurrentInstrumentPtr->VolumeFadeout =0;
      }
      
	  // InstrumentHeader (2nd part) is finished!
	  XM7_XMSampleHeader_Type* XMSampleHeader = (XM7_XMSampleHeader_Type*) ((u8*)&XMInstrument1Header->InstrumentHeaderLength + XMInstrument1Header->InstrumentHeaderLength);
	  
	  u8 CurrentSample;
	  
	  // read all the sample headers
	  for (CurrentSample=0;CurrentSample<(CurrentInstrumentPtr->NumberofSamples);CurrentSample++) {
	  
	    // allocate the new Sample
	    CurrentInstrumentPtr->Sample[CurrentSample] = PrepareNewSample (XMSampleHeader->Length, 
		                                               XMSampleHeader->LoopLength, XMSampleHeader->Type);
        if (CurrentInstrumentPtr->Sample[CurrentSample]==NULL) {
		  Module->NumberofInstruments = CurrentInstrument+1;
	      CurrentInstrumentPtr->NumberofSamples=CurrentSample;
          Module->State = XM7_STATE_ERROR | XM7_ERR_NOT_ENOUGH_MEMORY;
	      return (XM7_ERR_NOT_ENOUGH_MEMORY);
	    }
      
		XM7_Sample_Type* CurrentSamplePtr = CurrentInstrumentPtr->Sample[CurrentSample];
		
		// read all the data
		CurrentSamplePtr->LoopStart  = XMSampleHeader->LoopStart;
		CurrentSamplePtr->LoopLength = XMSampleHeader->LoopLength;
		CurrentSamplePtr->Volume     = XMSampleHeader->Volume;
		CurrentSamplePtr->FineTune   = XMSampleHeader->FineTune;
		CurrentSamplePtr->Flags      = XMSampleHeader->Type;
		// if loop type is 0x03 it becomes 'forward', because XMs shouldn't support 0x03 loops...
		if ((CurrentSamplePtr->Flags & 0x0F) == 0x03)
		  CurrentSamplePtr->Flags = (CurrentSamplePtr->Flags & 0xF0) | 0x01;
		// end
		CurrentSamplePtr->Panning      = XMSampleHeader->Panning;
		CurrentSamplePtr->RelativeNote = XMSampleHeader->RelativeNote;
		memcpy (CurrentSamplePtr->Name,XMSampleHeader->Name,22);    // char[22]
		
		// point to the next sample header (or the 1st byte after all the headers...)
		XMSampleHeader = (XM7_XMSampleHeader_Type*)  &(XMSampleHeader->NextHeader[0]);
	  }
	  
	  // the 1st byte after the header(s)... (both for the 8 and the 16 bit samples...)
	  XM7_SampleData_Type* SampleData     = (XM7_SampleData_Type*) XMSampleHeader;
	  XM7_SampleData16_Type* SampleData16 = (XM7_SampleData16_Type*) XMSampleHeader;
	  
	  // read all the sample data
	  for (CurrentSample=0;CurrentSample<(CurrentInstrumentPtr->NumberofSamples);CurrentSample++) {
	  
	    // get a pointer to the data space
		XM7_Sample_Type* CurrentSamplePtr = CurrentInstrumentPtr->Sample[CurrentSample];
		XM7_SampleData_Type*   CurrentSampleDataPtr   = (XM7_SampleData_Type*)   CurrentSamplePtr->SampleData;
		XM7_SampleData16_Type* CurrentSampleData16Ptr = (XM7_SampleData16_Type*) CurrentSamplePtr->SampleData;
		
		// read the sample, finally!
		
		// check if sample is 8 or 16 bit first!
		u32 i,j;
		if ((CurrentSamplePtr->Flags & 0x10)==0) {
		  // it's an 8 bit sample
		  s8 old=0;
		  for (i=0;i<CurrentSamplePtr->Length;i++) {
		    // samples in XM files are stored as delta value (nobody knows why...)
		    old+=SampleData->Data[i];
		    CurrentSampleDataPtr->Data[i]=old;
		  }
		  
		  // ping-pong loop conversion START
		  // since DS has got no support for ping/pong loop, we should duplicate the loop backward
		  if ((CurrentSamplePtr->Flags & 0x03) == 0x02)
		  {
		  
		    /*
		    for (j=0;j<(CurrentSamplePtr->LoopLength-2);j++) {
		      CurrentSampleDataPtr->Data[CurrentSamplePtr->Length+j] = CurrentSampleDataPtr->Data[(CurrentSamplePtr->Length-1)-j];
		    }
			*/
			
			for (j=0;j<CurrentSamplePtr->LoopLength;j++)
		      CurrentSampleDataPtr->Data[CurrentSamplePtr->Length+j] = CurrentSampleDataPtr->Data[CurrentSamplePtr->Length-j];
		    
			// and change it to a 'normal' loop (preserving 16 bit flag)
		    CurrentSamplePtr->Flags = (CurrentSamplePtr->Flags & 0xF0) | 0x01;
			
			// the lenght of the sample must be changed
			// CurrentSamplePtr->Length += (CurrentSamplePtr->LoopLength - 2);
			CurrentSamplePtr->Length += CurrentSamplePtr->LoopLength;
			
			// of course also LoopLength has to be changed!
			CurrentSamplePtr->LoopLength += CurrentSamplePtr->LoopLength;
		  }
		  // ping-pong loop conversion FINISHED
		  
		  
		  // prepare for the next sample (both pointers!)
		  SampleData   = (XM7_SampleData_Type*) &(SampleData->Data[i]);
          SampleData16 = (XM7_SampleData16_Type*) SampleData;
          
		} else {
		  // it's a 16 bit sample
		  s16 old16=0;
		  for (i=0;(i<CurrentSamplePtr->Length>>1); i++) {  // lenght is in bytes anyway
            old16+=SampleData16->Data[i];
		    CurrentSampleData16Ptr->Data[i]=old16;
		  }
          
		  // ping-pong loop conversion START
		  // since DS has got no support for ping/pong loop, we should duplicate the loop backward
		  if ((CurrentSamplePtr->Flags & 0x03) == 0x02)
		  {
		  
		    /*
		    for (j=0;j<((CurrentSamplePtr->LoopLength >> 1) -2);j++) {  // -2 because we won't duplicate 1st and last
		      CurrentSampleData16Ptr->Data[(CurrentSamplePtr->Length >> 1) +j] = CurrentSampleData16Ptr->Data[(CurrentSamplePtr->Length >> 1)-(j+1)];
		    }
			*/
			
			for (j=0;j<(CurrentSamplePtr->LoopLength >> 1);j++)
		      CurrentSampleData16Ptr->Data[(CurrentSamplePtr->Length >> 1) +j] = CurrentSampleData16Ptr->Data[(CurrentSamplePtr->Length >> 1)-j];
		    
			// and change it to a 'normal' loop (preserving 16 bit flag)
		    CurrentSamplePtr->Flags = (CurrentSamplePtr->Flags & 0xF0) | 0x01;
			
			// the lenght of the sample must be changed (it's in bytes)
			// CurrentSamplePtr->Length += (CurrentSamplePtr->LoopLength - 4);
			CurrentSamplePtr->Length += CurrentSamplePtr->LoopLength;
			
			// of course also LoopLength has to be changed! (it's in bytes)
			// CurrentSamplePtr->LoopLength += (CurrentSamplePtr->LoopLength - 4);
			CurrentSamplePtr->LoopLength += CurrentSamplePtr->LoopLength;
		  }
		  // ping-pong loop conversion FINISHED
		  
		  // prepare for the next sample (both pointers!)
		  SampleData16 = (XM7_SampleData16_Type*) &(SampleData16->Data[i]);
          SampleData   = (XM7_SampleData_Type*) SampleData16;
		}
		
	  }   // finished reading all the samples
	  
	  // prepare for the next instrument (the next byte after the last sample
	  XMInstrument1Header = (XM7_XMInstrument1stHeader_Type*)SampleData;
	  
	  // end if  (samples>0)
    } else { 
	  // if (samples=0)
	  // should be like this...
	  // XMInstrument1Header = (XMInstrument1stHeader_Type*) &(XMInstrument1Header->NextHeaderPart[0]);
	  
	  // ...but it seems like THERE IS the 2nd header too even when NO samples... 
	  // get the 2nd part of the header
	  
	  // NOTE: I've found some XM with instruments with 0 samples so I trash the following 2 lines
	  /*
	  XMInstrument2ndHeader_Type* XMInstrument2Header = (XMInstrument2ndHeader_Type*) &(XMInstrument1Header->NextHeaderPart[0]);
	  XMInstrument1Header = (XMInstrument1stHeader_Type*) &(XMInstrument2Header->NextDataPart[0]);
	  */ 
	  
	  XMInstrument1Header = (XM7_XMInstrument1stHeader_Type*) &(XMInstrument1Header->NextHeaderPart[(XMInstrument1Header->InstrumentHeaderLength)-(sizeof(XM7_XMInstrument1stHeader_Type))+1]);
	  
	}  
  }  // end "for Instruments"
  
  // Set standard panning
  Module->AmigaPanningEmulation = XM7_PANNING_TYPE_NORMAL;
  Module->AmigaPanningDisplacement = 0x00;
  
  // Replay style FT2 for XM
  Module->ReplayStyle = XM7_REPLAY_STYLE_FT2;
  
  // set State
  Module->State = XM7_STATE_READY;
  
  // end OK!
  return (0);
}


u16 XM7_LoadMOD (XM7_ModuleManager_Type* Module, XM7_MODModuleHeader_Type* MODModule) {
  // returns 0 if OK, an error otherwise
  
  int FLT8Flag;
  
  // file format ID check
  Module->NumberofChannels = IdentifyMOD (MODModule->FileFormat[0],MODModule->FileFormat[1],
                                           MODModule->FileFormat[2],MODModule->FileFormat[3]);
                                           
  FLT8Flag = (Module->NumberofChannels >> 7);
  Module->NumberofChannels &= 0x3f;

  // if MOD has not been identified:
  if (Module->NumberofChannels==0) {
    Module->NumberofInstruments = 0;
    Module->NumberofPatterns = 0;
	Module->State = XM7_STATE_ERROR | XM7_ERR_NOT_A_VALID_MODULE;
	return (XM7_ERR_NOT_A_VALID_MODULE);
  }
  
  // if MOD has too many channels:
  if (Module->NumberofChannels>16) {
    Module->NumberofInstruments = 0;
    Module->NumberofPatterns = 0;
	Module->State = XM7_STATE_ERROR | XM7_ERR_UNSUPPORTED_NUMBER_OF_CHANNELS;
	return (XM7_ERR_UNSUPPORTED_NUMBER_OF_CHANNELS);
  }

  // set these values, constants in a MOD file
  Module->NumberofInstruments = 31;                              // 31 instrums, default
  Module->FreqTable    = 0;                                      // AMIGA Freq. Table ( ==0 !)
  Module->DefaultTempo = 6;                                      // Spd=6, default
  Module->DefaultBPM   = 125;                                    // BPM=125, default
  memcpy (Module->TrackerName,"**** MOD Module ****",20);        // char[20]
  
  // load all the needed info from the header
  Module->ModuleLength = MODModule->SongLength;
  Module->RestartPoint = (MODModule->RestartPosition>=127)?0:MODModule->RestartPosition;
  memcpy (Module->PatternOrder, MODModule->PatternOrder, 128);   // u8[128]
  memcpy (Module->ModuleName,MODModule->MODModuleName,20);      // char[20]

 // calculate how many patterns are there in the module (in a safe way)
  Module->NumberofPatterns=0;
  int i;
  for (i=0;i<128;i++) {
    // divide by 2 if it's FLT8
    if (FLT8Flag)
      Module->PatternOrder[i] >>=1;
      
    if (Module->PatternOrder[i] > Module->NumberofPatterns)
      Module->NumberofPatterns=Module->PatternOrder[i];
  }
   
  // higher number+1
  Module->NumberofPatterns++;
  // the MODULE header is finished!
  
  // now working on the instrument headers (instruments are always 31)
  int CurrentInstrument;
  for (CurrentInstrument=0;CurrentInstrument<Module->NumberofInstruments;CurrentInstrument++) {
  
    // check if I need to allocate this instrument (or is it empty?)
    // NOTE: if len==1 then IT'S EMPTY (!!!)
    if (SwapBytes(MODModule->Instrument[CurrentInstrument].Length)>1) {
     
      // allocate the new instrument
	  Module->Instrument[CurrentInstrument] = PrepareNewInstrument ();
	  if (Module->Instrument[CurrentInstrument]==NULL) {
	    Module->NumberofInstruments=CurrentInstrument;
        Module->NumberofPatterns = 0;
	    Module->State = XM7_STATE_ERROR | XM7_ERR_NOT_ENOUGH_MEMORY;
	    return (XM7_ERR_NOT_ENOUGH_MEMORY);
	  }
	 
  	  XM7_Instrument_Type* CurrentInstrumentPtr = Module->Instrument[CurrentInstrument];
	 
	  // load the info from the header
	  CurrentInstrumentPtr->NumberofSamples=1;
	  memcpy(CurrentInstrumentPtr->Name,MODModule->Instrument[CurrentInstrument].Name,22);    // char[22]
     
      // no multisample, it's a MOD
      memset (CurrentInstrumentPtr->SampleforNote,0,96);
     
      // NO envelopes in a MOD
      CurrentInstrumentPtr->VolumeType  =0;
      CurrentInstrumentPtr->PanningType =0;
	  CurrentInstrumentPtr->NumberofVolumeEnvelopePoints  =0;
	  CurrentInstrumentPtr->NumberofPanningEnvelopePoints =0;
     
      // NO auto vibrato in a MOD
      CurrentInstrumentPtr->VibratoType  =0;
      CurrentInstrumentPtr->VibratoSweep =0;
      CurrentInstrumentPtr->VibratoDepth =0;
      CurrentInstrumentPtr->VibratoRate  =0;
     
      // NO fadeout in a MOD
      CurrentInstrumentPtr->VolumeFadeout =0;
     
      // allocate space for the (only) sample
	  CurrentInstrumentPtr->Sample[0] = PrepareNewSample (SwapBytes(MODModule->Instrument[CurrentInstrument].Length)*2,
                                                           SwapBytes(MODModule->Instrument[CurrentInstrument].LoopLength)*2, 0);
     
      if (CurrentInstrumentPtr->Sample[0]==NULL) {
	    Module->NumberofInstruments = CurrentInstrument+1;
        Module->NumberofPatterns = 0;
        Module->State = XM7_STATE_ERROR | XM7_ERR_NOT_ENOUGH_MEMORY;
	    return (XM7_ERR_NOT_ENOUGH_MEMORY);
	  }
      
	  XM7_Sample_Type* CurrentSamplePtr = CurrentInstrumentPtr->Sample[0];
		
	  // read all the data
      CurrentSamplePtr->Length     = SwapBytes(MODModule->Instrument[CurrentInstrument].Length)*2;
      u8 TmpFineTune   = MODModule->Instrument[CurrentInstrument].FineTune & 0x0F;
      
      // signed nibble (-8 ... + 7)  ->  signed byte*16 (-128 ... +127)
      if (TmpFineTune<=7)
        CurrentSamplePtr->FineTune = TmpFineTune * 16;
      else
        CurrentSamplePtr->FineTune = -(16-TmpFineTune)*16;
      
	  CurrentSamplePtr->Volume     = MODModule->Instrument[CurrentInstrument].Volume;
	  CurrentSamplePtr->LoopStart  = SwapBytes(MODModule->Instrument[CurrentInstrument].LoopStart)*2;
	  CurrentSamplePtr->LoopLength = SwapBytes(MODModule->Instrument[CurrentInstrument].LoopLength)*2;
      
      // stupid MOD bug PATCH: if LoopLength is 2 then there's really NO loop.
      if (CurrentSamplePtr->LoopLength==2)
        CurrentSamplePtr->LoopLength = 0;
     
      CurrentSamplePtr->Flags      = (CurrentSamplePtr->LoopLength>0)?1:0;  // 8 bit, forward loop
	  
      CurrentSamplePtr->Panning      = 0x80;  // default: center (there's no pan in mod samples)
	  CurrentSamplePtr->RelativeNote = 0;     // there's no such thing in MOD
	  
      // copy instrument name in sample name
      memcpy (CurrentSamplePtr->Name,CurrentInstrumentPtr->Name,22);    // char[22]
    } else {
      // there's no sample in this instrument, so NULL the instrument
      Module->Instrument[CurrentInstrument] = NULL;
    }
  }
  // instrument headers and sample space preparation finished.

  // now working on the patterns
  int CurrentPattern;
  XM7_MODPattern_Type* MODPattern = (XM7_MODPattern_Type*) &(MODModule->NextDataPart);

  for (CurrentPattern=0;CurrentPattern<(Module->NumberofPatterns);CurrentPattern++) {
  
    // Set pattern length (always 64 in MOD)
    Module->PatternLength[CurrentPattern]=64;
	
	// Prepare an empty pattern for the data
	Module->Pattern[CurrentPattern] = PrepareNewPattern (Module->PatternLength[CurrentPattern],Module->NumberofChannels);
	if (Module->Pattern[CurrentPattern]==NULL) {
	  Module->NumberofPatterns=CurrentPattern;
	  Module->State = XM7_STATE_ERROR | XM7_ERR_NOT_ENOUGH_MEMORY;
	  return (XM7_ERR_NOT_ENOUGH_MEMORY);
	}
    
    XM7_SingleNoteArray_Type* thispattern=Module->Pattern[CurrentPattern];
    
    // decode the pattern
    int row,chn,period,curs,curr=0;
    for (row=0;row<Module->PatternLength[CurrentPattern];row++) {
      for (chn=0;chn<Module->NumberofChannels;chn++) {
        curr=row*Module->NumberofChannels+chn;
        if (FLT8Flag)
          curs=row*4+chn+((chn<4)?0:256-4);
        else
          curs=curr;
        period=MODPattern->SingleNote[curs].PeriodL + ((MODPattern->SingleNote[curs].PeriodH & 0x0F) * 256);
        thispattern->Noteblock[curr].Note = (period!=0)?1+FindClosestNoteToAmigaPeriod(period):0;
        thispattern->Noteblock[curr].Instrument = (MODPattern->SingleNote[curs].Instr_EffType >> 4) | (MODPattern->SingleNote[curs].PeriodH & 0x10);
        thispattern->Noteblock[curr].Volume = 0;   // there's no such info here
        thispattern->Noteblock[curr].EffectType = (MODPattern->SingleNote[curs].Instr_EffType & 0x0F);
        thispattern->Noteblock[curr].EffectParam = MODPattern->SingleNote[curs].EffParam;
      }
    }
    
    // prepare for next pattern
    MODPattern = (XM7_MODPattern_Type*) &(MODPattern->SingleNote[curr+1]);
  }  // end 'pattern' for
  
  // done working with patterns
  // now loading samples
  
  u8* DataBlock = (u8*) MODPattern;
  
  for (CurrentInstrument=0;CurrentInstrument<Module->NumberofInstruments;CurrentInstrument++) {
    // check if this instrument is present
    if (Module->Instrument[CurrentInstrument]!=NULL) {
      XM7_Sample_Type* CurrentSamplePtr = Module->Instrument[CurrentInstrument]->Sample[0];
     
      // memcpy LEN bytes from MOD to SampleData memory
      memcpy (CurrentSamplePtr->SampleData, DataBlock, CurrentSamplePtr->Length);
     
      // prepare for reading next sample
      DataBlock = (u8*) &(DataBlock[CurrentSamplePtr->Length]);
    }
  }
  
  // samples read.

  // Set standard panning for MOD 
  Module->AmigaPanningEmulation = XM7_PANNING_TYPE_AMIGA;
  // Module->AmigaPanningDisplacement = 0x00;         // (AmigaPanning, complete separation)
  Module->AmigaPanningDisplacement = XM7_DEFAULT_PANNING_DISPLACEMENT;
  
  // Replay style MOD Player (w hack!)
  Module->ReplayStyle = XM7_REPLAY_STYLE_PT;
  
  // set State
  Module->State = XM7_STATE_READY;
  
  // end OK!
  return (0);
}


void XM7_UnloadXM (XM7_ModuleManager_Type* Module)
{
  s16 i,j;
  XM7_Instrument_Type* CurrentInstrumentPtr;
  XM7_Sample_Type* CurrentSamplePtr;
  
  // instruments, from last to first
  for (i=(Module->NumberofInstruments-1);i>=0;i--) {
    CurrentInstrumentPtr = Module->Instrument[i];

    if (CurrentInstrumentPtr == NULL)
      continue;
	
	// samples, from last to first
    for (j=(CurrentInstrumentPtr->NumberofSamples-1);j>=0;j--) {
	   CurrentSamplePtr = CurrentInstrumentPtr->Sample[j];
	   
	   // remove sample data
	   free (CurrentSamplePtr->SampleData);
	   
	   // remove sample info
	   free (CurrentSamplePtr);
	}
	
	// remove instrument
	free (CurrentInstrumentPtr);
  }
  
  // remove patterns
  for (i=(Module->NumberofPatterns-1);i>=0;i--)
    free (Module->Pattern[i]);
    
  // set State
  Module->State = XM7_STATE_EMPTY;
}


void XM7_UnloadMOD (XM7_ModuleManager_Type* Module) {
  XM7_UnloadXM (Module);
}

void XM7_SetReplayStyle  (XM7_ModuleManager_Type* Module, u8 style) {
  Module->ReplayStyle = style;
}

void XM7_SetPanningStyle (XM7_ModuleManager_Type* Module, u8 style, u8 displacement) {
  Module->AmigaPanningEmulation = style;
  Module->AmigaPanningDisplacement = displacement;
}
#include <nds.h>
#include <stdlib.h>
#include "libxm7.h"

// these are the variables I need to make the module play!
XM7_ModuleManager_Type* XM7_TheModule;

// calculated as 
// Period = 10*12*16*4 - Note*16*4 - FineTune/2;   (finetune = 0)
// Frequency = 8363*2^((6*12*16*4 - Period) / (12*16*4));
// taken from: http://aluigi.altervista.org/mymusic/xm.txt

#define BASEOCTAVE 6
// frequencies of the 6th octave
u16 SampleFrequency [12] = { 33452, 35441, 37549, 39781,
	                           42147, 44653, 47308, 50121,
                             53102, 56259, 59605, 63149 };


// finetunes with x.10 fixed point precision
// 2^((i*8)/(12*128)) << 10
#define FINETUNEPRECISION 10
u16 FineTunes [17] = { 1024, 1028, 1031, 1035,
                       1039, 1043, 1046, 1050,
					   1054, 1058, 1062, 1065,
					   1069, 1073, 1077, 1081, 1085 };

/*
// these are finetunes with x.14  fixed point precision
// 2^(i/(12*16)) << 14
#define FINETUNEPRECISION 14
int FineTunes [17] = { 16384, 16443, 16503, 16562,
                       16622, 16682, 16743, 16803,
					   16864, 16925, 16986, 17048,
					   17109, 17171, 17233, 17296, 17358 };
*/

// NTSC Amiga timer 7159090,5
#define AMIGAMAGICNUMBER 3579545
// MOD octave 0 difference
#define AMIGABASEOCTAVE 2
// AmigaPeriods for MOD "Octave ZERO"
u16 AmigaPeriods[12] = { 1712, 1616, 1525, 1440, 1357, 1281, 
                          1209, 1141, 1077, 1017, 961, 907 };

// sin(x) with 6.10 fixed point precision
// round ( sin(i/32*Pi) << 10 )
u16 sinus[17] = { 0, 100, 200, 297, 392, 483, 569, 650, 724,
                   792, 851, 903, 946, 980, 1004, 1019, 1024 };

u16 VeryFineTunes [129];
// will be calculated from FineTunes[], interpolating (linear)...

// u8 RealPanning [129];   // indexes [0..128], values from [0..0x80]
// will be calculated by Pan Aperture

#define YES				1
#define NO				0

#define ENVELOPE_NONE       0
#define ENVELOPE_ATTACK     1
#define ENVELOPE_SUSTAIN    2
#define ENVELOPE_RELEASE    3

void CalculateVeryFineTunes () {

  u8 i,j;
  
  for (i=0;i<=128;i++) {
	j = i >> 3;
    if ((i & 0x07)==0) {
      VeryFineTunes [i] = FineTunes[j];
	} else {
	  // interpolation (linear)
	  VeryFineTunes [i] = FineTunes [j] + (((FineTunes [j+1]-FineTunes [j]) * (i & 0x07)) >> 3);
	}
  }
}


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

/*
void CalculateRealPanningArray (u8 halfvalue) {   // halfvalue = 0..128

  u8 i,j,k;
  
  // resets values
  for (i=0;i<=128;i++)
    RealPanning [i] = 0xff;
    
  // set start values
  RealPanning [0]   = 0;
  RealPanning [128] = 128;
  RealPanning [64]  = halfvalue;
  
  for (j=5;j>0;j--) {
  
    halfvalue >>= 1;    // half of halfvalue
    k = (0x01 << j);
    
    
    for (i=k;i<128;i=i+k) {
     
      if (RealPanning[i]==0xff)
        RealPanning[i]=RealPanning[i-k] + halfvalue;
     
    }
    
  }
}
*/

void XM7_lowlevel_stopSound(u8 channel) {

  // use channels starting from last!
  channel = 15 - channel;
  SCHANNEL_CR(channel) = 0;
}

void XM7_lowlevel_pauseSound(u8 channel) {

  // use channels starting from last!
  channel = 15 - channel;
  SCHANNEL_CR(channel) &= ~SCHANNEL_ENABLE;
}

void XM7_lowlevel_resumeSound(u8 channel) {

  // use channels starting from last!
  channel = 15 - channel;
  SCHANNEL_CR(channel) |= SCHANNEL_ENABLE;
}

void XM7_lowlevel_startSound (int sampleRate, const void* data, u32 length, u8 channel, u8 vol, u8 pan, u8 format, u32 offset) {

  // use channels starting from last!
  channel = 15 - channel;

  SCHANNEL_CR(channel) = 0;
  offset = format?(offset*2):offset;
  
  // check if offset is still IN the sample (and len>0)
  if (length>offset) {
	SCHANNEL_TIMER(channel)  = SOUND_FREQ(sampleRate);
	SCHANNEL_SOURCE(channel) = ((u32)data)+offset;
  	SCHANNEL_REPEAT_POINT(channel) = 0;
	SCHANNEL_LENGTH(channel) = (length-offset) >> 2;
	SCHANNEL_CR(channel)     = SCHANNEL_ENABLE | SOUND_ONE_SHOT | SOUND_VOL(vol) | SOUND_PAN(pan) | (format==0?SOUND_FORMAT_8BIT:SOUND_FORMAT_16BIT);
  }
}

void XM7_lowlevel_startSoundwLoop (int sampleRate, const void* data, u32 looplength, u32 loopstart, u8 channel, u8 vol, u8 pan, u8 format, u32 offset) {

  // use channels starting from last!
  channel = 15 - channel;
  
  SCHANNEL_CR(channel) = 0;
  offset = format?(offset*2):offset;
  
  // check if offset is still IN the sample (and len>0)
  if ((loopstart+looplength)>offset) {
    // check if offset is still in the NON-looping part of the sample.
	// reset it to the BEGINNING of the LOOPING PART of the SAMPLE if it doesn't
	if (offset>loopstart)
	  offset = (format==0?loopstart:(loopstart>>1));
		
	SCHANNEL_TIMER(channel)  = SOUND_FREQ(sampleRate);
	SCHANNEL_SOURCE(channel) = ((u32)data)+offset;
	SCHANNEL_REPEAT_POINT(channel) = (loopstart-offset) >> 2;
  	SCHANNEL_LENGTH(channel) = looplength >> 2;
	SCHANNEL_CR(channel) = SCHANNEL_ENABLE | SOUND_REPEAT | SOUND_VOL(vol) | SOUND_PAN(pan) | (format?SOUND_FORMAT_16BIT:SOUND_FORMAT_8BIT);
  } 
}

void XM7_lowlevel_setVolumeandPanning (u8 channel, u8 vol,  u8 pan) {
  
  // use channels starting from last!
  channel = 15 - channel;
  
  SCHANNEL_VOL (channel) = (vol & 0x7f);
  SCHANNEL_PAN (channel) = (pan & 0x7f);
}

void XM7_lowlevel_pitchSound (int sampleRate, u8 channel) {

  // use channels starting from last!
  channel = 15 - channel;

  SCHANNEL_TIMER(channel) = SOUND_FREQ(sampleRate);
}

// define this to access the higherbyte of SCHANNEL_CR
#define SCHANNEL_CR_HIGHERBYTE(n)	(*(vu8*)(0x04000403 + ((n)<<4)))
#define SCHANNEL_CR_SHIFTBITS       24

void XM7_lowlevel_changeSample (const void* data, u32 looplength, u32 loopstart, u8 channel, u8 format) {

  // use channels starting from last!
  channel = 15 - channel;
  
  SCHANNEL_SOURCE (channel) = (u32)data;
  SCHANNEL_REPEAT_POINT (channel) = loopstart >> 2;
  SCHANNEL_LENGTH (channel) = looplength >> 2;
  // BETA: sets sound_repeat even if it's not
  // BETA2: doesn't change that because MOD support only 8 bits samples
  // SCHANNEL_CR_HIGHERBYTE (channel) = (SCHANNEL_ENABLE | SOUND_REPEAT | (format?SOUND_FORMAT_16BIT:SOUND_FORMAT_8BIT)) >> SCHANNEL_CR_SHIFTBITS;
}

void SetTimerSpeedBPM (u8 BPM) {

  // calculate the main timer freq
  // - the freq is 33.513.982 Hz, when prescaler is F/1024 then it's 32.728,5 Hz (circa)
  // = 1.963.710 clicks/minute , then divided by 6 because there are 6 ticks/line
  //   then divided by 4 because BPM is on 4/4th,

  // set the timer
  u16 timer = 1963710 / (BPM * 24);
  TIMER1_DATA = - timer;
  
  // start/restart it!
  TIMER1_CR &= ~TIMER_ENABLE;
  TIMER1_CR |= TIMER_ENABLE;
}

u8 CalculateFinalVolume (u8 samplevol, u8 envelopevol, u16 fadeoutvol) {
  // gives back [0x00-0x7f]
  // FinalVol=(FadeOutVol/32768)*(EnvelopeVol/64)*(GlobalVol/64)*(Vol/64)*Scale;
  // scale is 0x80
  u8 tmpvol = ((fadeoutvol>>3)*envelopevol*XM7_TheModule->CurrentGlobalVolume*samplevol) >> 23;
  
  // clip volume value
  if (tmpvol>0x7f) {
    tmpvol = 0x7f;
  }
  
  return (tmpvol);
}

u8 CalculateFinalPanning (u8 chn, u8 samplepan, u8 envelopepan) {
  // gives back [0x00-0x7f]
  // FinalPan=Pan+(EnvelopePan-32)*(128-Abs(Pan-128))/32;
  // return is UNSIGNED 0..127
  u8 res=0;
  
  if (XM7_TheModule->AmigaPanningEmulation) {
    switch (chn & 0x03) {
      case 0:
	  case 3:res = 0x00 + XM7_TheModule->AmigaPanningDisplacement;
	         break;
			 
	  case 1:
      case 2:res = 0x7f - XM7_TheModule->AmigaPanningDisplacement;
	         break;
    }
  } else {
    // when there was no panning envelope, it was simply:
    // res = (samplepan >> 1);
    res = (samplepan + ((envelopepan-32) * (128-abs(samplepan-128))) / 32 ) >> 1;
    
    // calculate real pan with new method  (deactivated, actually)    
    /*
    if (samplepan>=0x80)
      res = 63 + (RealPanning[samplepan-0x80] >> 1);  // gives 63..127
    else
      res = 64 - (RealPanning[0x80-samplepan] >> 1);  // gives 0..64
    */
  }
  
  return (res);
}

void SlideSampleVolume (u8 chn, s8 change) {
  // change is -0x40 ... 0x40
  if (change>0) {
    XM7_TheModule->CurrentSampleVolume[chn] += change;
	if ( XM7_TheModule->CurrentSampleVolume[chn] > 0x40 )
	  XM7_TheModule->CurrentSampleVolume[chn] = 0x40;
  } else {
    if ( XM7_TheModule->CurrentSampleVolume[chn] > -change )
	  XM7_TheModule->CurrentSampleVolume[chn] += change;
	else
	  XM7_TheModule->CurrentSampleVolume[chn] = 0;
  }
}

void SlideSamplePan (u8 chn, s8 change) {
  // change is -0x0f ... 0x0f
  if (change>0) {
    // pan RIGHT
    if ((255 - XM7_TheModule->CurrentSamplePanning[chn])>change)
	  XM7_TheModule->CurrentSamplePanning[chn] += change;
	else
	  XM7_TheModule->CurrentSamplePanning[chn] = 0xFF;   // complete RIGHT
  } else {
    // pan LEFT
    if ( XM7_TheModule->CurrentSamplePanning[chn] > -change )
	  XM7_TheModule->CurrentSamplePanning[chn] += change;
	else
	  XM7_TheModule->CurrentSamplePanning[chn] = 0x00;    // complete LEFT
  }
}

void ChangeVolumeonRetrigTable (u8 chn, u8 param) {
  switch (param) {
    case 0:
	case 8: break;
	
	case 1 ... 5:SlideSampleVolume (chn, -(1 << (param-1)));   // -1, -2, -4, -8, -16
	              break;
	
	case 9 ... 0x0d:SlideSampleVolume (chn, ( 1 << (param-9)));  // +1, +2, +4, +8, +16
	                 break;
		 
    case 6:XM7_TheModule->CurrentSampleVolume[chn] = (XM7_TheModule->CurrentSampleVolume[chn]*2/3);  //  * 2/3
	       break;
		   
	case 7:XM7_TheModule->CurrentSampleVolume[chn] /= 2;  //  * 1/2
	       break;
		   
    case 0x0e:XM7_TheModule->CurrentSampleVolume[chn] = (XM7_TheModule->CurrentSampleVolume[chn]*3/2);  // * 3/2
	          if ( XM7_TheModule->CurrentSampleVolume[chn] > 0x40 )
	            XM7_TheModule->CurrentSampleVolume[chn] = 0x40;
			  break;
			  
	case 0x0f:XM7_TheModule->CurrentSampleVolume[chn] *= 2;  // * 2
	          if ( XM7_TheModule->CurrentSampleVolume[chn] > 0x40 )
	            XM7_TheModule->CurrentSampleVolume[chn] = 0x40;
			  break;
  }
}

void ApplyVolumeandPanning (u8 chn) {

  u8 volume = XM7_TheModule->CurrentSampleVolume [chn];
  s8 tremolo = XM7_TheModule->CurrentTremoloVolume [chn];
  u8 tremormute = XM7_TheModule->CurrentTremorMuting [chn];

  if (tremormute) {
    volume = 0;
  } else {
    if (tremolo>0) {
      if ((volume + tremolo)>0x40)
  	    volume=0x40;
	  else
	    volume+=tremolo;
    } else if (tremolo<0) {
      if (volume < -tremolo)
	    volume=0;
	  else
	    volume+=tremolo;
    }
    
    
    // final calculation of volume & panning
    volume  = CalculateFinalVolume  (volume ,XM7_TheModule->CurrentSampleVolumeEnvelope[chn],
                                             XM7_TheModule->CurrentSampleVolumeFadeOut[chn]);
  }

										   
  u8 panning = CalculateFinalPanning (chn, XM7_TheModule->CurrentSamplePanning[chn],
                                          XM7_TheModule->CurrentSamplePanningEnvelope[chn]);
  
  // CHANGE VOLUME & PAN !
  XM7_lowlevel_setVolumeandPanning (chn,volume,panning);
}

void ApplyNewGlobalVolume (void) {
  u8 chn;
  // for every channel
  for (chn=0;chn<(XM7_TheModule->NumberofChannels);chn++)
    ApplyVolumeandPanning (chn);
}

void CalculateEnvelopeVolume (u8 chn, u8 instrument) {

  u16 i,j, x1=0,x2=0, y1=0,y2=0;
  XM7_Instrument_Type* CurrInstr = XM7_TheModule->Instrument[instrument-1];
  
  // calculate volume for point X
  j=(CurrInstr->NumberofVolumeEnvelopePoints-1);
  for (i=0;i<CurrInstr->NumberofVolumeEnvelopePoints;) {
	// find the closest (left) X point
	if (CurrInstr->VolumeEnvelopePoint[i].x<=XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn]) {
  	  x1=CurrInstr->VolumeEnvelopePoint[i].x;
	  y1=CurrInstr->VolumeEnvelopePoint[i].y;
    }
	 
	// find the closest (right) X point
	if (CurrInstr->VolumeEnvelopePoint[j].x>=XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn]) {
	  x2=CurrInstr->VolumeEnvelopePoint[j].x;
	  y2=CurrInstr->VolumeEnvelopePoint[j].y;
    }
	  
	// move the indexes
	i++;
	j--;
  }
     
  // calculate the final value between x1 and x2
  if (x1==x2) {
	// the points are the same
	XM7_TheModule->CurrentSampleVolumeEnvelope[chn]=y1;
  } else {
	// the points are different, interpolation needed!
	XM7_TheModule->CurrentSampleVolumeEnvelope[chn] = y1 + (y2-y1) * (XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn]-x1) / (x2-x1);
  }
}

void CalculateEnvelopePanning (u8 chn, u8 instrument) {

  u16 i,j, x1=0,x2=0, y1=0,y2=0;
  XM7_Instrument_Type* CurrInstr = XM7_TheModule->Instrument[instrument-1];
  
  // calculate Panning for point X
  j=(CurrInstr->NumberofPanningEnvelopePoints-1);
  for (i=0;i<CurrInstr->NumberofPanningEnvelopePoints;) {
	// find the closest (left) X point
	if (CurrInstr->PanningEnvelopePoint[i].x<=XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn]) {
  	  x1=CurrInstr->PanningEnvelopePoint[i].x;
	  y1=CurrInstr->PanningEnvelopePoint[i].y;
    }
	 
	// find the closest (right) X point
	if (CurrInstr->PanningEnvelopePoint[j].x>=XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn]) {
	  x2=CurrInstr->PanningEnvelopePoint[j].x;
	  y2=CurrInstr->PanningEnvelopePoint[j].y;
    }
	  
	// move the indexes
	i++;
	j--;
  }
     
  // calculate the final value between x1 and x2
  if (x1==x2) {
	// the points are the same
	XM7_TheModule->CurrentSamplePanningEnvelope[chn]=y1;
  } else {
	// the points are different, interpolation needed!
	XM7_TheModule->CurrentSamplePanningEnvelope[chn] = y1 + (y2-y1) * (XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn]-x1) / (x2-x1);
  }
}

void StartEnvelope (u8 chn, u8 startpoint) {

  // we need to start an envelope, if needed
  // check if envelope is ACTIVE for this instrument
  if (XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VolumeType & 0x01) {
  
    u16 tmp;
	// volume envelope is ACTIVE: set the variables
	tmp = XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VolumeEnvelopePoint[(XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->NumberofVolumeEnvelopePoints)-1].x;
	if (startpoint>tmp)
	  startpoint=tmp;
	XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn]=startpoint;
	XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]=ENVELOPE_ATTACK;
    CalculateEnvelopeVolume (chn, XM7_TheModule->CurrentChannelLastInstrument[chn]);
  } else {
	// volume envelope is DISABLED: set the variables to default values
	XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]=ENVELOPE_NONE;
	XM7_TheModule->CurrentSampleVolumeEnvelope[chn]=0x40;        // because no envelope!
  }
  
  // fade:
  XM7_TheModule->CurrentSampleVolumeFadeOut[chn] = 0x8000;       // reset to maximum (32768)
  
  //
  //
  // PANNING envelope!
  //
  //
  
  if (XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->PanningType & 0x01) {
	// panning envelope is ACTIVE: set the variables
    XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]=ENVELOPE_ATTACK;
	XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn]=0;
    CalculateEnvelopePanning (chn,XM7_TheModule->CurrentChannelLastInstrument[chn]);
  } else {
	// PANNING envelope is DISABLED: set the variables to default values
	XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]=ENVELOPE_NONE;
	XM7_TheModule->CurrentSamplePanningEnvelope[chn]=0x20;       // because no envelope!
  }
}

void ElaborateEnvelope (u8 chn, u8 instrument) {
  // This function should elaborate the volume envelope of the instrument playing on that channel
  // Now also the panning envelope!
 
  // make it easyer!
  XM7_Instrument_Type* CurrInstr = XM7_TheModule->Instrument[instrument-1];
  
  u8 VSP = CurrInstr->VolumeSustainPoint;
  u8 NVEP = (CurrInstr->NumberofVolumeEnvelopePoints)-1;  // Volume envelope last point
  u8 VLSP = CurrInstr->VolumeLoopStartPoint;
  u8 VLEP = CurrInstr->VolumeLoopEndPoint;

  // Volume: are we in ATTACK?
  if (XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]==ENVELOPE_ATTACK) {
    // check if we should pass in SUSTAIN...
    if (CurrInstr->VolumeType & 0x02) {
     // VOLUME SUSTAIN POINT is active, check if we reached that!
	 if (XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn]==CurrInstr->VolumeEnvelopePoint[VSP].x) {
	   XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]=ENVELOPE_SUSTAIN;
	  }
	}
  } // end "if ATTACK"
  
  // Volume: are we in SUSTAIN?
  if (XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]==ENVELOPE_SUSTAIN) {
    // we're in sustain: we won't move our X point, we won't change VOLUME
	XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn]=CurrInstr->VolumeEnvelopePoint[VSP].x;
	XM7_TheModule->CurrentSampleVolumeEnvelope[chn]=CurrInstr->VolumeEnvelopePoint[VSP].y;
  } else {
    // we're not in sustain, we will move our X point and calculate the new volume
    if (XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]!=ENVELOPE_NONE) {
	  XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn]++;
	 
      // check loop
	  if (CurrInstr->VolumeType & 0x04) {
	    // loop active! Check if we are on the loop end and restart, if needed.
	    if (XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn] == CurrInstr->VolumeEnvelopePoint[VLEP].x) {
	      // we reached the end of the loop, reset it!
		  XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn] = CurrInstr->VolumeEnvelopePoint[VLSP].x;
		  // XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn] = (CurrInstr->VolumeEnvelopePoint[VLSP].x % 256);
        }
	  }
	  
	  // now let's check if we ran over the points...
	  // (that could happen even when there's a loop, thanks to Lxx effect...)
	  if (XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn] > CurrInstr->VolumeEnvelopePoint[NVEP].x) {
	    // envelope is finished: re-use last envelope point
	    XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn] = CurrInstr->VolumeEnvelopePoint[NVEP].x;
	  }
		
	  // we've still got to calculate volume
	  CalculateEnvelopeVolume(chn, instrument);
    }
  }   // end "we aren't in SUSTAIN"

  // fade out vol (if we're in release)
  if (XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]==ENVELOPE_RELEASE) {
    // decrease volume, if possible
    if (XM7_TheModule->CurrentSampleVolumeFadeOut[chn] > CurrInstr->VolumeFadeout) {
	   XM7_TheModule->CurrentSampleVolumeFadeOut[chn] -= CurrInstr->VolumeFadeout;
    } else {
	  XM7_TheModule->CurrentSampleVolumeFadeOut[chn] = 0;
    }
  }
  
  //
  //
  //
  // PANNING envelope!
  //
  //
  //
  
  u8 PSP = CurrInstr->PanningSustainPoint;
  u8 NPEP = (CurrInstr->NumberofPanningEnvelopePoints)-1;  // Panning envelope last point
  u8 PLSP = CurrInstr->PanningLoopStartPoint;
  u8 PLEP = CurrInstr->PanningLoopEndPoint;

  // Panning: are we in ATTACK?
  if (XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]==ENVELOPE_ATTACK) {
    // check if we should pass in SUSTAIN...
    if (CurrInstr->PanningType & 0x02) {
     // PANNING SUSTAIN POINT is active, check if we reached that!
	 if (XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn]==CurrInstr->PanningEnvelopePoint[PSP].x) {
	   XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]=ENVELOPE_SUSTAIN;
	  }
	}
  } // end "if ATTACK"
  
  // Panning: are we in SUSTAIN?
  if (XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]==ENVELOPE_SUSTAIN) {
    // we're in sustain: we won't move our X point, we won't change PANNING
	XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn]=CurrInstr->PanningEnvelopePoint[PSP].x;
	XM7_TheModule->CurrentSamplePanningEnvelope[chn]=CurrInstr->PanningEnvelopePoint[PSP].y;
  } else {
    // we're not in sustain, we will move our X point and calculate the new panning
    if (XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]!=ENVELOPE_NONE) {
	  XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn]++;
	  
      // check loop
	  if (CurrInstr->PanningType & 0x04) {
	    // loop active! Check if we are on the loop end and restart, if needed.
	    if (XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn] == CurrInstr->PanningEnvelopePoint[PLEP].x) {
	      // we reached the end of the loop, reset it!
		  XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn] = CurrInstr->PanningEnvelopePoint[PLSP].x;
        }
	  }
	  
	  // now let's check if we ran over the points...
	  // (that could happen even when there's a loop)
	  if (XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn] > CurrInstr->PanningEnvelopePoint[NPEP].x) {
	    // envelope is finished: re-use last envelope point
	    XM7_TheModule->CurrentSamplePanningEnvelopePoint[chn] = CurrInstr->PanningEnvelopePoint[NPEP].x;
	  }
		
	  // we've still got to calculate Panning
      CalculateEnvelopePanning(chn, instrument);
    }
  }   // end "we aren't in SUSTAIN"
}

// function used by Vibrato & Tremolo  (6.10 fixed point)
s16 CalculateModulatorValue (u8 type, u8 pos) {
  s16 fixed = 0;
  switch (type) {
  
    case 0:if (pos<=16)                                 // sinusoidal
	         fixed = sinus [pos];         // 0..16
		   else if (pos<=32)
		     fixed = sinus [32-pos];     // 17..32
		   else if (pos<=48)
		     fixed = - sinus [pos-32];   // 33..48
		   else
		     fixed = - sinus [64-pos];   // 49..63
		   break;
		   
    case 1:fixed = 1024 - (2048*pos/63);               // ramp down
	       break;
		   
    case 2:fixed = (pos<32)?1024:-1024;                // square
	       break;
  }
  return (fixed);
}

s8 CalculateVibratoValue (u8 type, u8 pos, u8 dep) {
  // this is for calculating bending in x/128 of a semitone (depth was=[0..15])
  // ret is: [-120..+120] and this is in x/128th of a semitone
  return ( CalculateModulatorValue(type, pos) * dep >> 7);
}

s8 CalculateTremoloValue (u8 type, u8 pos, u8 dep) {
  // ret is: [-60..+60] (0x0f * 4)
  return ( CalculateModulatorValue (type, pos) * dep >> 8);
}

u16 DecodeVolumeColumn (u8 chn, u8 volcmd, u8 curtick, u8 EDxInAction) {
  
  s32 diff;
  u16 resvalue = 0x0001;
  // 0x0001 = change volume and/or panning
  // 0x0002 = change pitch
  
  u8 tmpvalue = (volcmd & 0x0f);                                            // 1..0x0f
  
  switch (volcmd) {
  
	case 0x10 ... 0x50:
	  // set volume to specified value
	  // (when the tick is the one specified in EDx or 0 when there's no EDx)
	  if (curtick==EDxInAction)
	    XM7_TheModule->CurrentSampleVolume[chn] = (volcmd - 0x10);         // 0..0x40
	  break;
     
	case 0x61 ... 0x6f:
	  // VOLUME slide down (decrease volume by value)
	  // NOTE: the effect starts from tick=1, not on tick=0
	  if (curtick!=0)
	    SlideSampleVolume(chn,-tmpvalue);
	  break;
	  
    case 0x71 ... 0x7f:
	  // VOLUME slide up (increase volume by value)
	  // NOTE: the effect starts from tick=1, not on tick=0
	  if (curtick!=0)
	    SlideSampleVolume(chn,tmpvalue);
      break;
	  
	case 0x81 ... 0x8f:
	  // FINE VOLUME slide down (decrease volume by fine value)
	  if (curtick==0)
	    SlideSampleVolume (chn, -tmpvalue);
      break;
	  
    case 0x91 ... 0x9f:
	  // FINE VOLUME slide up (increase volume by fine value)
	  if (curtick==0)
	    SlideSampleVolume (chn, tmpvalue);
      break;
	  
	case 0xa0 ... 0xaf:
	  // Set vibrato speed
	  if (curtick==0) {
	    // memory effect
		if (tmpvalue!=0)
		  XM7_TheModule->Effect4xxMemory[chn] = (XM7_TheModule->Effect4xxMemory[chn] & 0x0f) | (tmpvalue << 4);
        resvalue = 0x0000;
	  } else {
		XM7_TheModule->CurrentVibratoValue[chn] = CalculateVibratoValue (XM7_TheModule->CurrentVibratoType[chn] & 0x03, XM7_TheModule->CurrentVibratoPoint[chn], XM7_TheModule->Effect4xxMemory[chn] & 0x0F);
	    XM7_TheModule->CurrentVibratoPoint[chn] = (XM7_TheModule->CurrentVibratoPoint[chn] + (XM7_TheModule->Effect4xxMemory[chn] >> 4)) & 0x03f; // mod 64
        resvalue = 0x0002;
	  }
	  break;
	  
	case 0xb0 ... 0xbf:
	  // Vibrato
	  // Performs vibrato with depth x
	  // (but requires the speed component to be initialized with 4x0 or Sx.)
	  if (curtick==0) {
		// memory effect
		if (tmpvalue!=0)
		  XM7_TheModule->Effect4xxMemory[chn] = (XM7_TheModule->Effect4xxMemory[chn] & 0xf0) | tmpvalue;
        resvalue = 0x0000;
	  } else {
	    XM7_TheModule->CurrentVibratoValue[chn] = CalculateVibratoValue (XM7_TheModule->CurrentVibratoType[chn] & 0x03, XM7_TheModule->CurrentVibratoPoint[chn], XM7_TheModule->Effect4xxMemory[chn] & 0x0F);
	    XM7_TheModule->CurrentVibratoPoint[chn] = (XM7_TheModule->CurrentVibratoPoint[chn] + (XM7_TheModule->Effect4xxMemory[chn] >> 4)) & 0x03f; // mod 64
	    resvalue = 0x0002;
      }
	  
	  break;
	  
	case 0xc0 ... 0xcf:
	  // change panning
	  if (curtick==0)
	    XM7_TheModule->CurrentSamplePanning[chn] = (tmpvalue << 4) | tmpvalue;   // 0..0xff
	  break;
	  
	case 0xd1 ... 0xdf:
	  // PANNING slide LEFT
	  // NOTE: the effect starts from tick=1, not on tick=0
	  if (curtick>0)
	    SlideSamplePan (chn, -tmpvalue);
	  break;
	  
 	case 0xe1 ... 0xef:
	  // panning slide RIGHT
	  // NOTE: the effect starts from tick=1, not on tick=0
	  if (curtick>0)
	    SlideSamplePan (chn, tmpvalue);
	  break;
	  
	case 0xf0 ... 0xff:
	  // Portamento to note (aka tone porta)
	  resvalue = 0x0000;
	  
	  tmpvalue = (tmpvalue << 4) | tmpvalue;
	  
	  // memory effect
	  if (tmpvalue==0x00)
	    tmpvalue=XM7_TheModule->Effect3xxMemory[chn];
	  else
	    if (curtick==0)
  	      XM7_TheModule->Effect3xxMemory[chn]=tmpvalue;
	  
	  if (curtick>0) {
	    
        diff = XM7_TheModule->CurrentSamplePortaDest[chn] - XM7_TheModule->CurrentSamplePortamento[chn];
	  
	    if (diff>0) {
		  // increase period (avoiding overflow)
		  if ( (tmpvalue << 2) < diff )
	        XM7_TheModule->CurrentSamplePortamento[chn] += (tmpvalue << 2);  // + (val*4)
		  else
		    XM7_TheModule->CurrentSamplePortamento[chn] = XM7_TheModule->CurrentSamplePortaDest[chn];
		}
		
		if (diff<0) {
		  // decrease period (avoiding underflow)
		  if ( (tmpvalue << 2) < -diff )
		    XM7_TheModule->CurrentSamplePortamento[chn] -= (tmpvalue << 2);  //  - (val*4)
		  else
		    XM7_TheModule->CurrentSamplePortamento[chn] = XM7_TheModule->CurrentSamplePortaDest[chn];
		}
		
		resvalue = 0x0002;
	  }
	  break;

  }
  
  return(resvalue);
}


u16 DecodeBeforeEffectsColumn (u8 chn, u8 effcmd, u8 effpar, u8 curtick) {

  u16 resvalue=0;
  u8 tmpvalue;
  
  switch (effcmd) {
  
    case 0x3:                      // portamento to note (even when with volume...)
	case 0x5:resvalue = 0x0300;
	         break;

  	case 0xe:                                    
	  // sub effects!
	  tmpvalue = effpar & 0x0F;
	  
	  switch (effpar >> 4) {
	  
	  	case 0xd:                             // EDx
		  resvalue = 0x0ED0 | tmpvalue;
		  break;
		  
		  
		case 0x5:                             // E5x
		  resvalue = 0x0E50;
		  break;
	  }
	  break;
	
	case 0x14:                                // Kxx
	  if (effpar==curtick)
	    resvalue = 0x1400;
	  break;
	
	case 0x15:                                // Lxx
	  resvalue = 0x1500 | effpar;
	  break;
	  
	case 0x0b:                                // Bxx
	  resvalue= 0x0B00 | effpar;
	  break;

	case 0x0d:                                // Dxx
     	
	  // NOTE: the first digit has to be multiplied by 10 even if it's hex! (v. 1.06)
	  /* if (((effpar & 0x0f) < 0x0a) && ((effpar >> 4) < 0x0a)) */
	  effpar = (effpar >> 4) * 10 + (effpar & 0x0f);
	  
	  // give back value, binary!
	  resvalue = 0x0d00 | effpar;
	  break;

  }
  
  return (resvalue);
}

u8 MemoryEffectxxTogether (u8 value, u8 *store) {
  // checks the value and gets from *store if it's ZERO
  // writes into store the new value
  // the value is manipulated as a whole

  // take it from store if needed (or save it there!)
  if (value==0)
	value = *store;
  else
    *store = value;

  return (value);
}


u8 MemoryEffectxySeparated (u8 value, u8 *store) {
  // checks the value and gets from *store what is ZERO
  // writes into store the new value
  // the value is manipulated as a two separate values

  u8 tmpvalue;

  // take 1st part (x)
  tmpvalue = (value >> 4);

  // take it from store if needed (or save it there!)
  if (tmpvalue==0)
	value = (value & 0x0F) | (*store & 0xF0);
  else
    *store = (*store & 0x0F) | (tmpvalue << 4);

  // take 2nd part (y)
  tmpvalue = (value & 0x0F);

  // take it from memory if needed (or save it there!)
  if (tmpvalue==0)
    value = (value & 0xF0) | (*store & 0x0F);
  else
    *store = (*store & 0xF0) | tmpvalue;

  return (value);
}

u16 DecodeEffectsColumn (u8 chn, u8 effcmd, u8 effpar, u8 curtick, u16 addtick) {

  s32 diff;
  u16 resvalue=0xffff;
  u8 tmpvalue;
  
  switch (effcmd) {
  
    case 0x0:
	  // "Arpeggio: 0xy"
	  // " - quickly alters the note pitch between the base note and the semitone offsets x and y.
	  // "Each pitch is played for the duration of 1 tick. If speed is higher than 3
      //  (meaning there are more than 3 ticks per row), the sequence is looped."
	  // "order= 0,y,x , a la FastTracker II"
      
      tmpvalue = (curtick+addtick) % 3;
      
      // swap Arpeggio order (0,x,y) if style is not FT2
      if ((XM7_TheModule->ReplayStyle & XM7_REPLAY_STYLE_MOD_PLAYER) && (tmpvalue!=0)) {
        if (tmpvalue==1)
          tmpvalue=2;
        else
          tmpvalue=1;
      }
      
	  switch (tmpvalue) {
	    case 0:resvalue=0;
		       break;
	    case 1:resvalue=effpar & 0x0f;
		       break;
		case 2:resvalue=effpar >> 4;
		       break;
	  }
	  break;
	  
	case 0x1:
	  // "Portamento up"
	  // "Portamento is used to slide the note pitch up or down. The higher the xx, the faster"
      // " it goes. Effect is applied on every tick."
	  // To do "porta UP" you should DECREASE the Period
	  
      if (curtick==0)
	    MemoryEffectxxTogether (effpar, &XM7_TheModule->Effect1xxMemory[chn]);
	  else 
	    XM7_TheModule->CurrentSamplePortamento[chn] -= (XM7_TheModule->Effect1xxMemory[chn] << 2);
		
	  // needs this to trigger pitching
	  resvalue=0x0100;
	  break;
	  
	case 0x2:
	  // "Portamento down"
	  // "Works similarly to 1xx portamento up, only bending note pitch down instead of up"
	  // To do "porta DOWN" you should INCREASE the Period
	  
      if (curtick==0)
	    MemoryEffectxxTogether (effpar, &XM7_TheModule->Effect2xxMemory[chn]);
      else
	    XM7_TheModule->CurrentSamplePortamento[chn] += (XM7_TheModule->Effect2xxMemory[chn] << 2);
		
	  // needs this to trigger pitching
	  resvalue=0x0100;
	  break;
	  
	case 0x3:
	  // "Portamento to note"
	  // "This portamento command bends the already playing note pitch towards another one,
      //  entered with the 3xx command."
	  
      if (curtick==0) {
	    MemoryEffectxxTogether (effpar, &XM7_TheModule->Effect3xxMemory[chn]);
     
	  } else {
	    
        u16 value16 = XM7_TheModule->Effect3xxMemory[chn] << 2;  // (val*4)
        diff = XM7_TheModule->CurrentSamplePortaDest[chn] - XM7_TheModule->CurrentSamplePortamento[chn];
	  
	    if (diff>0) {
		  // increase period (avoiding overflow)
		  if ( value16 < diff )
	        XM7_TheModule->CurrentSamplePortamento[chn] += value16;
		  else
		    XM7_TheModule->CurrentSamplePortamento[chn] = XM7_TheModule->CurrentSamplePortaDest[chn];
		}
		else if (diff<0) {
		  // decrease period (avoiding underflow)
		  if ( value16 < -diff )
		    XM7_TheModule->CurrentSamplePortamento[chn] -= value16;
		  else
		    XM7_TheModule->CurrentSamplePortamento[chn] = XM7_TheModule->CurrentSamplePortaDest[chn];
		}
	  }
	 
	  // needs this to trigger pitching
	  resvalue=0x0100;
	  break;
	  
	case 0x4:
	  // Vibrato 4xy (x = speed, y = depth)
	  // Vibrato alters note pitch up and down in the maximum range of a full tone.
	  // After the initial xy pair, parameters can be set individually.
	  // The pitch is reset when the command is discontinued.
	  if (curtick==0) {
	    // effect memory
	    MemoryEffectxySeparated (effpar, &XM7_TheModule->Effect4xxMemory[chn]);
	  } else {
  	    XM7_TheModule->CurrentVibratoValue[chn] = CalculateVibratoValue (XM7_TheModule->CurrentVibratoType[chn] & 0x03, XM7_TheModule->CurrentVibratoPoint[chn], XM7_TheModule->Effect4xxMemory[chn] & 0x0F);
	    XM7_TheModule->CurrentVibratoPoint[chn] = (XM7_TheModule->CurrentVibratoPoint[chn] + (XM7_TheModule->Effect4xxMemory[chn] >> 4)) & 0x03f; // mod 64
     	// needs this to trigger pitching
	    resvalue=0x0100;
      }
	  break;
	  
	case 0x7:
	  // Tremolo 7xy (x = speed, y = depth)
	  // Tremolo alters note volume up and down. After the initial xy pair, parameters can be
	  // set individually. The volume is not reset when the command is discontinued.
	  if (curtick==0) {
		// effect memory
	    MemoryEffectxySeparated (effpar, &XM7_TheModule->Effect7xxMemory[chn]);
	  } else {
	    effpar = XM7_TheModule->Effect7xxMemory[chn];
	    XM7_TheModule->CurrentTremoloVolume [chn] = CalculateTremoloValue (XM7_TheModule->CurrentTremoloType[chn] & 0x03, XM7_TheModule->CurrentTremoloPoint[chn], (effpar & 0x0f));
		XM7_TheModule->CurrentTremoloPoint  [chn] = (XM7_TheModule->CurrentTremoloPoint[chn] + (effpar >> 4)) & 0x03f; // mod 64
	  }
	  break;
	  
    case 0x8:
      // "Sets the note stereo panning from far left 00 to far right FF overriding sample panning setting."
	  if (curtick==0)
	    XM7_TheModule->CurrentSamplePanning[chn] = effpar;
	  break;
	  
	case 0x9:
	  // "The sample that the note triggers is played from offset xx.
	  // The offsets are spread 256 samples apart so 908 skips the first (0x8*256=) 2048 bytes
	  // of the sample and plays it on from there."
	  if (curtick==0)
        effpar = MemoryEffectxxTogether (effpar, &XM7_TheModule->Effect9xxMemory[chn]);
	    resvalue= 0x0900 | effpar;
	  break;
	  
	case 0x5:
	  // Performs portamento to note with parameters initialized with 3xx or Mx
	  // while sliding volume similarly to Axy volume slide.
	  
	  // portamento part begins here (volume part will be done at "case 0xa:")
	  if (curtick>0) {
	    
		diff = XM7_TheModule->CurrentSamplePortaDest[chn] - XM7_TheModule->CurrentSamplePortamento[chn];
        u16 value16 = XM7_TheModule->Effect3xxMemory[chn] << 2;
	  
	    if (diff>0) {
		  // increase period (avoiding overflow)
		  if ( value16 < diff )
	        XM7_TheModule->CurrentSamplePortamento[chn] += value16;  // +(val*4)
		  else
		    XM7_TheModule->CurrentSamplePortamento[chn] = XM7_TheModule->CurrentSamplePortaDest[chn];
		}
		
		if (diff<0) {
		  // decrease period (avoiding underflow)
		  if ( value16 < -diff )
		    XM7_TheModule->CurrentSamplePortamento[chn] -= value16;  // -(val*4)
		  else
		    XM7_TheModule->CurrentSamplePortamento[chn] = XM7_TheModule->CurrentSamplePortaDest[chn];
		}
		
		resvalue=0x0500;
	  }
	  
	  // volume slide performed by the following Axy command
	  // NO BREAK;
	  
	case 0x6:
	  // Performs vibrato with parameters initialized with 4xy or Sx+Vx
	  // while sliding volume similarly to Axy volume slide.
      if (effcmd == 0x6) {
	    XM7_TheModule->CurrentVibratoValue[chn] = CalculateVibratoValue (XM7_TheModule->CurrentVibratoType[chn] & 0x03, XM7_TheModule->CurrentVibratoPoint[chn], XM7_TheModule->Effect4xxMemory[chn] & 0x0F);
	    XM7_TheModule->CurrentVibratoPoint[chn] = (XM7_TheModule->CurrentVibratoPoint[chn] + (XM7_TheModule->Effect4xxMemory[chn] >> 4)) & 0x03f; // mod 64
        // needs this to trigger pitching & volume change
	    resvalue=0x0500;
      }
      
	  // volume slide performed by the following Axy command
	  // NO BREAK;
	  
	case 0xa:
	  // "Slides note volume up/down at speed x/y depending on which parameter is specified."
	  // if UP volume is != 0 then volume DOWN will be ignored.
	  if (curtick==0) {
        MemoryEffectxxTogether (effpar, &XM7_TheModule->EffectAxyMemory[chn]);
	  } else {
		effpar = XM7_TheModule->EffectAxyMemory[chn];
	    tmpvalue = (effpar >> 4);               // volume UP
	    if (tmpvalue!=0) {
	      SlideSampleVolume (chn, tmpvalue);
	    } else {
 	      tmpvalue = (effpar & 0x0F);             // volume DOWN
	      SlideSampleVolume (chn, -tmpvalue);
	    }
	  }
      break;
	  
    case 0xc:
	  // "Sets the note volume 0 – 0x40 overriding sample volume setting"
	  if (curtick==0) {
	    if (effpar<=0x40)
  	      XM7_TheModule->CurrentSampleVolume[chn] = effpar;
	  }
	  break;
	  
	case 0xe:                                    
	  // sub effects!
	  tmpvalue = effpar & 0x0F;
	 
	  switch (effpar >> 4) {
	  
	    case 0x1:
		  // fine portamento UP
	      if (curtick==0) {
		    // memory effect
			tmpvalue = MemoryEffectxxTogether (tmpvalue, &XM7_TheModule->EffectE1xMemory[chn]);
			XM7_TheModule->CurrentSamplePortamento[chn] -= (tmpvalue << 2);
	      }
	      // needs this to keep pitching ON	
	      resvalue=0x0100;
		  break;
		  
		case 0x2:
		  // fine portamento DOWN
	      if (curtick==0) {
			// memory effect
			tmpvalue = MemoryEffectxxTogether (tmpvalue, &XM7_TheModule->EffectE2xMemory[chn]);
			XM7_TheModule->CurrentSamplePortamento[chn] += (tmpvalue << 2);
	      }
	      // needs this to keep pitching ON	
	      resvalue=0x0100;
		  break;
		  
		case 0x3:
		  // Glissando control
		  if (curtick==0) {
		    if (tmpvalue<2)
		      XM7_TheModule->CurrentGlissandoType[chn] = tmpvalue;
		  }
		  break;
		  
		case 0x4:
		  // Vibrato control
		  if (curtick==0) {
		    if ((tmpvalue!=3) && (tmpvalue<7))                    // 0,1,2,x,4,5,6,x
              XM7_TheModule->CurrentVibratoType[chn]=tmpvalue;
		    else if (tmpvalue==3)
		      XM7_TheModule->CurrentVibratoType[chn]=rand() % 3;      // set to 0,1,2
		    else if (tmpvalue==7)
		      XM7_TheModule->CurrentVibratoType[chn]=rand() % 3 + 4;  // set to 4,5,6
		  }
		  break;
		  
		case 0x5:
		  // Set note fine-tune
          // This command is different for XM and MOD replay! (v1.06)
          // http://www.milkytracker.net/docs/MilkyTracker.html#fxE5x
          
          if (XM7_TheModule->ReplayStyle & XM7_REPLAY_STYLE_MOD_PLAYER) {
            // for MOD replay
            if (tmpvalue<8)
	  	      XM7_TheModule->CurrentFinetuneOverride[chn] = +16 * tmpvalue;
            else
              XM7_TheModule->CurrentFinetuneOverride[chn] = -16 * (16 - tmpvalue);
          } else {
            // for XM replay
		    if (tmpvalue>=8)
		      XM7_TheModule->CurrentFinetuneOverride[chn] = +16 * (tmpvalue-8);
		    else
		      XM7_TheModule->CurrentFinetuneOverride[chn] = -16 * (8-tmpvalue);
          }
		  break;
		  
		case 0x6:
          // loop pattern
		  // Loops a section of a pattern x times. E60 sets the (optional) loop start point and 
		  // E6x with x values 1–F sets the end point and the number of iterations.
		  // If loop start point is not set, beginning of the pattern is used by default.
		  
          if (tmpvalue==0) {              // set loop begin point
		    if (curtick==0)
		      XM7_TheModule->CurrentLoopBegin[chn] = XM7_TheModule->CurrentLine;
		  } else {
	        resvalue=0x0E60 | tmpvalue;  // needs this to inform that we want to loop.
		  }
		  break;
		  
		case 0x7:
		  // Tremolo control
		  if (curtick==0) {
		    if ((tmpvalue!=3) && (tmpvalue<7))                    // 0,1,2,x,4,5,6,x
              XM7_TheModule->CurrentTremoloType[chn]=tmpvalue;
		    else if (tmpvalue==3)
		      XM7_TheModule->CurrentTremoloType[chn]=rand() % 3;      // set to 0,1,2
		    else if (tmpvalue==7)
		      XM7_TheModule->CurrentTremoloType[chn]=rand() % 3 + 4;  // set to 4,5,6
		  }
		  break;
     
	    case 0x8:
		  // "Sets the note stereo panning from far left 00 to far right FF overriding sample panning setting."
		  if (curtick==0)
	        XM7_TheModule->CurrentSamplePanning[chn] = (tmpvalue << 4) | tmpvalue;   // 0..0xff
	      break;
		  
		case 0x9:
		  // "Re-trigger note"
		  // "re-triggers a note every x ticks"
		  if (curtick!=0) {
		    if ((curtick % tmpvalue)==0) resvalue = 0xe91; else resvalue=0xe90;  // gives back 1 if retrig needed
		  }
		  break;
	     
	    case 0xa:
		  // "Fine volume slide up"
		  if (curtick==0) {
		    tmpvalue = MemoryEffectxxTogether (tmpvalue, &XM7_TheModule->EffectEAxMemory[chn]);
		    SlideSampleVolume (chn, tmpvalue);
          }
		  break;
		  
		case 0xb:
		  // "Fine volume slide down"
		  if (curtick==0) {
		    tmpvalue = MemoryEffectxxTogether (tmpvalue, &XM7_TheModule->EffectEAxMemory[chn]);
		    SlideSampleVolume (chn, -tmpvalue);
          }
	  	  break;
		  
		case 0xc:
		  // "Note cut"
		  // Cuts a note by setting its volume to 0 at tick precision.
		  // Possible parameter x values are 0 – (song speed - 1). Higher values have no effect.
		  if (curtick==tmpvalue)
		    XM7_TheModule->CurrentSampleVolume[chn]=0;
          break;
		  
		case 0xe:
		  // "Pattern delay": Delays playback progression for the duration of x rows
		  if (curtick==0)
		    XM7_TheModule->CurrentDelayLines = tmpvalue;
		  break;
	  }
     
	  break;
	  
	case 0xf:
	  // "set song speed"
	  //   - Parameter x values 01 – 1F  i.e. the amount of ticks per row.
	  //   - Values 20 – FF set the BPM which essentially is the speed of the ticks.
	  //   - F00 stops playback.
	  
	  if (curtick==0) {
	    if ((effpar>0x00) && (effpar<0x20)) {
          // values 01 – 1F  :set the amount of ticks per row
	      XM7_TheModule->CurrentTempo = effpar;
	    } else if ((effpar!=0x00) && (effpar>=0x20)) {
		  // values 20 – FF  :set the BPM
	      XM7_TheModule->CurrentBPM = effpar;
	      SetTimerSpeedBPM (effpar);
	    }
	  }
	  break;
	  
	case 0x10:                  // Gxx
	  // "Set global volume"
	  // "Sets the global song note volume"
	  if (curtick==0) {
	    if (effpar<=0x40) {
  	      XM7_TheModule->CurrentGlobalVolume = effpar;
		  ApplyNewGlobalVolume();
		}
	  }
	  break;
	  
	case 0x11:                  // Hxy
	  // "Global volume slide"
	  // "Slides global song volume up/down at speed x/y depending on which parameter is specified""
	  // if UP volume is != 0 then volume DOWN will be ignored.
	  // NOTE: the effect starts from tick=1, not on tick=0
	  
	  // effect memory
      effpar = MemoryEffectxxTogether (effpar, &XM7_TheModule->EffectHxyMemory[chn]);
	  
	  if (curtick>0) {
	    tmpvalue = (effpar >> 4);               // volume UP
	    if (tmpvalue!=0) {
		  XM7_TheModule->CurrentGlobalVolume += tmpvalue;
		  if (XM7_TheModule->CurrentGlobalVolume > 0x40)
		    XM7_TheModule->CurrentGlobalVolume = 0x40;
	    } else {
 	      tmpvalue = (effpar & 0x0F);             // volume DOWN
	      if (XM7_TheModule->CurrentGlobalVolume > tmpvalue)
		    XM7_TheModule->CurrentGlobalVolume -= tmpvalue;
		  else
		    XM7_TheModule->CurrentGlobalVolume = 0;
	    }
		ApplyNewGlobalVolume();
	  }
      break;
	 
	case 0x15:                  // Lxx
	  // "Set envelope position"
	  // "Makes the currently playing note jump to tick xx on the volume envelope timeline."
	  if (curtick==0) {        // is it right on tick == 0?
	    // does that instrument has an envelope?
	    if (XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VolumeType & 0x01) {
		 
		  u16 tmp;
		  // check IF we aren't running out of envelope...
		  tmp = XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VolumeEnvelopePoint[(XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->NumberofVolumeEnvelopePoints)-1].x;
	      if (effpar>tmp)
	        effpar=tmp;
			
		  // jump to this envelope point
		  XM7_TheModule->CurrentSampleVolumeEnvelopePoint[chn] = effpar;
		  
		  // set the new Volume value
		  CalculateEnvelopeVolume (chn, XM7_TheModule->CurrentChannelLastInstrument[chn]);
	    }
	  }
	  break;
	 
	case 0x19:                  // Pxy
	  // "Panning slide"
	  // "Slides note pan at speed x/y depending on which parameter is specified."
	  // if RIGHT pan is != 0 (x) then LEFT pan (y) will be ignored.
	  // NOTE: the effect starts from tick=1, not on tick=0
	  if (curtick>0) {
	    // effect memory
	    effpar = MemoryEffectxxTogether (effpar, &XM7_TheModule->EffectPxyMemory[chn]);
	    tmpvalue = (effpar >> 4);               // pan RIGHT
	    if (tmpvalue!=0) {
	      SlideSamplePan (chn, tmpvalue);
	    } else {
 	      tmpvalue = (effpar & 0x0F);             // pan LEFT
	      SlideSamplePan (chn, -tmpvalue);
	    }
	  }
	  break;
	  
	case 0x1b:                                   // Rxy
	  // "Re-trigger note with volume slide"
	  // "re-triggers a note while sliding its volume. Parameter x values affect note volume"
	  
	  resvalue = 0x1b00;
	  
	  if (curtick!=0) {
        // effect memory	  
	    effpar = MemoryEffectxySeparated (effpar, &XM7_TheModule->EffectRxyMemory[chn]);
	    
	    // take y part (retrig)
	    tmpvalue = (effpar & 0x0F);
		
		// if value isn't ZERO  
		if (tmpvalue!=0) {
          // check if retrig wanted 
		  if ((curtick % tmpvalue)==0)
		    resvalue = 0x1b01;             // gives back 1 if retrig needed
		}
		
		// take x part (volume)
		tmpvalue = (effpar >> 4);
		
		// change volume as requested
		ChangeVolumeonRetrigTable (chn, tmpvalue);
	  }
	  break;
	  
	case 0x1d:                                // Txy
	  // Tremor  (x+1 = tick on, y+1 = tick off )
	  // Rapidly alters note volume from full to zero, x and y setting the duration
	  // of the states in ticks.
	  
      if (curtick==0) {
	    effpar = MemoryEffectxxTogether (effpar, &XM7_TheModule->EffectTxyMemory[chn]);
      } else {
        effpar = XM7_TheModule->EffectTxyMemory[chn];
        XM7_TheModule->CurrentTremorMuting [chn] = (XM7_TheModule->CurrentTremorPoint [chn]>(effpar >> 4))?1:0;   // 1 = muting
        XM7_TheModule->CurrentTremorPoint [chn]  = (XM7_TheModule->CurrentTremorPoint [chn] + 1) % ( (effpar >> 4)+(effpar & 0x0f)+2 );  // tick % (x+y+2)
      }
      break;
	  
	case 0x23:                               // X1x - X2x
	  // extra-fine portamento up/down
	  tmpvalue = effpar & 0x0F;
	  switch (effpar >> 4) {
	    case 0x01:
		  // extra-fine portamento up
		  if (curtick==0) {
			tmpvalue = MemoryEffectxxTogether (tmpvalue, &XM7_TheModule->EffectX1xMemory[chn]);
			XM7_TheModule->CurrentSamplePortamento[chn] -= tmpvalue;
	      }
		  break;
		  
		case 0x02:
		  // extra-fine portamento down
	      if (curtick==0) {
			tmpvalue = MemoryEffectxxTogether (tmpvalue, &XM7_TheModule->EffectX2xMemory[chn]);
			XM7_TheModule->CurrentSamplePortamento[chn] += tmpvalue;
	      }
		  break;
		  
	  }
	  // needs this to keep pitching ON	
	  resvalue=0x0100;
	  break;
  }
  
  return (resvalue);
}

XM7_Sample_Type* GetSamplePointer (u8 note, u8 instrument) {
  
  if (instrument>0) {
    // my array starts from instrument # 0 ...  
    instrument--;

    // check if instrument is present
	if (XM7_TheModule->Instrument[instrument]!=NULL) {
      // obtain the number of the sample that should be played (note = 0..95) , num = 0..0x0f
      u8 sample_num = XM7_TheModule->Instrument[instrument]->SampleforNote[note];
      
      // obtain the pointer to the sample that should be played (and it can be NULL!)
      return (XM7_TheModule->Instrument[instrument]->Sample[sample_num]);
     
	} else {
      // this instrument doesn't exist
	  return (NULL);
	}
  } else {
    // no sense: instrument can't be 0 !!!
	return (NULL);
  }
}


int CalculateFreq (u8 mode, u8 note, s16 relativenote, s16 finetune, s32 periodpitch, s8 vibratopitch, s8 autovibratopitch, u8 glissandotype) {

  int freq=0;

  if (mode!=0) {

    //
    // LINEAR FREQ TABLE!  (the "normal" one...)
    //

    s16 newnote;
    s8 octave;
  
    if (periodpitch!=0) {                          // check if note is pitched
     
	  if (glissandotype) {                         // do we have steps in glissando?
        if (periodpitch & 0x20)
          periodpitch = (periodpitch & ~0x3f) + 0x40;   // more than half, go to next note
        else   
	      periodpitch = (periodpitch & ~0x3f);          // less than half, gets truncated
      }
     
      periodpitch *= 2;              // from x/64 to x/128
      finetune -= periodpitch;       // "pitching the finetuning"
     
      // moving the relativenote if finetunes is more than a semitone (means both upper or lower)
      relativenote += (finetune / 128);  // >> 7
      finetune %= 128;                    // & 0x7F
    }
  
    // vibratopitching
    if (vibratopitch!=0) {
      finetune -= vibratopitch;      // vibrato pitch already in x/128
	 
	  // moving the relativenote if finetunes is more than a semitone (means both upper or lower)
      relativenote += (finetune / 128);  // >> 7
      finetune %= 128;                    // & 0x7F
    }
  
    // instrument 'auto'vibrato pitching
    if (autovibratopitch!=0) {
      finetune -= autovibratopitch;      // autovibratopitch pitch already in x/128
     
      // moving the relativenote if finetunes is more than a semitone (means both upper or lower)
      relativenote += (finetune / 128);  // >> 7
      finetune %= 128;                    // & 0x7F
    }

    newnote = note + relativenote;  // add the sample' relative note
    octave = newnote / 12;          // div
    note = newnote % 12;            // mod
  
    octave = BASEOCTAVE - octave;
    freq = SampleFrequency [note];
  
    // if octave <>0 then 'shift' note   
    if (octave>0) {
      freq >>= octave;
    } else {
      if (octave<0) {
	    freq <<= -octave;
	  }
    }
  
  } else {
  
    //
    //  AMIGA FREQ TABLE (the "MOD compatible" one)
    //
    
    // period pitch should be in x/4th of period unit
    if (periodpitch!=0) {                // check if note is pitched
     
      periodpitch /= 4;                  // truncate to units
     
	  if (glissandotype) {               // do we have steps in glissando?
        note = FindClosestNoteToAmigaPeriod (GetAmigaPeriod (note) + periodpitch);
        periodpitch = 0;
      }
    }
    
    // vibratopitching
    if (vibratopitch!=0) {               // if note is vibrato pitched
      periodpitch += vibratopitch/8;     // vibrato pitch is in x/128
    }
  
    // instrument 'auto'vibrato pitching
    if (autovibratopitch!=0) {           // if note is auto-vibrato pitched
      finetune += autovibratopitch/8;    // autovibratopitch pitch is in x/128
    }
    
    // calculate period and then frequency
    int period = GetAmigaPeriod (note) + periodpitch;
    freq = AMIGAMAGICNUMBER / period;
    
    // now fix freq with the sample's relative note
    if (relativenote>0) {
      freq <<= (relativenote / 12);
      relativenote %= 12;
      while (relativenote>0) {
        freq = (freq * FineTunes[16]) >> FINETUNEPRECISION;
        relativenote--;
      }  
    } else if (relativenote<0) {
      relativenote = -relativenote;
      freq >>= (relativenote / 12);
      relativenote %= 12;
      while (relativenote>0) {
        freq = (freq << FINETUNEPRECISION) / FineTunes[16];
        relativenote--;
      }
    }
  }
  
  // fine-tuning (simple!) (works for both AMIGA and LINEAR system)
  if (finetune!=0) {
    if (finetune>0) {
	  freq = (freq * VeryFineTunes[finetune]) >> FINETUNEPRECISION;
	} else {
	  freq = (freq << FINETUNEPRECISION) / VeryFineTunes[-finetune];
	}
  }
  
  return (freq);
}


s8 CalculateAutoVibrato (u8 chn, u8 instrument) {

  XM7_Instrument_Type* instr = XM7_TheModule->Instrument[instrument-1];
  s8 autovib=0;   // will be returned in x/128th of semitone
    
  switch (instr->VibratoType) {
  
    // sinus
    case 0:autovib = (CalculateModulatorValue (0,XM7_TheModule->CurrentAutoVibratoPoint[chn]>>2) * instr->VibratoDepth) >> 7;
           break;

    // square
    case 1:autovib = (instr->VibratoDepth << 3);
           if (XM7_TheModule->CurrentAutoVibratoPoint[chn]>127)
             autovib = -autovib;
           break;
    
    // saw down 
    case 2:autovib = ((0x80 - XM7_TheModule->CurrentAutoVibratoPoint[chn]) * (instr->VibratoDepth)) >> 4;
           break;

    // saw up
    case 3:autovib = ((XM7_TheModule->CurrentAutoVibratoPoint[chn] - 0x80) * (instr->VibratoDepth)) >> 4;
           break;
  }
  
  // apply Sweep
  autovib = (XM7_TheModule->CurrentAutoVibratoSweep[chn] * autovib) >> 16;
  
  // seems we've got too much depth
  // BETA! trying with this:
  autovib >>=1;
  
  // value ready!
  return (autovib);
}


void PitchNote (u8 chn, u8 pitch, s32 porta, s8 vibra) {

  u8 note = XM7_TheModule->CurrentChannelLastNote[chn]-1;
  u8 instrument = XM7_TheModule->CurrentChannelLastInstrument[chn];
  u8 glis = XM7_TheModule->CurrentGlissandoType[chn];

  XM7_Sample_Type* sample_ptr = GetSamplePointer(note, instrument);
  
  if (sample_ptr!=NULL) {
     
    s8 finetune = (XM7_TheModule->CurrentFinetuneOverrideOn[chn])?XM7_TheModule->CurrentFinetuneOverride[chn]:(sample_ptr->FineTune & 0xF8);
    // finetune = (finetune)?finetune:sample_ptr->FineTune; ????
   
    s8 autovibra = ((instrument!=0) && (XM7_TheModule->Instrument[instrument-1]->VibratoDepth!=0) && (XM7_TheModule->Instrument[instrument-1]->VibratoRate!=0))?
                   CalculateAutoVibrato (chn, instrument):0;
     
    int freq=CalculateFreq (XM7_TheModule->FreqTable, (note+pitch), sample_ptr->RelativeNote, finetune, porta, vibra, autovibra, glis);

    XM7_lowlevel_pitchSound (freq, chn);
  }
}

void PlayNote (u8 chn, u16 sample_offset) {
 
  u8 note = XM7_TheModule->CurrentChannelLastNote[chn]-1;
  u8 instrument = XM7_TheModule->CurrentChannelLastInstrument[chn];
  u8 glis = XM7_TheModule->CurrentGlissandoType[chn];
  s8 vibra = XM7_TheModule->CurrentVibratoValue[chn];

  XM7_Sample_Type* sample_ptr = GetSamplePointer(note,instrument);
  
  if (sample_ptr!=NULL) {

    s8 finetune = (XM7_TheModule->CurrentFinetuneOverrideOn[chn])?XM7_TheModule->CurrentFinetuneOverride[chn]:(sample_ptr->FineTune & 0xF8);
   
    s8 autovibra = ((instrument!=0) && (XM7_TheModule->Instrument[instrument-1]->VibratoDepth!=0) && (XM7_TheModule->Instrument[instrument-1]->VibratoRate!=0))?
                   CalculateAutoVibrato (chn, instrument):0;    

    int freq=CalculateFreq (XM7_TheModule->FreqTable, note, sample_ptr->RelativeNote, finetune, 0, vibra, autovibra, glis);
	
    u8 volume = XM7_TheModule->CurrentSampleVolume[chn];
	s8 tremolo = XM7_TheModule->CurrentTremoloVolume[chn];
	
  	if (tremolo>0) {
      if ((volume + tremolo)>0x40)
  	    volume=0x40;
	  else
	    volume+=tremolo;
    } else if (tremolo<0) {
      if (volume < -tremolo)
  	    volume=0;
	  else
	    volume+=tremolo;
    }

    // final calculation of volume & panning
    volume  = CalculateFinalVolume  (volume ,XM7_TheModule->CurrentSampleVolumeEnvelope[chn], XM7_TheModule->CurrentSampleVolumeFadeOut[chn]);
    u8 panning = CalculateFinalPanning (chn, XM7_TheModule->CurrentSamplePanning[chn], XM7_TheModule->CurrentSamplePanningEnvelope[chn]);
  
    // ***************************** READY TO PLAY SAMPLE !!! *******************************************
  
    // check if the sample has a loop or not
    if ( (sample_ptr->Flags & 0x01) == 0) {
	// no loop
	  XM7_lowlevel_startSound (freq,sample_ptr->SampleData,sample_ptr->Length,chn,volume,panning,(sample_ptr->Flags >> 4),sample_offset);
    } else {
      // has a loop
	  XM7_lowlevel_startSoundwLoop (freq,sample_ptr->SampleData,sample_ptr->LoopLength,sample_ptr->LoopStart,chn,volume,panning,(sample_ptr->Flags >> 4),sample_offset);
    }
  } else {
    // it's NULL: play nothing! (stop this channel)
    XM7_lowlevel_startSound (0,NULL,0,chn,0,0,0,0);
  }
}

void ChangeSample (u8 chn) {

  u8 note = XM7_TheModule->CurrentChannelLastNote[chn]-1;
  u8 instrument = XM7_TheModule->CurrentChannelLastInstrument[chn];
  XM7_Sample_Type* sample_ptr = GetSamplePointer(note,instrument);
  
  if (sample_ptr!=NULL)
    XM7_lowlevel_changeSample (sample_ptr->SampleData,sample_ptr->LoopLength,sample_ptr->LoopStart,chn,(sample_ptr->Flags >> 4));
  else  // should 'play silence'
    XM7_lowlevel_changeSample (&XM7_TheModule->Silence,4,0,chn,0);

}

void Timer1Handler (void) {
  // this gets called each time Timer1 'overflows'
  XM7_SingleNoteArray_Type* CurrNoteLine;
  XM7_SingleNote_Type* CurrNote = NULL;
  
  u8 chn;
  u8 EDxInAction;
  u8 EnvStartPoint;
  
  // boolean flags that define what should be done in the end
  u8 ShouldTriggerNote;
  u8 ShouldChangeVolume;
  u8 ShouldPitchNote;
  u8 ShouldRestartEnvelope;
  u8 ShouldTriggerKeyOff;
  u8 ShouldChangeInstrument;
  
  // "keep" flags
  u8 KeepArpeggioedNote;

  u8 PitchToNote;
  u8 OverrideFinetune;
  
  // values for intonation
  u8 ArpeggioValue;
  
  u16 effres;
  u16 SampleStartOffset;
  
  // next aren't "for channel", so initialization is required
  
  // Bxx & Dxx
  s16 NextPatternPosition = -1;
  u8 NextPatternStartLine = 0;
  u8 BreakThisPattern = NO;
  
  // loops
  u8 RequestedLoops = 0;
  u8 CurrentLoopEffChannel = 0;
  
   // for every channel
  for (chn=0;chn<(XM7_TheModule->NumberofChannels);chn++) {
  
	ShouldTriggerNote = NO;
	ShouldChangeVolume = NO;
	ShouldRestartEnvelope = NO;
	ShouldPitchNote = NO;
    ShouldTriggerKeyOff = NO;
    ShouldChangeInstrument = NO;
	
	KeepArpeggioedNote = NO;

	PitchToNote = NO;
	OverrideFinetune = NO;
	
	EDxInAction = 0;
	SampleStartOffset = 0;
	EnvStartPoint = 0;

	ArpeggioValue = 0;	
	
	// read the line and do what's written
	CurrNoteLine = (XM7_SingleNoteArray_Type*) &(XM7_TheModule->Pattern[XM7_TheModule->CurrentPatternNumber]->Noteblock[XM7_TheModule->CurrentLine * (XM7_TheModule->NumberofChannels)]);
	CurrNote = &(CurrNoteLine->Noteblock[chn]);
	
	// decode effects that could apply NOW!
	effres=DecodeBeforeEffectsColumn (chn, CurrNote->EffectType, CurrNote->EffectParam, XM7_TheModule->CurrentTick);
	switch (effres >> 4) {
	  case 0x000:break;
	  
	  case 0x030:PitchToNote = YES;
	             break;
	 
	  case 0x0b0 ... 0xbf:BreakThisPattern = YES;
	                      NextPatternPosition = (effres & 0x00ff);
						  break;
	 
	  case 0x0d0 ... 0xdf:BreakThisPattern = YES;
	                      NextPatternStartLine = (effres & 0x00ff);
	                      break;
     
      case 0x0ED:EDxInAction = (effres & 0x000f);  // EDx
				 break;
				 
	  case 0x0E5:OverrideFinetune = YES;     //  E5x
	             break;
	 
      case 0x140:ShouldTriggerKeyOff = YES;  // Kxx
	             break;
	 
	  case 0x150 ... 0x15f:if (XM7_TheModule->CurrentTick==0)     // Lxx
	                          EnvStartPoint = (effres & 0x00ff);
	                        break;
    }
	
	// check if portamento to note (Mx)
	if ((CurrNote->Volume>=0xf0) && (CurrNote->Volume<=0xff))
	  PitchToNote = YES;

    // is there a note specified?
	if ((CurrNote->Note>0) && (CurrNote->Note<97)) {
      // is there a 3xx specified?
	  if (!PitchToNote) {
		if (XM7_TheModule->CurrentTick==EDxInAction) {
		  
	      XM7_TheModule->CurrentChannelLastNote[chn]=CurrNote->Note;
		  XM7_TheModule->CurrentSamplePortamento[chn] = 0;
	     
		  // instrument specified?
		  if (CurrNote->Instrument!=0) {
		    XM7_TheModule->CurrentChannelLastInstrument[chn] = CurrNote->Instrument;
			XM7_Sample_Type* sample_ptr = GetSamplePointer((CurrNote->Note-1),CurrNote->Instrument);
            ShouldRestartEnvelope = YES;
            // sample_ptr can be NULL!
            if (sample_ptr!=NULL) {
		      XM7_TheModule->CurrentSampleVolume[chn]  = sample_ptr->Volume;
	          XM7_TheModule->CurrentSamplePanning[chn] = sample_ptr->Panning;
            } else {
              // mute the channel ('old' trick)
              XM7_TheModule->CurrentSampleVolume[chn] = 0;
            }
		  }
          
          // EDx specified? (means that envelope has to be restarted!)
          if ((EDxInAction!=0) && (XM7_TheModule->CurrentChannelLastInstrument[chn]!=0))
            ShouldRestartEnvelope = YES;
		  
		  // trigger ONLY if there has been an instrument specified before, 'somewhere in time'!
		  // (it means also thay if CurrNote->Instrument was 0 then Vol&Pan will be RETAINED!
		  if (XM7_TheModule->CurrentChannelLastInstrument[chn]!=0) {
			// XM7_TheModule->CurrentFinetuneOverrideOn[chn] = OverrideFinetune;
			ShouldTriggerNote = YES;
		  }
		  
		}
	  } else {
		// we want to pitch toward this note
        if (XM7_TheModule->FreqTable!=0)
          // LINEAR FREQ TABLE (semitones*16*4)
		  XM7_TheModule->CurrentSamplePortaDest[chn] = (XM7_TheModule->CurrentChannelLastNote[chn] - CurrNote->Note) * (4*16);
        else
          // AMIGA FREQ TABLE (periods*4)
          XM7_TheModule->CurrentSamplePortaDest[chn] = (GetAmigaPeriod(CurrNote->Note-1) - GetAmigaPeriod (XM7_TheModule->CurrentChannelLastNote[chn]-1)) * 4;
	  }
	}
    
	if (CurrNote->Note==97) {
	  // it's a key off:
	  if (XM7_TheModule->CurrentTick==EDxInAction)
	    ShouldTriggerKeyOff = YES;
	}

	if (ShouldTriggerKeyOff) {
	  // key-off, should be like that
	  if (XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]!=ENVELOPE_NONE) {
      
        // volume envelope should go to RELEASE state
	    XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]=ENVELOPE_RELEASE;
        
        // maybe there's also a PANNING envelope
        if (XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]!=ENVELOPE_NONE)
          XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]=ENVELOPE_RELEASE;
        
	  } else {
		// no envelope, stop the channel
        // stopSound (chn);
		// no! lower volume to ZERO!
		XM7_TheModule->CurrentSampleVolume[chn]=0;
		ShouldChangeVolume = YES;
	  }
	}

	// is there an instrument specified (without note!) ?
	// OR is there even a note BUT it's specified for bending?
	if (((CurrNote->Instrument!=0) && (CurrNote->Note==0)) ||
	    ((CurrNote->Instrument!=0) && (PitchToNote))             ) {
        
      //  **** BETA: ProTracker on-the-fly sample change emulation  ******************
      if (XM7_TheModule->ReplayStyle & XM7_REPLAY_ONTHEFLYSAMPLECHANGE_FLAG)
        if (XM7_TheModule->CurrentTick==EDxInAction)
          if (XM7_TheModule->CurrentChannelLastInstrument[chn]!=CurrNote->Instrument)
            if (XM7_TheModule->CurrentChannelLastNote[chn]!=0) {
              // save the new instrument number and trigger instrument change
              XM7_TheModule->CurrentChannelLastInstrument[chn]=CurrNote->Instrument;
              ShouldChangeInstrument = YES;
          }
      //  ************************************************************************ END ****
        
	  //  ... and check if there's a last note! otherwise simply ignore it!
	  if (XM7_TheModule->CurrentChannelLastNote[chn]!=0) {
	    // reset volume & panning
		if (XM7_TheModule->CurrentTick==EDxInAction) {
          XM7_Sample_Type* sample_ptr = GetSamplePointer ((XM7_TheModule->CurrentChannelLastNote[chn]-1), XM7_TheModule->CurrentChannelLastInstrument[chn]);
          if (sample_ptr!=NULL) {
		    XM7_TheModule->CurrentSampleVolume[chn] = sample_ptr->Volume;
	        XM7_TheModule->CurrentSamplePanning[chn]= sample_ptr->Panning;
            
            ShouldChangeVolume = YES;
  	        ShouldRestartEnvelope = YES;  // reset envelope too!
          } 
          
          /* else {
            // try muting the volume if the sample doesn't exists
            XM7_TheModule->CurrentSampleVolume[chn] = 0;
          } */
         
          
		}
	  }
	}
    
    // if EDx (w/ x>0) you should trigger note (and its envelope) even if there's no note 
    // and/or no instrument. Reset portamento too!
    if ((EDxInAction!=0) && (XM7_TheModule->CurrentTick==EDxInAction) && (CurrNote->Note==0)) {
      if ((XM7_TheModule->CurrentChannelLastNote[chn]!=0) && (XM7_TheModule->CurrentChannelLastInstrument[chn]!=0)) {
        // reset portamento to last note  
        XM7_TheModule->CurrentSamplePortamento[chn] = 0;        
        // retrigger note & restart envelope  
        ShouldRestartEnvelope = YES;
        ShouldTriggerNote = YES;
      }
    }
	
	// should we keep note arpeggioed?
	// if (XM7_TheModule->CurrentTick==0)
	//   KeepArpeggioedNote = NO;
    
    // check if we need to retrigger vibrato/tremolo/tremor
    if (ShouldTriggerNote || ShouldRestartEnvelope) {
	 
	  // should we REtrigger vibrato wave?
	  if (XM7_TheModule->CurrentVibratoType[chn]<4) {
	    XM7_TheModule->CurrentVibratoPoint[chn]=0;
        XM7_TheModule->CurrentVibratoValue[chn]=0;  // BETA (?)
      }
	 
	  // should we REtrigger tremolo wave?
	  if (XM7_TheModule->CurrentTremoloType[chn]<4) {
      XM7_TheModule->CurrentTremoloPoint [chn]=0;
      XM7_TheModule->CurrentTremoloVolume[chn]=0;  // BETA (?)
      }
      
      // Retrigger Tremor wave
      XM7_TheModule->CurrentTremorMuting[chn]=0;
      XM7_TheModule->CurrentTremorPoint [chn]=0;
      
      // Retrigger Instrument (auto) Vibrato
      XM7_TheModule->CurrentAutoVibratoSweep[chn]=0;
      XM7_TheModule->CurrentAutoVibratoPoint[chn]=0;
    }
    
    // autovibrato (if is ON, it means that we should change pitch in this tick)
    if (XM7_TheModule->CurrentChannelLastInstrument[chn]>0) {
      // check if this instrument exists before accessing its data!!!
      if (XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]!=NULL) {
        if ((XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VibratoDepth!=0) && 
            (XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VibratoRate!=0)) {
         
          // instrument autovibrato sweep
          XM7_TheModule->CurrentAutoVibratoSweep[chn] += XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VibratoSweep;
          if (XM7_TheModule->CurrentAutoVibratoSweep[chn]>0x10000)
            XM7_TheModule->CurrentAutoVibratoSweep[chn]=0x10000;
          
          ShouldPitchNote = YES;
        }
      }
    }

   	// is there a Volume col?
	if (CurrNote->Volume>=0x10) {
	  effres=DecodeVolumeColumn (chn, CurrNote->Volume, XM7_TheModule->CurrentTick, EDxInAction);
	  if (effres & 0x0001)
	    ShouldChangeVolume = YES;
	  if (effres & 0x0002)
	  	ShouldPitchNote = YES;
	}


    // is there an EFFECT?
	if ((CurrNote->EffectType!=0x00) || (CurrNote->EffectParam!=0x00)) {
	 
	  effres=DecodeEffectsColumn (chn, CurrNote->EffectType, CurrNote->EffectParam, XM7_TheModule->CurrentTick, XM7_TheModule->CurrentAdditionalTick);
		
	  switch (effres >> 8) {
	    case 0x00:ArpeggioValue = effres & 0x000f;
				  XM7_TheModule->CurrentChannelIsArpeggioedNote[chn] = YES;
				  ShouldPitchNote = YES;
				  KeepArpeggioedNote = YES;
		          break;
		
		case 0x01:ShouldPitchNote = YES;
		          break;
				  
        case 0x05:ShouldPitchNote = YES;
		          ShouldChangeVolume = YES;
		          break;
		
		case 0x09:SampleStartOffset = (effres & 0x00ff) << 8;
		          break;
		
	    case 0x0e:switch (effres >> 4) {
		            case 0xe9:if (effres & 0x001)
					            if (XM7_TheModule->CurrentChannelLastInstrument[chn]) {
					              ShouldTriggerNote=YES;
                                  ShouldRestartEnvelope = YES;
                                }
					          break;
					
					case 0xe6:RequestedLoops = (effres & 0x000F);
					          CurrentLoopEffChannel = chn;
							  XM7_TheModule->CurrentLoopEnd[CurrentLoopEffChannel] = XM7_TheModule->CurrentLine;
					          break;
		          }
		          break;
		
		case 0x1b:if (effres & 0x0001) {                   // Rxy
		            if (XM7_TheModule->CurrentChannelLastInstrument[chn]) {
		              ShouldTriggerNote = YES;
                      // ShouldRestartEnvelope = YES;  // Rxy won't reset envelope!
                    }
					
				    // if there's a volume specified (xx), we should reset before retrigger!
				    // ... I know it's strange, but FT2 works that way...
	                if ( (CurrNote->Volume>=0x10) && (CurrNote->Volume<=0x40) )
	                  effres=DecodeVolumeColumn (chn, CurrNote->Volume, 0, 0);
				  }
                 
				  // if it's not the 1st tick you should change volume
				  if (XM7_TheModule->CurrentTick!=0)
				    if (effres & 0x0001)
		              ShouldChangeVolume = YES;
				  break;
	 /*
		case 0x1d:if (effres & 0x0001)       // Txy
		            TremorisMuting = YES;
                  ShouldChangeVolume = YES;
		          break;  */
	 
		case 0xff:ShouldChangeVolume = YES;
		          break;
	  }
	}
	
	
	// *******************  Arpeggio (reset?) *************  
    // was the note arpeggioed and should be reset?
    if (XM7_TheModule->CurrentChannelIsArpeggioedNote[chn] && (!KeepArpeggioedNote) &&
                                                          (XM7_TheModule->CurrentTick==0) ) {
	  XM7_TheModule->CurrentChannelIsArpeggioedNote[chn] = NO;
	  ShouldPitchNote = YES;
	}
	

    // *******************  ENVELOPES!  *************  
	if (ShouldRestartEnvelope) {
	  // we need to start an envelope, if needed
	  StartEnvelope(chn, EnvStartPoint);
      ShouldChangeVolume = YES;
	} else {
	  // check if we need to go on with an envelope
	  if ((XM7_TheModule->CurrentSampleVolumeEnvelopeState[chn]!=ENVELOPE_NONE) ||
          (XM7_TheModule->CurrentSamplePanningEnvelopeState[chn]!=ENVELOPE_NONE)) {
	    // there's an envelope to follow...
	    ElaborateEnvelope (chn, XM7_TheModule->CurrentChannelLastInstrument[chn]);
		ShouldChangeVolume = YES;
	  }
	}
    
    // E5x: activate/cancel it if there's a new trigger
    if (ShouldTriggerNote)
      XM7_TheModule->CurrentFinetuneOverrideOn[chn] = OverrideFinetune;
	
	// *******************  ACTION!!!! **************
	// DO what is needed for this channel
	if (ShouldTriggerNote) {
	  PlayNote (chn,SampleStartOffset);
	} else {
      // **** BETA: ProTracker on-the-fly sample change emulation  ******************    
      if (ShouldChangeInstrument)
        ChangeSample (chn);
      // ****************************************************************************
	  if (ShouldChangeVolume)
	    ApplyVolumeandPanning (chn);
	  if (ShouldPitchNote)
	    PitchNote (chn, ArpeggioValue, XM7_TheModule->CurrentSamplePortamento[chn], XM7_TheModule->CurrentVibratoValue[chn]);
	}

    // last thing to do
    // Autovibrato: move to next point, if needed (if it's ON!)
    if (XM7_TheModule->CurrentChannelLastInstrument[chn]!=0) {
      // check if this instrument exists before accessing its data!!!
      if (XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]!=NULL) {
        if ((XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VibratoDepth!=0) && 
            (XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VibratoRate!=0)) {
         
          // move point forward, for next
          XM7_TheModule->CurrentAutoVibratoPoint[chn] += XM7_TheModule->Instrument[XM7_TheModule->CurrentChannelLastInstrument[chn]-1]->VibratoRate;
         }
       }
    }
	
  }  // end FOR channels
  
  
  // calculate delay ticks from delay lines
  if ((XM7_TheModule->CurrentDelayLines) || (XM7_TheModule->CurrentDelayTick==0)) {
    XM7_TheModule->CurrentDelayTick = XM7_TheModule->CurrentDelayLines * XM7_TheModule->CurrentTempo;
    XM7_TheModule->CurrentDelayLines = 0;
  }
  
  // increments the tick...
  XM7_TheModule->CurrentTick++;

  // check if it's time to change line/pattern
  if ( (XM7_TheModule->CurrentTick) >= (XM7_TheModule->CurrentTempo) ) {
    // end of the line: check if there are some DelayTick (effect EEx)
    if (XM7_TheModule->CurrentDelayTick>0) {
	  // I should wait before changing line...
	  XM7_TheModule->CurrentDelayTick--;
	  XM7_TheModule->CurrentAdditionalTick++;
	  XM7_TheModule->CurrentTick--;
	} else {
	  // next line! (whatever 'next' means...)
	  XM7_TheModule->CurrentTick = 0;
	  XM7_TheModule->CurrentAdditionalTick = 0;
	  
	  // check if we should loop in this pattern
	  if (RequestedLoops>(XM7_TheModule->CurrentLoopCounter[CurrentLoopEffChannel])) {
	    XM7_TheModule->CurrentLine = XM7_TheModule->CurrentLoopBegin[CurrentLoopEffChannel];
		XM7_TheModule->CurrentLoopCounter[CurrentLoopEffChannel]++;
	  } else {
	  
		// go to NEXT line
	    XM7_TheModule->CurrentLine++;
		
		// if loopend passed, reset the loop counter
		if (XM7_TheModule->CurrentLine>XM7_TheModule->CurrentLoopEnd[CurrentLoopEffChannel])
		  XM7_TheModule->CurrentLoopCounter[CurrentLoopEffChannel]=0;
		
	    // now check if pattern is over (or should be breaked!)
	    if ( BreakThisPattern || ((XM7_TheModule->CurrentLine) >= (XM7_TheModule->PatternLength[XM7_TheModule->CurrentPatternNumber])) ) {
  	      // next pattern!
	      XM7_TheModule->CurrentLine=NextPatternStartLine;  // should be 0 when not using Dxx
         
		  // NextPatternPosition comes from Bxx
		  if ( (NextPatternPosition>=0) && (NextPatternPosition<XM7_TheModule->ModuleLength) )
		    XM7_TheModule->CurrentSongPosition = NextPatternPosition;
		  else
	        XM7_TheModule->CurrentSongPosition++;
		  
	      // check if song is finished... it is, we've got to restart!
	      if ( (XM7_TheModule->CurrentSongPosition) >= (XM7_TheModule->ModuleLength) )
		    XM7_TheModule->CurrentSongPosition = XM7_TheModule->RestartPoint;
          
		  // set new currentpatternnumber!
		  XM7_TheModule->CurrentPatternNumber=XM7_TheModule->PatternOrder[XM7_TheModule->CurrentSongPosition];
		  
          // reset the loopbegin[], we are in a new pattern!
		  for (chn=0;chn<16;chn++)
		    XM7_TheModule->CurrentLoopBegin[chn]=0;
        }
      }
	  
	}
  }
  
}


void XM7_PlayModuleFromPos (XM7_ModuleManager_Type* TheModule, u8 position) {

  XM7_TheModule = TheModule;
 
  // set, ready...

  // re-set the Module tempo & bpm & global volume
  XM7_TheModule->CurrentBPM = XM7_TheModule->DefaultBPM;
  XM7_TheModule->CurrentTempo = XM7_TheModule->DefaultTempo;
  XM7_TheModule->CurrentGlobalVolume = 0x40;

  // re-set the Module position
  XM7_TheModule->CurrentSongPosition  = (position<XM7_TheModule->ModuleLength)?position:0;
  XM7_TheModule->CurrentPatternNumber = XM7_TheModule->PatternOrder[XM7_TheModule->CurrentSongPosition];
  XM7_TheModule->CurrentLine = 0;
  XM7_TheModule->CurrentTick = 0;

  // other...
  XM7_TheModule->CurrentDelayLines = 0;
  XM7_TheModule->CurrentDelayTick = 0;
  XM7_TheModule->CurrentAdditionalTick = 0;

  u8 i;		
  for (i=0;i<16;i++) {
  
    // re-set the channels
	XM7_TheModule->CurrentChannelLastNote[i]       = 0;               // empty
	XM7_TheModule->CurrentChannelLastInstrument[i] = 0;               // empty
  
    // re-set volume & panning & envelope
	XM7_TheModule->CurrentSampleVolume [i] = 0;                       // mute
	XM7_TheModule->CurrentSamplePanning[i] = 0x80;                    // center
	XM7_TheModule->CurrentSampleVolumeEnvelopeState[i]=ENVELOPE_NONE;
    XM7_TheModule->CurrentSamplePanningEnvelopeState[i]=ENVELOPE_NONE;
		  
	// "zero" the effects memory!
    XM7_TheModule->Effect1xxMemory[i]=0;
	XM7_TheModule->Effect2xxMemory[i]=0;
	XM7_TheModule->Effect3xxMemory[i]=0;
	XM7_TheModule->Effect4xxMemory[i]=0;
	XM7_TheModule->EffectAxyMemory[i]=0;
	XM7_TheModule->EffectE1xMemory[i]=0;
	XM7_TheModule->EffectE2xMemory[i]=0;
	XM7_TheModule->EffectEAxMemory[i]=0;
	XM7_TheModule->EffectPxyMemory[i]=0;
	XM7_TheModule->EffectRxyMemory[i]=0x80;             // no volume change
    XM7_TheModule->EffectTxyMemory[i]=0x00;             // Fast tremor
	XM7_TheModule->EffectX1xMemory[i]=0;
	XM7_TheModule->EffectX2xMemory[i]=0;
	
	// re-set pitch & porta & stuff
	XM7_TheModule->CurrentChannelIsArpeggioedNote[i] = NO;
	XM7_TheModule->CurrentSamplePortamento[i] = 0;
	XM7_TheModule->CurrentGlissandoType[i] = 0;
	
	// re-set loops to line 0
	XM7_TheModule->CurrentLoopBegin  [i] = 0;
	XM7_TheModule->CurrentLoopCounter[i] = 0;
    XM7_TheModule->CurrentLoopEnd    [i] = 0;
	
	// re-set vibrato
	XM7_TheModule->CurrentVibratoValue [i] = 0;
	XM7_TheModule->CurrentVibratoType  [i] = 0;
	XM7_TheModule->CurrentVibratoPoint [i] = 0;
	
	// re-set tremolo
	XM7_TheModule->CurrentTremoloVolume[i]=0;
	XM7_TheModule->CurrentTremoloType  [i]=0;
	XM7_TheModule->CurrentTremoloPoint [i]=0;
    
    // re-set instrument auto-vibrato
    XM7_TheModule->CurrentAutoVibratoSweep[i]=0;
    XM7_TheModule->CurrentAutoVibratoPoint[i]=0;
	
	// finetuning override
	XM7_TheModule->CurrentFinetuneOverride   [i]= 0;
	XM7_TheModule->CurrentFinetuneOverrideOn [i]= NO;
  }
  
  // the silence sample
  XM7_TheModule->Silence = 0x00000000;
  
  // ... GO!
  
  // 1st: set up the IRQ handler for the timer1 and enable the IRQ.
  irqSet(IRQ_TIMER1,Timer1Handler);
  irqEnable(IRQ_TIMER1);

  // then set the timer and make it start!
  TIMER1_CR = TIMER_DIV_1024 | TIMER_IRQ_REQ;
  SetTimerSpeedBPM (XM7_TheModule->DefaultBPM);
  
  // set engine state
  XM7_TheModule->State = XM7_STATE_PLAYING;
}


void XM7_PlayModule (XM7_ModuleManager_Type* TheModule) {
  XM7_PlayModuleFromPos (TheModule, 0);
}


void XM7_StopModule() {

  u8 i;

  // will deactivate the timer IRQ (and stop the channels)
  TIMER1_CR = 0;
  irqDisable(IRQ_TIMER1);

  for (i=0;i<(XM7_TheModule->NumberofChannels);i++)
	XM7_lowlevel_stopSound (i);
    
  // change the state
  XM7_TheModule->State = XM7_STATE_STOPPED;
}
/*
void XM7_PauseModule() {
  int i;
  
  // will deactivate the timer IRQ (and stop the channels)
  TIMER1_CR = 0;
  irqDisable(IRQ_TIMER1);
  
  for (i=0;i<(XM7_TheModule->NumberofChannels);i++)
	XM7_lowlevel_pauseSound (i);
}

void XM7_ResumeModule() {
  int i;
  // then set the timer and make it start!
  TIMER1_CR = TIMER_DIV_1024 | TIMER_IRQ_REQ;
  SetTimerSpeedBPM (XM7_TheModule->CurrentBPM);
  irqEnable(IRQ_TIMER1);
  
  for (i=0;i<(XM7_TheModule->NumberofChannels);i++)
	XM7_lowlevel_resumeSound (i);
}
*/
void XM7_Initialize() {
  CalculateVeryFineTunes();
  // CalculateRealPanningArray (45);   //  (35% of 128 = 44,8)
}



















	  /*
	  case PLAY_ONE_SHOT_SAMPLE:
	    // TEST: play a sample (no looping) on the req'd channel
	    startSound(IPC_ptr->Sample.Frequency,IPC_ptr->Sample.Data,IPC_ptr->Sample.Length,IPC_ptr->Sample.Channel, IPC_ptr->Sample.Volume,64,0);
	    break;
	  case PLAY_LOOPING_SAMPLE:
		// TEST: play a sample (WITH looping) on the req'd channel
	    startSoundwLoop(IPC_ptr->Sample.Frequency,IPC_ptr->Sample.Data,IPC_ptr->Sample.Length,IPC_ptr->Sample.LoopStart,IPC_ptr->Sample.Channel, IPC_ptr->Sample.Volume,64,0);
	    break;
	  */
	  
	  
	  
	  
	  
	  
	  
  /*
  u16 timer = (1963710 / (XM7_TheModule->DefaultBPM)) / (6 * 4);
  TIMER1_DATA = - timer;
  */
  
  
  
  
  /*
void startSound(int sampleRate, const void* data, u32 bytes, u8 channel, u8 vol,  u8 pan, u8 format) {

  SCHANNEL_CR(channel) = 0;

  if (bytes>0) {
	SCHANNEL_TIMER(channel)  = SOUND_FREQ(sampleRate);
	SCHANNEL_SOURCE(channel) = (u32)data;
	SCHANNEL_LENGTH(channel) = bytes >> 2;
	SCHANNEL_CR(channel)     = SCHANNEL_ENABLE | SOUND_ONE_SHOT | SOUND_VOL(vol) | SOUND_PAN(pan) | (format==0?SOUND_8BIT:SOUND_16BIT);
  }
}
*/



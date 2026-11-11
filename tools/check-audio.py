#!/usr/bin/env python3
"""Verify the emulated AC97 output, not merely decoder or DMA counters."""
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import wave


def check(path):
    with wave.open(str(path)) as wav:
        assert wav.getnchannels()==2 and wav.getsampwidth()==2
        rate=wav.getframerate()
        assert rate==44100
        pcm=array.array('h',wav.readframes(wav.getnframes()))
    if sys.byteorder!='little': pcm.byteswap()
    left=pcm[::2]; right=pcm[1::2]
    assert len(left)>rate
    # Choose a full second of the strongest signal, excluding later silence.
    start=max(range(0,len(left)-rate,rate//4),key=lambda i:sum(x*x for x in left[i:i+rate]))
    tone=left[start:start+rate]
    rms=math.sqrt(sum(x*x for x in tone)/rate)
    right_rms=math.sqrt(sum(x*x for x in right[start:start+rate])/rate)
    frequency=sum(tone[i-1]<0<=tone[i] for i in range(1,len(tone)))
    crossings=sum(left[i-1]<0<=left[i] for i in range(1,len(left)))
    assert rms>500 and right_rms>500,(rms,right_rms)
    assert 434<=frequency<=446,frequency
    assert 1700<=crossings<=1850,crossings
    result={'rate':rate,'channels':2,'rms_left':round(rms,2),
            'rms_right':round(right_rms,2),'tone_hz':frequency,
            'tone_crossings':crossings,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    print('PASS AC97 WAV: '+json.dumps(result),flush=True)
    return result


if __name__=='__main__':
    path=Path(sys.argv[1]) if len(sys.argv)>1 else Path('build/out/native-audio.wav')
    result=check(path)
    path.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')

# Description

MSharpen is a sharpener that tries to sharpen only edges.

This plugin is [a port of the VapourSynth plugin MSharpen](https://github.com/dubhater/vapoursynth-msmoosh).

# Usage

```
MSharpen (clip, float "threshold", float "strength", bool "mask", bool "luma", bool "chroma", bool "highq")
```

## Parameters:

- clip\
    Clip to process. Can have variable format and dimensions. Frames with float sample type or with bit depth greater than 16 will be returned untouched.
    
- threshold\
    Sensitivity of the edge detection. Decrease if important edges are getting blurred. This parameter became a percentage in order to make it independent of the bit depth.\
    Default: 6.0.
            
- strength\
    Strength of the sharpening. This parameter became a percentage in order to make it independent of the bit depth.\
    Default: 39.0.
    
- mask\
    If True, the edge mask will be returned instead of the filtered frames.\
    Default: false.
    
- luma, chroma\
    Planes to process.\
    When mask=True, the untouched planes will contain garbage.\
    Default: luma = true; chroma = false

- highq\
    It's a dummy parameter for backward compatibility.

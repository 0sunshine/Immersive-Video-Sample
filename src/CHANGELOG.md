# **Changelog**

---
## [1.12.0] - 2023-06-28
**Features & bug fix:**
- 360SCVP (Stream Concatenation and Viewport Processing) Library
   + Support HEVC bitstream stitching for video with non 64-aligned resolution.

- FFmpeg Tool & Encoder Library
   + Encoder Library: Optimize scaling process to improve performance.
   + Encoder Library: Support HEVC 10bit encoding on SG1 with P010 and V210 sources.
   + Encoder Library: Support HEVC encoding for video with non 64-aligned resolution on SG1.
   + FFmpeg Tool:     Add 1:N multi-thread transcoding sample based on ffmpeg API and encoder library.

---
## [1.10.0] - 2022-06-27
**Features & bug fix:**
- OMAF Packing Library
   + Support DASH MPD writer plugin mechanism
   + Support DASH packing for multi-view streams
   + Support Chunk Location Box in customized CMAF segment writer plugin according to configuration
   + Fix incorrect segment duration setting for float type frame rate
   + Fix unsecure file handling

- OMAF Dash Access Library
   + Support MPD parsing, view selection, segment downloading/parsing, and parsed packets synchronization for multi-view streaming.
   + Support client simulator tool to trace the downloading and do the analysis.
   + Support Chunk Location Box downloading and parsing, chunk downloading according to Chunk Location Box parsing result.
   + Support CMAF chunk info type parsing according to segment
   + Fix memory leak, null-ptr, un-initialization issues

- 360SCVP (Stream Concatenation and Viewport Processing) Library
   + Support NovelViewSEI generation and parsing
   + Support rotation conversion
   + Support novel view info parsing from XML file
   + Fix incorrect HEVC short_term_ref_pic_set parsing and multiple AUD NAL units parsing
   + Fix uninitialized variable issue and pointer check issue

- Reference OMAF Player
   + Support multi-view source mode, auto view selector, multi-view decoding for multi-view source rendering
   + Fix Android build issues
   + Fix memory leak, un-initialization and double free issues.

- FFmpeg Plugins & Encoder Library
   + Encoder Library: Support single reference frame for B frame in hardware accelerated encoding
   + Encoder Library: Support forced IDR frame setting in hardware accelerated encoding
   + Encoder Library: Support GPU copy in hardware accelerated encoding
   + Encoder Library: Fix encode & decode frame sync issue
   + FFmpeg Plugins:  Add option for CMAF chunk info type setting in DASH packing
   + FFmpeg Tool:     Add DASH packing sample application based on DASH packing muxer

- Deployment
   + Support two docker containers built using docker build kit, one providing build environment and access for more operation, and the other only containing the must-have files and dependencies.
   + Support non-root for security benefit in runtime deployment
   + Support high and static UID to avoid host conflict

---
## [1.8.0] - 2021-12-7
**Features & bug fix:**
- OMAF Packing Library
   + Support DASH segment writer plugin mechanism
   + Support customized CMAF compliant segment writer plugin to generate CMAF segments
   + Support CMAF compliance in DASH MPD file
   + Support CMAF chunk duration correction to align with video GOP size

- OMAF Dash Access Library
   + Support ISOBMFF segment index box parsing and CMAF chunk downloading and parsing
   + Support CMAF compliant DASH MPD file parsing
   + Support E2E latency tracking

- 360SCVP (Stream Concatenation and Viewport Processing) Library
   + Support HEVC bitstream parsing for multiple short-term reference picture sets and reference picture lists modification

- Reference OMAF Player
   + Android Player: Add different framerate rendering and CMAF support
   + Linux Player: Add CMAF support
   + Linux Player: Support segment-duration adaptive time-out threshold setting
   + bug fix: memory leak in android player

- FFmpeg Plugins & Encoder Library
   + Encoder Library: Support hardware accelerated encoding
   + Encoder Library: Extend config file for hardware accelerated encoding
   + Encoder Library: Fix incorrect frame list issue, bitrate overflow issue and asyn depth wrong value issue
   + Encoder Library: Add fake coded frame checking log for hardware accelerated encoding
   + FFmpeg Plugins: Add option for CMAF segment generation
   + FFmpeg Plugins: Support 8K resolution SDI I/O input

- Live360SDK Library
   + Support video bitstream re-writing, FOV selection etc. functions for the edge
   + Update WebRTC 360 sample with the library
   + Upgrade WebRTC 360 sample to OWT5.0
   + Support dynamic input config parameters in WebRTC 360 sample

---
## [1.6.0] - 2021-6-4
**Features & bug fix:**
- OMAF Packing Library
   + Support GOP size output in DASH MPD file

- OMAF Dash Access Library
   + Support motion halting possibility output with viewport prediction
   + Support in-time viewport update strategy to reduce M2HQ latency
   + bug fix : incorrect pts, framerate calculation

- 360SCVP (Stream Concatenation and Viewport Processing) Library
   + Support HEVC B frame in bitstream parsing and stitching
   + Optimize viewport related tiles selectin for both equirectangular and cube-map projections
   + Support webrtc under cube-map projection
   + Optimize memory copy in GenerateSliceHdr

- Reference OMAF Player
   + Android Player: Support in-time viewport update strategy to reduce M2HQ latency
   + Android Player: Support user input configuration
   + Linux Player: Support in-time viewport update strategy to reduce M2HQ latency
   + Linux Player: Support performance data log and frame sequences log to trace M2HQ latency

- FFmpeg Plugins & Encoder Library
   + Encoder Library:add HEVC B frame support and optimize config file parsing
   + FFmpeg Plugins: add option for HEVC B frame support

---
## [1.4.0] - 2021-1-14
**Features & bug fix:**
- OMAF Packing Library
   + Support packing for cube-map projection in extractor track mode
   + Support both fixed and dynamic sub-picture resolutions in extractor track mode
   + Support packing for AAC audio stream
   + Support packing for planar projection in late-binding mode
   + Plugin mode to support customized media stream process
   + Support external log callback
   + bug fix: memory leak, hang / crash in some condition

- OMAF Dash Access Library
   + Support cube-map projection in extractor track mode
   + Support maximum decodable picture width and height limitation in late-binding mode
   + Support DASH de-packing for AAC audio stream segments
   + Support planar projection in late-binding mode
   + bug fix: memory leak, time out, tiles stitching disorder in some condition

- 360SCVP (Stream Concatenation and Viewport Processing) Library
   + code refactor: add plugin definition for tile selection
   + optimization for tile selection to improve performance, accuracy and efficiency
   + Support external log callback

- Reference OMAF Player
   + Android Player: with ERP and Cube-map support
   + Android platform: extend DashAccess JNI library with MediaCodec decoder integrated.
   + Linux Player: Support WebRTC source with multiple video stream decoding, rendering; and RTCP FOV feedback
   + Linux Player: Support Planar Video
   + Code refactor

- FFmpeg Plugins & Encoder Library
   + Encoder Library: Bug fix for memory leak
   + FFmpeg Plugins: add option for external log callback and log level set
   + FFmpeg Plugins: add option for fixed/dynamic sub-picture resolution for extractor track mode
   + FFmpeg Plugins: add audio stream input process
   + FFmpeg Plugins: add option for planar projection support
   + FFmpeg Plugins: add option for customized packing plugin and media stream process plugin set

---
## [1.2.0] - 2020-8-14
**Features & bug fix:**
- OMAF Packing Library
   + Support late-binding mode, option for extractor track generation
   + Support packing for cube-map projection in late-binding mode
   + Optimize tile partition for extractor generation
   + Plugin mode to support customized packing method
   + bug fix: memory leak, hang / crash in some condition

- OMAF Dash Access Library
   + Support late-binding mode: tile-selection and bit-stream rewriting in client side
   + Support cube-map projection in late-binding mode
   + Enable NDK cross-compiling, Add JNI support for Dash Access API
   + Optimization of downloading and segmentation parsing for fault-tolerance and stability
   + Plugin mode to support customized FOV sequence operation

- 360SCVP (Stream Concatenation and Viewport Processing) Library
   + Support Cube-map projection: tile processing and viewport processing
   + optimization for tile selection to improve performance and accuracy

- Reference OMAF Player
   + Support Cube-map projection
   + Support multiple video streams decoding and tile rendering
   + Code refactor

- FFmpeg Plugins & Encoder Library
   + Encoder Library: Enable local session support to improve performance
   + Encoder Library: Bug fix for memory leak, resource release, share memory usage, call mechanism, etc.
   + FFmpeg Plugins: add option for Cube-map projection support
   + FFmpeg Plugins: add option for late-binding support

---
## [1.0.0] - 2020-01-13   
**Features:** 
- OMAF Packing Library
   + Generate OMAF compliant DASH MPD and tiled-based mp4 segmentation.
   + Support OMAF viewport-dependent baseline presentation profile and HEVC-based viewport-dependent OMAF video profile.
   + Support viewport-dependent extractor track generation and relative meta data generation
   + Support tiled video processing with multi-resolution content
   + Support modes: VOD and Live Streaming

-  OMAF Dash Access Library
   + Support OMAF-Compliant MPD file parser; 
   + Support Tile-based MP4 segmentation (media track and extractor track) downloading and parsing;
   + Support Viewport-dependent extractor track selection and reading;
   + Support OMAF-compliant metadata parsing;
   + Support HTTPS/HTTP;

-  360SCVP (Stream Concatenation and Viewport Processing) Library
   + Provide a unify interface to process tile-based HEVC bitstream processing and viewport-based Content Processing;
   + Support HEVC bitstream processing: VPS/SPS/PPS parsing and generating, 360Video-relative SEI Generating and parsing, HEVC tile-based bitstream aggregation;
   + Support Viewport generation, viewport-based tile selection and extractor selection based on content coverage;

-  Reference OMAF Player
   + Support Linux platform with OpenGL rendering
   + Support OMAF-Compliant Dash source and WebRTC-based tile source;
   + Support ERP-based 360Video Rendering;
   + Support Tile-based 360Video Rendering;
   + Support Intel Gen-GPU Hardware acceleration with GPU is presented in the system; 

-  FFMPEG Plugins
   + Demux plugin with OMAF Dash accessing support;
   + Multiplexing plugin with OMAF Packing library support;
   + SVT encoder support

**Know Issues:**
-  Audio segmentation hasn't been support yet;
---

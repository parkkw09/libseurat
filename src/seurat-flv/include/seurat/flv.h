/* =============================================================================
 * seurat-flv — FLV muxer for RTMP publishing (public C API).
 *
 * Scope:
 *   - Write-only (publisher). No demuxing.
 *   - In-memory streaming: every produced byte is handed to a user-supplied
 *     writer callback. The muxer itself never touches files or sockets.
 *   - Video: H.264 only, delivered as AVCC (length-prefixed NALUs).
 *   - Audio: AAC-LC only, delivered as raw AAC frames (no ADTS wrapper).
 *
 * References:
 *   - Adobe FLV File Format Specification v10.1 (2010).
 *   - ISO/IEC 14496-15 (AVCC / AVCDecoderConfigurationRecord).
 *   - ISO/IEC 14496-3 §1.6 (AudioSpecificConfig).
 *
 * Threading:
 *   - Single-threaded. A seurat_flv_muxer_t must be driven from one thread at
 *     a time. The writer callback is invoked synchronously on the caller's
 *     thread; do not share muxers across threads without external locking.
 *
 * Error model:
 *   - Functions return 0 on success, negative on error (see SEURAT_FLV_E_*).
 *   - On error the muxer state is left valid but the partial tag is not
 *     emitted to the writer.
 *
 * License: MIT.
 * ============================================================================= */

#ifndef SEURAT_FLV_H_
#define SEURAT_FLV_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- errors -- */

#define SEURAT_FLV_OK                 0
#define SEURAT_FLV_E_INVALID_ARG     -1
#define SEURAT_FLV_E_WRITER          -2   /* writer callback indicated failure (reserved) */
#define SEURAT_FLV_E_STATE           -3   /* call order violation                        */
#define SEURAT_FLV_E_OVERFLOW        -4   /* input larger than FLV 24-bit size field     */
#define SEURAT_FLV_E_BUFFER          -5   /* caller-supplied buffer too small            */
#define SEURAT_FLV_E_UNSUPPORTED     -6   /* unsupported codec parameter                 */

/* ------------------------------------------------------------ callbacks -- */

/* Writer callback. Receives muxed bytes in order. Return 0 on success, any
 * non-zero value to abort the current FLV call (propagated as _E_WRITER).
 *
 * The callback may be invoked multiple times per seurat_flv_write_* call.
 */
typedef int (*seurat_flv_writer_fn)(void* user,
                                     const uint8_t* data,
                                     size_t len);

/* -------------------------------------------------------------- config -- */

typedef struct {
    /* Video. Zeros are legal (video-less stream when has_video=0 at header
     * time). Used for the onMetaData script tag only. */
    int     video_width;
    int     video_height;
    double  video_framerate;        /* fps, e.g. 30.0 / 60.0 */
    int     video_bitrate_bps;      /* 0 = unknown */

    /* Audio. Used for the onMetaData script tag only. */
    int     audio_sample_rate;      /* 44100 or 48000 */
    int     audio_channels;         /* 1 or 2 */
    int     audio_sample_size_bits; /* 8 or 16 */
    int     audio_bitrate_bps;      /* 0 = unknown */
} seurat_flv_config_t;

/* --------------------------------------------------------------- types -- */

typedef struct seurat_flv_muxer seurat_flv_muxer_t;

/* ------------------------------------------------------------- lifecycle -- */

/* Create a new muxer. Returns NULL on allocation failure. `writer` must be
 * non-NULL; `user` is passed verbatim to each callback invocation.
 *
 * The muxer stores a copy of `cfg`; the caller may release it after the call.
 */
seurat_flv_muxer_t* seurat_flv_create(const seurat_flv_config_t* cfg,
                                       seurat_flv_writer_fn writer,
                                       void* user);

/* Destroy a muxer. `m` may be NULL. */
void seurat_flv_destroy(seurat_flv_muxer_t* m);

/* ---------------------------------------------------------------- header -- */

/* Write the 9-byte FLV file header plus the initial PreviousTagSize=0.
 *
 * Must be called exactly once, before any tag writer.
 *
 *   has_video / has_audio: sets the TypeFlags bits in the FLV header. It is
 *   legal to declare audio-only or video-only streams.
 */
int seurat_flv_write_header(seurat_flv_muxer_t* m,
                             int has_video,
                             int has_audio);

/* ------------------------------------------------------------ onMetaData -- */

/* Write the onMetaData script tag built from the config passed at create time.
 * Recommended to be the first tag after the FLV header; required by most
 * RTMP servers for proper stream discovery. */
int seurat_flv_write_metadata(seurat_flv_muxer_t* m);

/* --------------------------------------------------------------- video -- */

/* Write the H.264 AVCDecoderConfigurationRecord (aka "AVCC sequence header").
 *
 * sps / pps are raw NAL units (NO start codes, NO length prefix, NO Annex-B).
 * Typically this is the first video tag and uses timestamp_ms=0.
 *
 * Must be called once before any seurat_flv_write_video_frame().
 */
int seurat_flv_write_video_sequence(seurat_flv_muxer_t* m,
                                      uint32_t       timestamp_ms,
                                      const uint8_t* sps, size_t sps_len,
                                      const uint8_t* pps, size_t pps_len);

/* Write an H.264 video frame.
 *
 *   avcc_nalus is a concatenation of one or more NAL units, each prefixed by
 *   a 4-byte big-endian length (AVCC framing). Use seurat_flv_annexb_to_avcc()
 *   to convert from Annex-B (start-code framed) encoder output.
 *
 *   is_keyframe is non-zero for IDR frames.
 *
 *   composition_time_ms = PTS - DTS (ms). 0 when the encoder emits no B-frames
 *   (recommended for low-latency publish). Must be representable in a signed
 *   24-bit integer.
 */
int seurat_flv_write_video_frame(seurat_flv_muxer_t* m,
                                   uint32_t       timestamp_ms,
                                   int            is_keyframe,
                                   int32_t        composition_time_ms,
                                   const uint8_t* avcc_nalus,
                                   size_t         avcc_nalus_len);

/* Write the end-of-sequence video tag (AVCPacketType=2). Optional; send
 * immediately before shutting the stream down. */
int seurat_flv_write_video_end(seurat_flv_muxer_t* m, uint32_t timestamp_ms);

/* ---------------------------------------------------------------- audio -- */

/* Write the AAC AudioSpecificConfig (the "AAC sequence header").
 *
 *   asc is the raw ASC payload (typically 2 bytes for AAC-LC). Build it from
 *   the encoder's output (AudioToolbox, MediaCodec, MFT) or hand-roll:
 *     ASC[0] = (AOT<<3) | (freq_idx>>1)
 *     ASC[1] = ((freq_idx&1)<<7) | (channels<<3)
 *
 * Must be called once before any seurat_flv_write_audio_frame().
 */
int seurat_flv_write_audio_sequence(seurat_flv_muxer_t* m,
                                      uint32_t       timestamp_ms,
                                      const uint8_t* asc,
                                      size_t         asc_len);

/* Write a raw AAC frame (no ADTS header). If the encoder emits ADTS-framed
 * data, strip the 7/9-byte ADTS header before calling.
 */
int seurat_flv_write_audio_frame(seurat_flv_muxer_t* m,
                                   uint32_t       timestamp_ms,
                                   const uint8_t* aac_raw,
                                   size_t         aac_raw_len);

/* --------------------------------------------------------------- helpers -- */

/* Convert an Annex-B byte stream (NALUs separated by 0x000001 or 0x00000001
 * start codes) into the AVCC framing that seurat_flv_write_video_frame()
 * expects (each NAL prefixed with 4-byte big-endian length).
 *
 *   out_cap: capacity of out_avcc.
 *   *written: on success, bytes actually written into out_avcc.
 *
 * Safe to call with in_len == 0 (writes nothing, *written = 0).
 *
 * Returns SEURAT_FLV_E_BUFFER if out_cap is too small; on that return the
 * function may have written partial data, so callers should ignore out_avcc.
 */
int seurat_flv_annexb_to_avcc(const uint8_t* annexb,
                               size_t         in_len,
                               uint8_t*       out_avcc,
                               size_t         out_cap,
                               size_t*        written);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* SEURAT_FLV_H_ */

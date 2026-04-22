/* =============================================================================
 * seurat-rtmp — RTMP / RTMPS publish client (public C API).
 *
 * Scope:
 *   - Publish-only. No subscribing, no relay, no server role.
 *   - Targets mainstream live platforms (YouTube Live, Twitch, SOOP, etc.)
 *     that speak RTMP 1.0 with Adobe-style AMF0 commands.
 *   - Delivers H.264 video + AAC audio via FLV-framed payloads. The framing
 *     is produced by seurat-flv and fed into the RTMP chunk stream.
 *
 * RTMP vs RTMPS:
 *   - RTMP  (TCP on port 1935).
 *   - RTMPS (TLS-over-TCP, port 443 in practice). Requires building against
 *     OpenSSL (LEONARDO_CRYPTO=ON at project configure time).
 *
 * Threading:
 *   - Synchronous / single-threaded. All `_run*` style methods block the
 *     caller until the operation completes or errors out. If you need an
 *     async wrapper, build it on top.
 *
 * Memory:
 *   - All strings passed in are copied internally at create() time.
 *   - The caller owns the bytes passed to `publish_video_*` / `publish_audio_*`
 *     only for the duration of the call; the implementation serialises them
 *     synchronously.
 *
 * License: MIT.
 * ============================================================================= */

#ifndef SEURAT_RTMP_H_
#define SEURAT_RTMP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- errors -- */

#define SEURAT_RTMP_OK                   0
#define SEURAT_RTMP_E_INVALID_ARG       -1
#define SEURAT_RTMP_E_STATE             -2   /* call-order violation              */
#define SEURAT_RTMP_E_ALLOC             -3
#define SEURAT_RTMP_E_NETWORK           -4   /* socket / TLS I/O failure          */
#define SEURAT_RTMP_E_PROTOCOL          -5   /* peer violated RTMP spec           */
#define SEURAT_RTMP_E_REJECTED          -6   /* server returned `onStatus` error   */
#define SEURAT_RTMP_E_TIMEOUT           -7
#define SEURAT_RTMP_E_UNSUPPORTED       -8   /* feature not yet implemented       */
#define SEURAT_RTMP_E_TLS               -9   /* RTMPS requested but not built in  */
#define SEURAT_RTMP_E_URL               -10  /* cannot parse rtmp(s):// URL       */

/* ------------------------------------------------------------- lifecycle -- */

typedef struct seurat_rtmp_client seurat_rtmp_client_t;

typedef struct {
    /* Publish target. Examples:
     *   rtmp://a.rtmp.youtube.com/live2/<stream-key>
     *   rtmps://a.rtmps.youtube.com:443/live2/<stream-key>
     *   rtmp://live.twitch.tv/app/<stream-key>
     *
     * The parser splits it into host, port, app, stream_key. If the URL
     * contains a stream key as the final path component (most platforms),
     * the caller may leave `stream_key` NULL and it will be auto-detected.
     */
    const char* url;

    /* Optional override. If non-NULL it wins over any path-derived key. */
    const char* stream_key;

    /* Optional RTMP client identity. Passed in the AMF0 `connect` command's
     * flashVer property. Default = "libseurat/0.2 (FMLE-compatible)". */
    const char* flash_ver;

    /* Optional override for the RTMP `connect` `tcUrl` property. Default is
     * derived from `url` (scheme://host[:port]/app). */
    const char* tc_url;

    /* Connect / send / recv timeouts in ms. 0 = implementation default. */
    uint32_t    connect_timeout_ms;
    uint32_t    io_timeout_ms;
} seurat_rtmp_config_t;

seurat_rtmp_client_t* seurat_rtmp_create(const seurat_rtmp_config_t* cfg);
void                  seurat_rtmp_destroy(seurat_rtmp_client_t* c);

/* Human-readable error description for logging. Returned pointer is valid
 * for the lifetime of the process. */
const char* seurat_rtmp_strerror(int code);

/* ------------------------------------------------------------- connection -- */

/* Dial the server, complete the RTMP handshake, and run the publish
 * negotiation sequence:
 *
 *     connect → releaseStream → FCPublish → createStream → publish
 *
 * After this returns SEURAT_RTMP_OK the client is ready to accept AV payload
 * via `publish_video_*` / `publish_audio_*`. */
int seurat_rtmp_connect(seurat_rtmp_client_t* c);

/* Send `FCUnpublish` + `deleteStream` + flush, then tear down the connection
 * gracefully. Always safe to call (no-op if not connected). */
int seurat_rtmp_disconnect(seurat_rtmp_client_t* c);

/* ------------------------------------------------------------- metadata -- */

/* Send an onMetaData script tag describing the stream. The RTMP chunk wraps
 * the FLV script tag produced by seurat-flv. Most servers require this to
 * appear before the first audio/video payload. */
int seurat_rtmp_send_metadata(seurat_rtmp_client_t* c,
                               int      video_width,
                               int      video_height,
                               double   video_framerate,
                               int      video_bitrate_bps,
                               int      audio_sample_rate,
                               int      audio_channels,
                               int      audio_bitrate_bps);

/* ---------------------------------------------------------------- video -- */

/* Send the H.264 AVCDecoderConfigurationRecord (built from SPS+PPS).
 *
 * sps / pps are raw NAL units (no start codes, no length prefix).
 * Call exactly once, before the first seurat_rtmp_send_video_frame(). */
int seurat_rtmp_send_video_sequence(seurat_rtmp_client_t* c,
                                      uint32_t       timestamp_ms,
                                      const uint8_t* sps, size_t sps_len,
                                      const uint8_t* pps, size_t pps_len);

/* Send an H.264 frame. `avcc_nalus` is AVCC framing (each NAL prefixed by a
 * 4-byte big-endian length). Use seurat_flv_annexb_to_avcc() to convert
 * Annex-B encoder output.
 *
 * is_keyframe: non-zero for IDR.
 * composition_time_ms: PTS - DTS (0 when no B-frames; recommended). */
int seurat_rtmp_send_video_frame(seurat_rtmp_client_t* c,
                                   uint32_t       timestamp_ms,
                                   int            is_keyframe,
                                   int32_t        composition_time_ms,
                                   const uint8_t* avcc_nalus,
                                   size_t         avcc_nalus_len);

/* ---------------------------------------------------------------- audio -- */

/* Send the AAC AudioSpecificConfig. Call once before the first audio frame. */
int seurat_rtmp_send_audio_sequence(seurat_rtmp_client_t* c,
                                      uint32_t       timestamp_ms,
                                      const uint8_t* asc, size_t asc_len);

/* Send a raw AAC frame (no ADTS header). */
int seurat_rtmp_send_audio_frame(seurat_rtmp_client_t* c,
                                   uint32_t       timestamp_ms,
                                   const uint8_t* aac_raw, size_t aac_raw_len);

/* ------------------------------------------------------------ statistics -- */

typedef struct {
    uint64_t bytes_sent_total;   /* cumulative TCP/TLS-level bytes out */
    uint64_t bytes_recv_total;
    uint64_t rtmp_messages_sent;
    uint64_t video_frames_sent;
    uint64_t audio_frames_sent;
} seurat_rtmp_stats_t;

int seurat_rtmp_get_stats(seurat_rtmp_client_t* c, seurat_rtmp_stats_t* out);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* SEURAT_RTMP_H_ */

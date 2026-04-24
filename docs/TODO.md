# libseurat — 할 일 목록

최종 갱신: 2026-04-23

> 미완료 작업만 기록합니다.
> - 정적 정보 → `OVERVIEW.md`
> - 완료 내역 → `PROGRESS.md`

우선순위는 **P0(즉시) → P1(단기) → P2(중기) → P3(장기)** 로 표기합니다.

---

## 1. 잔여 이슈 (Open Issues)

### 1.1 [P1] `build-rtc.cmake` / `src/webrtc` 정리

- **문제**: `cmake/builder/build-rtc.cmake`가 비어 있는 `src/webrtc/`를 참조. `SEURAT_RTC`
  기본값이 `OFF`라 즉시 영향은 없지만, `OVERVIEW.md §1.2`에서 **WebRTC/WHIP은 현재 범위 밖**으로
  확정되어 잔존 파일이 오탐(false positive)의 원인이 됩니다.
- **조치**: `build-rtc.cmake` + `src/webrtc/` 삭제 + `CMakeLists.txt`의 `SEURAT_RTC` 옵션 제거.
- **재검토 시점**: 향후 WebRTC 필요 시 **WHIP over HTTP + libdatachannel (MIT)** 조합으로 별도 라이브러리화.

### 1.2 [P1] Android / iOS 회귀 빌드 및 산출물 유효성 검증

- **현황**: ExternalProject 단계 통과까지는 확인 (`PROGRESS.md §5`). 내부 컴포넌트
  (`seurat-flv` / `seurat-rtmp`) 및 최종 `.aar` / `.xcframework` 산출물의 **무결성 검증**이 필요합니다.
- **조치**:
  - Android: `seurat.aar`를 실제 Android Studio 샘플 앱에 import → ABI별 정적 아카이브 + 헤더 노출 검증.
  - iOS: `Libseurat.xcframework`를 device + simulator 양쪽으로 실제 Xcode 프로젝트에 링크 + 빌드 검증.
  - RTMP E2E: RTMP 서버(MediaMTX 또는 local SRS)로 `seurat_rtmp_publish` 송출 테스트.

### 1.3 (완료되어 PROGRESS.md로 이동됨)
- H.264 인코더 전략 전환 (플랫폼 네이티브 우선) 작업은 2026-04-23 부로 완료되었습니다.

### 1.4 [P3] MSVC 패키징 단계 미구현

- **파일**: `build.sh:118-120`

  ```bash
  MSVC)
      echo "    (MSVC packaging step not yet implemented)"
      ;;
  ```

- **현황**:
  - MSVC toolchain 파일은 최소 골격만 존재 (`toolchain-msvc.cmake`, 33 LOC).
  - OpenSSL 빌드 시 Perl + NASM 필요 (code 경고 추가됨: `build-openssl.cmake:98`).
  - 현재 우선순위 플랫폼은 모바일(Android/iOS) + macOS이므로 후순위.
- **조치**: Windows 수요 확정 시 별도 `scripts/package-msvc.{sh,ps1}` 신설.

### 1.5 [정보/관찰] 빌드 로그상 OpenSSL Perl 경고

OSX 빌드 로그에서 OpenSSL 3.3.2 configure/generate 단계에 다음 경고가 반복됩니다.

```
Use of uninitialized value in join or string at
  /System/Library/Perl/5.34/darwin-thread-multi-2level/re.pm line 47.
```

- OpenSSL 3.3.x configure의 Perl 의존성에서 발생하는 **상류(업스트림) warning**.
- 빌드 결과물에 영향 없음. OpenSSL 3.4 이후 릴리스에서 개선 예정.
- **조치**: 현재는 무시. 추적만.

---

## 2. 단계별 작업 목록

### Phase 3 — 송출 스택 완성 (진행 중)

| 작업 | 우선순위 | 현황 / 메모 |
|---|---|---|
| `seurat-aac-native` (플랫폼별 AAC wrapper) | ✅ 완료 | iOS/macOS AudioToolbox, Android MediaCodec 구현 완료. Windows(MFT)만 대기 중. |
| `seurat-h264-native` (플랫폼별 H.264 HW 인코더) | ✅ 완료 | iOS/macOS VideoToolbox, Android MediaCodec 구현 완료. Windows(MFT)만 대기 중. |
| `seurat-mpegts` 통합 | P2 | 옵션 A [libmpegts(kierank), ISC] 통합 **권장** vs 옵션 B 자체 구현 (~1500 LOC). `seurat-aac-native` 선행 필요. |

### Phase 4 — 통합 및 샘플

| 작업 | 우선순위 | 메모 |
|---|---|---|
| 공개 API 정리 (`seurat.h` 통합) | P2 | 현재 컴포넌트별 헤더 (`seurat/flv.h`, `seurat/rtmp.h`). 최상위 umbrella 헤더 필요. |
| 샘플 앱 (OSX / iOS / Android) | P2 | RTMP/RTMPS 송출 엔드투엔드 데모. |
| README / API 가이드 | P3 | 본 docs/* 체계를 프로젝트 루트 README에 링크. |

### Phase 5 — 릴리스 엔지니어링

| 작업 | 우선순위 | 메모 |
|---|---|---|
| H.264 네이티브 인코더 전환 (기본 경로) | ✅ 완료 | `seurat-h264-native` 채택 및 `SEURAT_OPENH264=OFF` 반영 완료. |
| OpenH264 프리빌트 동적 로드 경로 (선택적 SW 폴백) | P3 | §1.3 참조. 네이티브 경로 정착 후, SW 폴백 필요 시점에 착수. |
| Windows / MSVC 검증 | P3 | §1.4 참조. |
| AAR prefab 메타데이터 (`prefab/*.json`) | P3 | Gradle `prefab` feature 지원. |
| E2E 테스트 (YouTube / Twitch / SOOP 실서버) | P2 | RTMPS 인증서 검증(Phase E) 활성화 후 정식 검증. |

### Phase E — RTMPS 보안 강화 (인증서 검증)

**목적**: Phase D의 `SSL_VERIFY_NONE` 1차 구현을 프로덕션 수준으로 상향.
**근거**: MITM 공격으로 스트림 키 탈취 가능 → 현재 상태는 상용 배포 금지.
(현재 구현 상세는 `PROGRESS.md §4.2`, 관련 코드 `src/seurat-rtmp/src/rtmp_tls.cpp:88-90`)

| # | 작업 | 우선순위 | 상태 | 메모 |
|---|---|---|---|---|
| E.1 | `seurat_rtmp_config_t` 확장 | P1 | ✅ 2026-04-24 | `ca_bundle_pem`(in-memory PEM, NULL 가능), `tls_insecure`(개발용 우회, 기본 0) 필드 추가. |
| E.2 | `X509_VERIFY_PARAM_set1_host` 호스트명 검증 | P1 | ✅ 2026-04-24 | MITM 방어 활성화. `SSL_get0_param()` + `X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS`. CA 미확보 시 `SEURAT_RTMP_E_TLS`. |
| E.3 | 플랫폼 CA 번들 자동 추출 | P1 | ✅ 2026-04-24 (macOS/Android) / 🟡 (iOS/Windows 후속) | 신규 API `seurat_rtmp_platform_ca_bundle_pem()`. **macOS**: `SecTrustCopyAnchorCertificates` (검증: 156 CA / 235 KB). **Android**: cacerts 디렉터리 스캔. **iOS/Windows**: 네이티브 앵커 API 부재 → 사용자 번들 제공 또는 임베드 Mozilla CA (후속 작업). |
| E.4 | 실서버 인증서 체인 E2E | P2 | ⏳ | YouTube / Twitch / 치지직 / SOOP 4종 RTMPS 엔드포인트 연결 + publish 성공. TLS 1.2 / 1.3 모두 재개. |
| E.5 | iOS/Windows용 임베드 Mozilla CA 번들 (옵션) | P2 | ⏳ | `SEURAT_RTMP_EMBED_CA` CMake 옵션. curl.se `cacert.pem` vendored. `seurat_rtmp_platform_ca_bundle_pem()`이 iOS/Windows에서 임베드 번들을 대신 반환. ~200 KB 바이너리 증가. |

**진입 순서**: E.1 → E.2 → E.3 → E.4. E.1+E.2+E.3 완료로 macOS/Android는 즉시 사용 가능. iOS 앱은 자체 번들 주입(E.5 전까지) 필요.

### Phase F — RTMP 안정성 / 관찰성

**목적**: 장시간 라이브 방송 중 서버 호환성 및 네트워크 이상 상황(Wi-Fi 전환, 기지국 핸드오버, 서버 재시작)에 대한 내성 강화.
**근거**: 현재 구현은 RTMP 제어 메시지 응답과 재연결 로직이 없어 "OBS 대체"로 쓰기엔 부족. E2E 검증에서 드러날 가능성 높음.

| # | 작업 | 우선순위 | 메모 |
|---|---|---|---|
| F.1 | Window Acknowledgement 응답 | P1 | RTMP spec §5.4.3 / §5.4.4. 서버가 보낸 `WindowAckSize`(type 5)를 보관 → 누적 송신 바이트가 윈도우 초과 시마다 `Acknowledgement`(type 3) 송신. 현재 `rtmp_chunk.cpp::handle_control()`에서 수신만 하고 무시 중. 관대한 서버(YouTube)에선 문제 없지만 SRS·엄격 릴레이에서 연결 끊김 가능. |
| F.2 | User Control Message 응답 | P2 | `PingRequest`(event 6) → `PingResponse`(event 7) 송신. `StreamBegin/EOF/Dry/SetBufferLength` 수신 시 로깅. 주요 OTT는 publish 방향 ping을 안 보내지만 일부 릴레이 서버 호환성용. |
| F.3 | 네트워크 품질 지표 | P2 | `seurat_rtmp_stats_t` 확장: `ack_lag_bytes`(송신-확인 차), `last_rtt_ms`(자체 ping 왕복), `send_buffer_depth`(TCP 백프레셔), `tls_version` / `negotiated_cipher`(RTMPS 진단). OBS의 "congestion" / "dropped frames" 급 관찰성. |
| F.4 | 자동 재연결 유틸 | P2 | `seurat_rtmp_reconnect()` 공개 API + 지수 백오프 정책 필드(`retry_max_attempts`, `retry_base_ms`). 모바일 네트워크 전환 대응. 세션 상태(stream_key, 마지막 metadata, 마지막 seq headers)를 내부 보관 후 재사용 → 상위 앱이 일일이 재주입할 필요 없음. |
| F.5 | Enhanced RTMP 준비 (HEVC) | P3 | `seurat-flv` 확장 video tag(IsExHeader + FourCC `hvc1`), `seurat-rtmp` `connect` 명령의 `fourCcList` 파라미터. `seurat-h265-native` 선행. 별도 Phase로 빼도 무방. |

---

## 3. 컴포넌트별 작업 지침 (다른 에이전트용)

### 3.1 AAC Native wrapper 개발자 — ✅ 완료 (Windows 대기)

공통 C API 제안:

```c
typedef struct seurat_aac_encoder seurat_aac_encoder_t;

typedef struct {
    int sample_rate;    // 44100 or 48000
    int channels;       // 1 or 2
    int bitrate;        // bps, e.g. 128000
} seurat_aac_config_t;

seurat_aac_encoder_t* seurat_aac_create(const seurat_aac_config_t* cfg);
int  seurat_aac_encode(seurat_aac_encoder_t* enc,
                       const int16_t* pcm, int samples_per_channel,
                       uint8_t* adts_out, int out_capacity);
void seurat_aac_destroy(seurat_aac_encoder_t* enc);
```

플랫폼별 구현 파일:

- `src/aac/aac_audiotoolbox.m` — iOS/macOS
- `src/aac/aac_mediacodec.cc` — Android JNI
- `src/aac/aac_mediafoundation.cpp` — Windows

참고 문서: `OVERVIEW.md §7` (플랫폼 네이티브 AAC 문서 링크).

### 3.2 H.264 Native wrapper 개발자 — ✅ 완료 (Windows 대기)

§1.3 전환 결정에 따른 신규 트랙. `seurat-aac-native`와 **동일한 패턴**으로 설계 및 Android/Apple 플랫폼 구현이 완료되었습니다.

공통 C API 제안:

```c
typedef struct seurat_h264_encoder seurat_h264_encoder_t;

typedef struct {
    int      width, height;
    int      fps_num, fps_den;     // e.g. 60000 / 1001
    int      bitrate_bps;          // CBR
    int      gop_seconds;          // 2 권장 (YouTube Live)
    int      profile;              // 0=Main, 1=High
    int      allow_hw_only;        // 1이면 HW 인코더 미지원 시 즉시 실패
} seurat_h264_config_t;

seurat_h264_encoder_t* seurat_h264_create(const seurat_h264_config_t* cfg);
int  seurat_h264_encode(seurat_h264_encoder_t* enc,
                        const uint8_t* i420, int stride_y, int stride_uv,
                        int64_t pts_us,
                        uint8_t* nal_out, int out_capacity, int* is_keyframe);
void seurat_h264_force_keyframe(seurat_h264_encoder_t* enc);
void seurat_h264_destroy(seurat_h264_encoder_t* enc);
```

플랫폼별 구현 파일:

- `src/h264/h264_videotoolbox.m`      — iOS/macOS (`VTCompressionSession`)
- `src/h264/h264_mediacodec.cc`       — Android (NDK `AMediaCodec` 또는 JNI)
- `src/h264/h264_mediafoundation.cpp` — Windows (MFT H.264 Encoder)

참고 사항:

- 입력 색 포맷: 기본 I420. HW 인코더가 NV12를 요구하는 경우 `libyuv`로 변환(이미 `libyuv` 도입).
- 출력은 Annex-B NAL 스트림. AVCC 변환은 상위(`seurat-flv`) 책임.
- Android: 벤더 MediaCodec 품질 편차 존재. 화이트리스트/블랙리스트 운영 고려.
- `OVERVIEW.md §7` — 플랫폼 네이티브 문서 링크(현재는 AAC만 명시) + VideoToolbox / MediaCodec / MFT H.264 링크 추가 필요.

### 3.3 MPEG-TS 먹서 개발자 — ⏳ 대기

- **옵션 A (권장)**: [libmpegts (kierank)](https://github.com/kierank/libmpegts) 통합 — ISC 라이선스, OBE 프로젝트에서 실사용 검증.
- **옵션 B**: 자체 구현 — PAT/PMT/PES/PCR 타이밍 처리 필요 (~1500 LOC).
- **진입 조건**: `seurat-aac-native` 선행 필요 (오디오 경로가 성립해야 MPEG-TS 페이로드 구성 가능).

### 3.4 RTMP 인증서 검증 개발자 — ⏳ 대기 (Phase E)

1. `seurat_rtmp_config_t`에 CA 번들 및 검증 모드 필드 추가:
   ```c
   const char* ca_bundle_pem;   // optional, NULL이면 플랫폼 기본 CA 사용
   int         tls_insecure;    // 개발 전용, 기본 0
   ```
2. `rtmp_tls.cpp::TlsTransport`:
   - `SSL_CTX_load_verify_locations()` 또는 `SSL_CTX_set_default_verify_paths()`로 CA 주입.
   - `SSL_set_verify(ctx, SSL_VERIFY_PEER, ...)` + `X509_VERIFY_PARAM_set1_host()`.
   - `tls_insecure=1`인 경우에만 `SSL_VERIFY_NONE` 유지 (로그로 명시).
3. 플랫폼별 CA 번들 로딩 헬퍼 추가:
   - iOS/macOS — Security framework (`SecTrust*`) 또는 시스템 keychain 덤프.
   - Android — system CA store 추출 (`/system/etc/security/cacerts/`).
   - Windows — `CertOpenStore("ROOT")`.
4. E2E: YouTube/Twitch/치지직/SOOP 실 RTMPS 엔드포인트 4종 연결 성공.

---

## 4. 완료 조건 (Definition of Done)

- [x] Phase 3 네이티브 코덱 래퍼 구축 — `seurat-aac-native`, `seurat-h264-native` 1차(Android/Apple) 구현 및 빌드 파이프라인(단일 동적 라이브러리) 완성.
- [ ] Phase 3 MPEG-TS — `seurat-mpegts` 통합 완료.
- [ ] Phase 4 샘플 앱 3종(OSX/iOS/Android) 실기 송출 성공.
- [x] Phase 5 H.264 네이티브 인코더 기본 경로 정식 채택 (OpenH264 기본값 OFF 전환 완료).
- [x] Phase E.1~E.3 완료 — RTMPS 인증서 검증 기본 ON + 호스트명 검증 포함 (macOS/Android). iOS는 사용자 번들 주입 필요.
- [ ] Phase E.4 — 4개 메이저 플랫폼(YouTube/Twitch/치지직/SOOP) RTMPS E2E 통과.
- [ ] Phase E.5 — iOS/Windows용 임베드 Mozilla CA 번들 옵션.
- [ ] Phase F.1 — Window Acknowledgement 응답 구현 (장시간 방송 안정성).
- [ ] MSVC 빌드/패키징 성공 (단, 수요 확정 시점 이후).

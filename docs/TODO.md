# libseurat — 할 일 목록

최종 갱신: 2026-04-22

> 미완료 작업만 기록합니다.
> - 정적 정보 → `OVERVIEW.md`
> - 완료 내역 → `PROGRESS.md`

우선순위는 **P0(즉시) → P1(단기) → P2(중기) → P3(장기)** 로 표기합니다.

---

## 1. 잔여 이슈 (Open Issues)

### 1.1 [P1] `build-rtc.cmake` / `src/webrtc` 정리

- **문제**: `cmake/builder/build-rtc.cmake`가 비어 있는 `src/webrtc/`를 참조. `LEONARDO_RTC`
  기본값이 `OFF`라 즉시 영향은 없지만, `OVERVIEW.md §1.2`에서 **WebRTC/WHIP은 현재 범위 밖**으로
  확정되어 잔존 파일이 오탐(false positive)의 원인이 됩니다.
- **조치**: `build-rtc.cmake` + `src/webrtc/` 삭제 + `CMakeLists.txt`의 `LEONARDO_RTC` 옵션 제거.
- **재검토 시점**: 향후 WebRTC 필요 시 **WHIP over HTTP + libdatachannel (MIT)** 조합으로 별도 라이브러리화.

### 1.2 [P1] Android / iOS 회귀 빌드 및 산출물 유효성 검증

- **현황**: ExternalProject 단계 통과까지는 확인 (`PROGRESS.md §5`). 내부 컴포넌트
  (`seurat-flv` / `seurat-rtmp`) 및 최종 `.aar` / `.xcframework` 산출물의 **무결성 검증**이 필요합니다.
- **조치**:
  - Android: `seurat.aar`를 실제 Android Studio 샘플 앱에 import → ABI별 정적 아카이브 + 헤더 노출 검증.
  - iOS: `Libseurat.xcframework`를 device + simulator 양쪽으로 실제 Xcode 프로젝트에 링크 + 빌드 검증.
  - RTMP E2E: RTMP 서버(MediaMTX 또는 local SRS)로 `seurat_rtmp_publish` 송출 테스트.

### 1.3 [P1] H.264 인코더 전략 전환 — 플랫폼 네이티브 우선

- **배경**: 현재 `cmake/builder/build-open-h264.cmake`는 **OpenH264 소스 빌드 → 정적 링크** 경로.
  이 경로는 Cisco의 MPEG-LA 로열티 대납 혜택을 받지 못하므로 **상용 배포 불가** (`OVERVIEW.md §2.2`).
- **신규 방향** (2026-04-22 결정): **AAC와 동일한 패턴**으로 H.264도 플랫폼 네이티브 인코더를 기본 경로로 채택.
  - **기본 경로**: `seurat-h264-native` (신규 컴포넌트)
    - iOS/macOS — VideoToolbox (`VTCompressionSession`)
    - Android — MediaCodec (NDK `AMediaCodec` 또는 JNI)
    - Windows — Media Foundation H.264 Encoder (MFT)
    - 로열티는 OS 제조사가 대납, 시스템 프레임워크 동적 링크라 배포 번들 정적 링크 이슈 없음.
  - **선택적 폴백(opt-in)**: OpenH264를 **Cisco 공식 프리빌트 동적 라이브러리**(`.so`/`.dylib`/`.dll`)
    런타임 다운로드/로드 경로로 재작성. SW 인코딩이 필요한 특수 케이스(데스크탑 고품질, HW 인코더 결함 회피)에만 사용.
- **현상태 유지 원칙**: 전환 작업은 신규 트랙으로 병행 진행하고, **현재의 OpenH264 소스 빌드 구성은 그대로 유지**
  (내부 개발/테스트용). 상용 릴리스 파이프라인에 진입하기 전까지 릴리스 블로커 상태로 추적.
- **조치 (순서)**:
  1. `seurat-h264-native` 컴포넌트 신설 — 아래 §2 Phase 3, §3.2 참조.
  2. CMake 옵션 재편: `LEONARDO_H264_NATIVE=ON`(기본) / `LEONARDO_OPENH264=OFF`(opt-in).
  3. `OVERVIEW.md §1.1 / §4` 영상 인코딩 항목을 "플랫폼 네이티브 HW 인코더 (OpenH264는 선택적 SW 폴백)"로 갱신.
  4. (선택) `build-open-h264.cmake`를 Cisco 프리빌트 릴리스 아카이브 다운로드 + 런타임 로드 경로로 재작성.
- **릴리스 블로커**: 상용 배포 전까지 **(1)(2)(3) 필수**, (4)는 SW 폴백 필요 시점에 착수.

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
| `seurat-aac-native` (플랫폼별 AAC wrapper) | P1 | 아래 §3.1 API 제안 참조. iOS/macOS AudioToolbox, Android MediaCodec JNI, Windows Media Foundation 4종 필요. |
| `seurat-h264-native` (플랫폼별 H.264 HW 인코더 wrapper) | P1 | §1.3 전환 결정에 따른 신규 컴포넌트. §3.2 API 제안 참조. iOS/macOS VideoToolbox, Android MediaCodec, Windows Media Foundation 3종 필요. |
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
| H.264 네이티브 인코더 전환 (기본 경로) | P1 | §1.3 참조. `seurat-h264-native` 채택 + CMake 옵션 재편 + `OVERVIEW.md` 갱신. 상용 배포 필수 선행조건. |
| OpenH264 프리빌트 동적 로드 경로 (선택적 SW 폴백) | P3 | §1.3 참조. 네이티브 경로 정착 후, SW 폴백 필요 시점에 착수. |
| Windows / MSVC 검증 | P3 | §1.4 참조. |
| AAR prefab 메타데이터 (`prefab/*.json`) | P3 | Gradle `prefab` feature 지원. |
| E2E 테스트 (YouTube / Twitch / SOOP 실서버) | P2 | RTMPS 인증서 검증(Phase E) 활성화 후 정식 검증. |

### Phase E — 보안 강화 (RTMPS 인증서 검증)

**목적**: Phase D의 `SSL_VERIFY_NONE` 1차 구현을 프로덕션 수준으로 상향.
(현재 상태의 상세는 `PROGRESS.md §4.2`)

| 작업 | 우선순위 | 메모 |
|---|---|---|
| 플랫폼별 CA 번들 주입 | P1 | iOS Security framework / Android system keystore / Mozilla PEM 번들 중 택일. OpenSSL의 `SSL_CTX_set_default_verify_paths()`만으로는 iOS/Android에서 신뢰 앵커가 비어 있음. |
| `X509_VERIFY_PARAM_set1_host` 호스트명 검증 | P1 | MITM(중간자 공격) 방어 활성화. |
| `seurat_rtmp_config_t` 확장 | P1 | `tls_insecure`(개발용 우회) 또는 `ca_bundle_pem` 필드 추가. |
| 실서버 인증서 체인 E2E 테스트 | P2 | YouTube / Twitch / 치지직 / SOOP 엔드포인트 각각 검증. |

---

## 3. 컴포넌트별 작업 지침 (다른 에이전트용)

### 3.1 AAC Native wrapper 개발자 — ⏳ 대기

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

### 3.2 H.264 Native wrapper 개발자 — ⏳ 대기

§1.3 전환 결정에 따른 신규 트랙. `seurat-aac-native`와 **동일한 패턴**으로 설계합니다.

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

- [ ] Phase 3 전체 종료 — `seurat-aac-native`, `seurat-h264-native`, `seurat-mpegts` 통합 완료.
- [ ] Phase 4 샘플 앱 3종(OSX/iOS/Android) 실기 송출 성공.
- [ ] Phase 5 H.264 네이티브 인코더 기본 경로 정식 채택 (OpenH264 소스 빌드 경로 릴리스 번들 제외).
- [ ] Phase E 인증서 검증 ON + 4개 메이저 플랫폼(YouTube/Twitch/치지직/SOOP) E2E 통과.
- [ ] MSVC 빌드/패키징 성공 (단, 수요 확정 시점 이후).

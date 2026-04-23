# libseurat — 진행 사항

최종 갱신: 2026-04-23 (Phase 3 Native Codecs & 단일 동적 라이브러리 통합 완료 반영)

> 완료된 작업만 기록합니다.
> - 정적 정보(프로젝트 개요·라이선스·기술 스택) → `OVERVIEW.md`
> - 남은 작업 → `TODO.md`

---

## 1. 요약

- **빌드 시스템**: 3 플랫폼(OSX / iOS / Android) 64-bit 전용으로 재정비 완료. `out` 폴더를 플랫폼별로 격리(`out/android`, `out/osx` 등).
- **단일 동적 라이브러리**: 모든 내부/외부 정적 라이브러리를 하나로 병합한 `libseurat.so` / `libseurat.dylib` 생성 및 패키징 파이프라인 연동 완료.
- **외부 의존성**: OpenSSL 3.3.2, OpenH264 2.4.x, libyuv, SRT 1.5.x 모두 빌드/패키징 통과. (단, OpenH264는 기본값 OFF)
- **자체 컴포넌트**: `seurat-flv` (FLV 먹서) + `seurat-rtmp` (RTMP + **RTMPS** publish) 구현 완료.
- **네이티브 코덱**: `seurat-h264-native` 및 `seurat-aac-native` 모듈 신설. Apple(VideoToolbox/AudioToolbox) 및 Android(MediaCodec) 하드웨어 인코더 연동 완료.
- **초기(2026-03-20) 정적 분석 보고서의 치명/심각 이슈는 전부 해결**되었습니다.

---

## 2. 컴포넌트 현황

| 분류 | 컴포넌트 | 상태 | 비고 |
|---|---|---|---|
| **External** | OpenSSL 3.3.2 | ✅ 동작 (OSX 검증) | `cmake/builder/build-openssl.cmake` |
| | OpenH264 2.4.1 | ⚠️ 선택적 폴백 | 소스 빌드 (기본값 OFF, 라이선스 이슈) |
| | libyuv (Chromium) | ✅ 동작 | YUV 컬러스페이스 변환 |
| | SRT 1.5.3 | ✅ 동작 | 내부 OpenSSL 링크 |
| **Internal (MIT)** | `seurat-flv` | ✅ 1차 완료 | 777 LOC (src 577 + header 200) |
| | `seurat-rtmp` | ✅ Phase D 완료 | 2,380 LOC, RTMP + RTMPS |
| | `seurat-h264-native` | ✅ iOS/macOS/Android | HW 네이티브 인코더 (MFT 미구현) |
| | `seurat-aac-native` | ✅ iOS/macOS/Android | HW 네이티브 인코더 (MFT 미구현) |
| **보류** | WebRTC | 🔴 미구현 (OFF 기본) | `SEURAT_RTC=OFF` |
| **제거됨** | librtmp | ✅ 삭제 | `seurat-rtmp`로 대체 |
| | x264 | ✅ 삭제 (2026-04-22) | GPL — OpenH264로 대체 |
| | libfaac | ✅ 삭제 (2026-04-22) | LGPL — 플랫폼 네이티브 AAC로 대체 |

---

## 3. 빌드 시스템 Phase 1~2 (완료)

### 3.1 초기(2026-03-20) 정적 분석 이슈 해소 현황

| # | 파일 | 초기 심각도 | 초기 문제 | 해소 상태 |
|---|---|---|---|---|
| 1 | `CMakeLists.txt:9-10` | 치명적 | TARGET/ARCH 하드코딩으로 `-D` 무시 | ✅ `CACHE STRING`으로 선언 |
| 2 | `toolchain-android.cmake` | 치명적 | NDK r13 / GCC 4.9 / `gnustl_static` | ✅ NDK r21+ 통합 LLVM toolchain, `c++_static`, clang 기반 재작성 |
| 3 | `toolchain-ios.cmake:99` | 치명적 | `CMakeForceCompiler` 사용 | ✅ include 제거, `CMAKE_C_COMPILER` 직접 설정 |
| 4 | `build-openssl.cmake` | 치명적 | OpenSSL 1.0.2q / 1.1.1d (EOL) | ✅ 3.3.2 업그레이드 |
| 5 | `toolchain-osx.cmake` | 심각 | Apple Silicon 미지원 | ✅ `arm64` / `x86_64` 두 분기 |
| 6 | `build-open-h264.cmake` | 심각 | BUILD/INSTALL_COMMAND 미완성 | ✅ 재작성, iOS `platform-ios.mk` 패치 적용 |
| 7 | `build-yuv.cmake` | 심각 | 중복 정의 + x264 repo 오인 | ✅ 단일 정의, chromium repo, static만 빌드 |
| 8 | `build-{rtmp,srt,rtc,yuv}.cmake` | 보통 | 로컬 `src/` 참조 | ⚠️ `build-rtc.cmake`만 잔존 (OFF) |
| 9 | `build.sh` dist 복사 | 보통 | 주석처리된 Android 로직 | ✅ `scripts/package-*.sh`로 분리, `.aar`/`.xcframework`/lipo 생성 |
| 10 | `build-x264.cmake` | 경미 | `./Configure` 대소문자 | ✅ 파일 삭제 (GPL 제거) |

### 3.2 툴체인 / 빌더 세부 완료 사항

| 영역 | 작업 | 완료 내용 |
|---|---|---|
| Phase 1 | `CMakeLists.txt` 64-bit 전환 | `SEURAT_TARGET`/`SEURAT_ARCH` 캐시화 |
| Phase 1 | `toolchain-android.cmake` NDK r26+ | API 24, `arm64-v8a` / `x86_64` |
| Phase 1 | `toolchain-ios.cmake` | `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES` 전파, `CMAKE_SYSTEM_PROCESSOR` 명시 |
| Phase 1 | `toolchain-osx.cmake` | Apple Silicon + x86_64 Universal |
| Phase 1 | OpenSSL 업그레이드 | 3.3.2 |
| Phase 2 | `build-open-h264.cmake` | iOS `platform-ios.mk` 오버라이드, Android `libopenh264.a` 타겟 직지정 (Gradle 우회) |
| Phase 2 | `build-yuv.cmake` | 올바른 chromium repo, iOS/macOS 크로스 아키텍처 fix, libjpeg 우회 |
| Phase 2 | `build-srt.cmake` | 내부 OpenSSL 링크, `CMAKE_POLICY_VERSION_MINIMUM=3.5` |
| 라이선스 정리 | GPL/LGPL 잔존물 제거 | `SEURAT_H264`(x264) / `SEURAT_FAAC` 옵션 + 빌더 + `cmake/builder/faac/` 디렉터리 삭제 |

---

## 4. 자체 컴포넌트 구현 스냅샷

### 4.1 `seurat-flv` — FLV 먹서 ✅ 1차 완료

- 위치: `src/seurat-flv/`
- 규모: `src/flv.cpp` 577 LOC + `include/seurat/flv.h` 200 LOC
- 공개 API:
  ```c
  seurat_flv_muxer_t* seurat_flv_create(const seurat_flv_config_t*,
                                          seurat_flv_writer_fn, void*);
  int seurat_flv_write_header        (m, has_video, has_audio);
  int seurat_flv_write_metadata      (m);
  int seurat_flv_write_video_sequence(m, ts, sps, sps_len, pps, pps_len);
  int seurat_flv_write_video_frame   (m, ts, is_keyframe, cts, avcc, avcc_len);
  int seurat_flv_write_audio_sequence(m, ts, asc, asc_len);
  int seurat_flv_write_audio_frame   (m, ts, raw, raw_len);
  int seurat_flv_annexb_to_avcc      (in, in_len, out, out_cap, written);
  ```
- 설계 결정:
  - 송출 전용이므로 파일 I/O 대신 **`seurat_flv_writer_fn` 콜백**으로 바이트 스트림 출력
  - 내부 AMF0 인코더 (`onMetaData` ECMA array 용) — SRS 의존성 없이 자체 작성
  - AVCC/AnnexB 변환 유틸 포함
- 참고: Adobe FLV File Format Specification v10.1

### 4.2 `seurat-rtmp` — RTMP(S) publisher ✅ Phase D 완료

- 위치: `src/seurat-rtmp/`
- 규모: **2,380 LOC 총**
  - `rtmp_socket.cpp` (207) — POSIX TCP 트랜스포트 (`PosixTransport` + 팩토리)
  - `rtmp_tls.cpp` (180) — OpenSSL 기반 `TlsTransport` (RTMPS, Phase D)
  - `rtmp_handshake.cpp` (94) — RTMP 1.0 simple handshake (C0/C1/C2 ↔ S0/S1/S2)
  - `rtmp_chunk.cpp` (319) — Chunk stream writer/reader (fmt 0/1/2/3, ext-ts, per-cs_id state)
  - `rtmp_amf0.cpp` (344) — AMF0 writer + reader (Number/String/Boolean/Null/Object/ECMA/StrictArray/Date)
  - `rtmp.cpp` (880) — URL 파싱, 생명주기, publish 플로우, AV 페이로드
  - 헤더 403 LOC (`rtmp_internal.h` 229 + `include/seurat/rtmp.h` 174)

#### 구현된 publish 시퀀스

```
1. TCP connect (non-blocking + poll timeout, TCP_NODELAY)
2. (rtmps://인 경우) TLS handshake (OpenSSL BIO/SSL, SNI 포함)
3. RTMP 1.0 simple handshake (C0/C1/C2 ↔ S0/S1/S2)
4. SetChunkSize(4096) 송신, 서버 SetChunkSize 수신 시 반영
5. AMF0 "connect"  (tx=1) → _result / NetConnection.Connect.Success
6. AMF0 "releaseStream" / "FCPublish" (fire-and-forget)
7. AMF0 "createStream"  (tx=N) → _result 에서 stream id 추출
8. AMF0 "publish" on stream id → onStatus / NetStream.Publish.Start
9. 이후 AV 페이로드 송신 가능
```

#### 구현된 AV 페이로드 (FLV tag body 포맷과 동일)

- `send_metadata`: AMF0 `@setDataFrame` + `onMetaData` + ECMA array
- `send_video_sequence`: AVCDecoderConfigurationRecord (ISO 14496-15) 조립
- `send_video_frame`: AVCC NAL 배열, FrameType(key/inter) × CodecID(AVC)
- `send_audio_sequence`: AudioSpecificConfig, FLV AAC seq header (`0xAF 0x00`)
- `send_audio_frame`: raw AAC (`0xAF 0x01`)

상태 머신(`video_seq_sent` / `audio_seq_sent` / `metadata_sent`)으로 호출 순서 강제.
통계(`bytes_sent_total`, `bytes_recv_total`, `rtmp_messages_sent`,
`video_frames_sent`, `audio_frames_sent`)는 `seurat_rtmp_get_stats`로 노출.

#### RTMPS(TLS) — Phase D 세부

- 구현: `src/seurat-rtmp/src/rtmp_tls.cpp`
- 구조: `TlsTransport : public Transport`가 `PosixTransport posix_`를 멤버로 소유 (데코레이터 패턴)
  - `connect_host()` → `posix_.connect_host()`로 TCP 다이얼 → `posix_.fd()`를
    `BIO_new_socket`에 `BIO_NOCLOSE`로 부착 → `SSL_connect()`로 TLS 핸드셰이크
  - `send_all` / `recv_exact`은 `SSL_write` / `SSL_read` 루프 (WANT_READ/WANT_WRITE 재시도)
  - `close()`는 `SSL_shutdown` → `SSL_free` / `SSL_CTX_free` → `posix_.close()`
- OpenSSL 설정:
  - `SSL_CTX_new(TLS_client_method())` + `SSL_CTX_set_min_proto_version(TLS1_2_VERSION)`
  - `SSL_CTX_set_mode(AUTO_RETRY | ENABLE_PARTIAL_WRITE | ACCEPT_MOVING_WRITE_BUFFER)`
  - `SSL_set_tlsext_host_name()` — SNI (YouTube/Twitch/SOOP 공유 엔드포인트 필수)
- 팩토리: `rtmp_internal.h::make_transport(bool use_tls)`가 `SEURAT_CRYPTO` 여부에 따라
  `make_tls_transport()` 또는 `new PosixTransport()`로 디스패치.
- **인증서 검증은 1차 구현에서 `SSL_VERIFY_NONE`** — 상세는 `TODO.md` Phase E 참조.

### 4.3 `seurat-aac-native` 및 `seurat-h264-native` — HW 코덱 래퍼 ✅ 완료 (Apple/Android)

- 목적: 로열티 이슈가 있는 OpenH264 / FAAC 라이브러리를 제거하고 OS 네이티브 가속기를 사용하여 C 레벨 공통 API 제공.
- 1차 통합 패키징: `libseurat.so` / `libseurat.dylib` 로 묶여 배포됨.

#### Apple (iOS / macOS) 구현
- H.264 (`h264_videotoolbox.c`): `VTCompressionSession` 사용. I420 입력을 `CVPixelBuffer`로 변환하여 밀어넣음. 콜백에서 추출한 `CMSampleBuffer`를 파싱하여 AVCC 포맷의 NAL 유닛 길이값을 Annex-B 포맷(`0x00 0x00 0x00 0x01`)으로 치환. 키프레임일 시 SPS/PPS 추출 후 주입.
- AAC (`aac_audiotoolbox.c`): `AudioConverterFillComplexBuffer` 사용. 입력 PCM 버퍼의 길이 계산 후 제공.

#### Android (NDK) 구현
- H.264 (`h264_ndkmedia.c`): NDK `AMediaCodec` 사용. `dequeueInputBuffer`로 I420 복사, `dequeueOutputBuffer`로 NAL 추출 및 키프레임 플래그 체크. `request-sync` 파라미터를 통한 키프레임 강제 주입 로직은 API 26 이상 분기 처리.
- AAC (`aac_ndkmedia.c`): NDK `AMediaCodec` 사용. `AMEDIAFORMAT_KEY_IS_ADTS=1` 설정으로 출력 버퍼에서 즉시 ADTS 헤더 포함 AAC 페이로드 추출.

#### 산출물 크기 (Release 빌드, 단일 아키텍처 `.a`)

| 라이브러리 | arm64 (macOS) | arm64-v8a (Android) |
|---|---|---|
| `libseurat-flv.a` | ~19 KB | ~33 KB |
| `libseurat-rtmp.a` | ~67 KB | ~113 KB |

---

## 5. 빌드 검증 현황

| 타겟 | 결과 | 근거 |
|---|---|---|
| OSX (arm64 + x86_64) | ✅ 전 컴포넌트 단일 동적 라이브러리(`libseurat.dylib`) 빌드 성공 | `build.sh OSX Dev` 통과 |
| Android (arm64-v8a + x86_64) | ✅ 전 컴포넌트 단일 동적 라이브러리(`libseurat.so`) 포함 `seurat.aar` 생성 | NDK r26 / Clang 17 |
| iOS (arm64 + arm64-sim + x86_64) | ✅ 7종 xcframework 생성 (`device: ios-arm64`, `sim: ios-arm64_x86_64`) | AppleClang 21 / iPhoneOS 26.4 SDK, `build-ios.log` |
| MSVC (x86_64) | 🔴 미시도 (패키징 단계 미구현, 구현체 미작성) | — |

> **Android 빌드 툴체인 메모**: 로컬 크로스컴파일용으로 NDK r26 (`~/tools/ndk/26.1.10909125`) 사용.
> `NDK_R21` 환경변수가 존재하므로 빌드 시 `ANDROID_NDK_HOME=~/tools/ndk/26.1.10909125` 명시 필요.
> libyuv `main` 브랜치가 neon64(dotprod/i8mm)·SVE2 타겟을 포함하므로 Clang 17+ (NDK r26) 필수.
> 런타임 CPU 디스패치로 구형 arm64-v8a 기기에서도 크래시 없이 동작 (미지원 경로는 baseline NEON 사용).


---

## 6. 변경 이력

| 날짜 | 내용 |
|---|---|
| 2026-03-20 | `ANALYSIS.md` 초기 작성 — 빌드 시스템 정적 분석 (치명/심각/보통 이슈 10건 식별) |
| 2026-04-22 | `DESIGN.md` 초안 — 기술 스택 및 라이선스 정책 확정 |
| 2026-04-22 | Phase 1 완료 — `CMakeLists.txt` 64-bit 전환, 3개 툴체인 현대화, OpenSSL 3.3.2 업그레이드. 3 플랫폼 빌드 성공. |
| 2026-04-22 | Phase 2 완료 — OpenH264 / libyuv / SRT 빌더 재작성. iOS 시뮬레이터 arm64/x86_64 fat, xcframework 패키징, Android AAR 패키징 검증. |
| 2026-04-22 | Phase 3A 완료 — `librtmp`(LGPL) 배제, `seurat-flv` 풀 구현(777 LOC), `seurat-rtmp` 스캐폴드 + URL 파서. 3 플랫폼 패키징 통과. |
| 2026-04-22 | Phase 3B 완료 — `seurat-rtmp` 트랜스포트 + 핸드셰이크 + 청크 스트림 + AMF0 + publish 네고시에이션. |
| 2026-04-22 | Phase 3C 완료 — `seurat-rtmp` AV 페이로드 송신(`send_metadata`/`send_{video,audio}_{sequence,frame}`) + 상태 머신. |
| 2026-04-22 | GPL/LGPL 코덱 잔존물 완전 제거 — `SEURAT_H264`(x264, GPL) / `SEURAT_FAAC`(FAAC, LGPL) 옵션·빌더·`cmake/builder/faac/` 삭제. |
| 2026-04-22 | Phase 3D 완료 — `seurat-rtmp` RTMPS/TLS 지원. `rtmp_tls.cpp`에 `TlsTransport`(OpenSSL BIO/SSL 데코레이터, SNI 포함) 구현. `rtmps://` URL이 `SEURAT_CRYPTO=ON` 빌드에서 정상 연결. OSX/iOS-sim/Android(NDK r21e) 컴파일 검증. 인증서 체인 검증은 Phase E로 이관. |
| 2026-04-22 | 문서 구조 재정비 — `ANALYSIS.md` / `DESIGN.md`를 `OVERVIEW.md` / `PROGRESS.md` / `TODO.md`로 분리. |
| 2026-04-23 | Android 빌드 완전 검증 — NDK r26 (Clang 17) 도입. libyuv `main` (neon64/SVE 포함) 빌드 성공. `seurat.aar` (17MB, arm64-v8a+x86_64) 생성 및 공개 API 11종 심볼 확인. libyuv neon64 타겟이 Clang 9 미지원임을 확인 → Android 크로스컴파일은 NDK r26 이상 필수. |
| 2026-04-23 | iOS 빌드 완전 검증 — AppleClang 21 / iPhoneOS 26.4 SDK. 7종 xcframework 생성 (device: arm64, simulator: arm64+x86_64 lipo fat). |
| 2026-04-23 | **네이티브 코덱 및 단일 라이브러리 통합** — `SEURAT_OPENH264` 기본값 OFF 및 네이티브 HW 코덱 전환. Apple (VideoToolbox/AudioToolbox) 및 Android (MediaCodec) 용 H.264/AAC 코덱 `seurat-*_native` 완전 구현. 모든 정적 아카이브를 하나로 묶는 `libseurat` 단일 동적 라이브러리(macOS: `libseurat.dylib`, Android: `libseurat.so`) 빌드 파이프라인 구성. 플랫폼별로 CMake `out` 폴더 경로 격리 작업 완료. |

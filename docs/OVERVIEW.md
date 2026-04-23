# libseurat (seurat) — 프로젝트 기본 정보

최종 갱신: 2026-04-23

> 본 문서는 `libseurat`의 **정적(changeless) 정보**만 담습니다.
> - 진행 상태 → `PROGRESS.md`
> - 남은 작업 → `TODO.md`

---

## 1. 프로젝트 역할

`libseurat` (코드명 `seurat`)는 모바일/데스크톱 애플리케이션에서 사용할
**라이브 스트리밍 송출(publish) 전용 미디어 라이브러리**입니다.

CMake `ExternalProject_Add` 기반으로 허용 라이선스 범위의 3rd-party 오픈소스를
크로스컴파일하고, 내부 컴포넌트(`seurat-flv`, `seurat-rtmp`, `seurat-*-native` 등)와 함께 묶어
플랫폼별 단일 동적 라이브러리(`libseurat.so`, `libseurat.dylib`)를 포함한 배포 산출물(AAR / xcframework / OSX Bundle)을
생성합니다. 과거의 파편화된 정적 아카이브를 하나로 통합하여 제공하는 것이 특징입니다.

### 1.1 제공 기능

| 분류 | 기능 | 비고 |
|---|---|---|
| **송출(주 용도)** | RTMP / RTMPS publish | YouTube Live, Twitch, 치지직, SOOP, Facebook Live 등 |
| **송출(보조 용도)** | SRT publish | 자체 미디어 서버, 방송 장비, SRT→RTMP 게이트웨이 |
| **영상 처리** | YUV 색공간 변환, 스케일링, 블렌딩 | `libyuv` |
| **영상 인코딩** | H.264 Main/High | 플랫폼 네이티브 인코더 wrapper (`seurat-h264-native`) |
| **음성 인코딩** | AAC-LC | 플랫폼 네이티브 인코더 wrapper 예정 (`seurat-aac-native`) |
| **암호화** | TLS 1.2+ (RTMPS) | OpenSSL 3.3.2 |
| **먹싱** | FLV / MPEG-TS | `seurat-flv` 구현, `seurat-mpegts` 미구현 |

### 1.2 명시적 비목표 (Non-Goals)

- **재생(playback)** — HLS/DASH 수신, 디코딩 재생은 범위 밖
- **RTMP 서버** — 송출 클라이언트만 구현
- **WebRTC / WHIP** — YouTube 미지원이라 현재 범위 밖 (향후 별도 라이브러리로 분리 검토)

---

## 2. 라이선스 정책

**모든 컴포넌트는 상용 배포 가능한 permissive 라이선스만 사용합니다.**

### 2.1 허용 / 금지

| 구분 | 대상 |
|---|---|
| **허용** | MIT, BSD (2/3-Clause), Apache 2.0, MPL 2.0, ISC, Zlib, Unlicense |
| **금지** | GPL (any), LGPL (any), AGPL, 특허 지뢰 라이선스 |

### 2.2 미디어 특허 처리 전략

코덱 특허(H.264, AAC)는 **라이브러리에 인코더를 내장하지 않고 플랫폼 네이티브
또는 로열티 대납 구현을 사용**하여 우회합니다.

| 특허 | 처리 방식 |
|---|---|
| H.264 | **플랫폼 네이티브 인코더** (VideoToolbox / MediaCodec / MFT) — 로열티를 OS 제조사가 대납 |
| AAC | **플랫폼 네이티브 인코더** (AudioToolbox / MediaCodec / MFT) — 로열티를 OS 제조사가 대납 |

> ⚠️ OpenH264 소스 빌드는 기본적으로 **OFF**되어 있으며, 개발 및 테스트용 또는 특수 플랫폼의
> 소프트웨어 폴백(opt-in) 용도로만 제한적으로 지원됩니다. 상용 배포에서는 반드시 플랫폼 네이티브
> 인코더를 사용해야 합니다.
> 근거: [Cisco OpenH264 FAQ](https://www.openh264.org/faq.html)

### 2.3 배제 컴포넌트 및 사유 (Decision Log)

| 항목 | 배제 이유 | 재검토 조건 |
|---|---|---|
| x264 | GPL 2.0 | OpenH264 품질 부족 시 상용 H.264 라이선스 구매 검토 |
| FAAC | LGPL 2.1 + 품질 낮음 | 재검토 불필요 |
| FDK-AAC | 특허 이슈 (한국/미국) | 재검토 불필요 |
| FFmpeg | LGPL/GPL 전염 리스크 | 범위가 급격히 확장될 경우 재검토 |
| Opus | YouTube 미지원 | WebRTC/WHIP 지원 시점 |
| librtmp (rtmpdump) | LGPL + 2015년 이후 유지보수 중단 + RTMPS 미지원 | 재검토 불필요 |
| WebRTC | YouTube 미지원, 규모 방대 | 별도 라이브러리로 분리하여 2차 이슈화 |
| MonaServer | GPL | 재검토 불필요 |
| pili-librtmp | LGPL | 재검토 불필요 |

---

## 3. 지원 범위

### 3.1 타겟 플랫폼 / 아키텍처 (64-bit 전용)

| 플랫폼 | 아키텍처 | 산출물 |
|---|---|---|
| Android | `arm64-v8a`, `x86_64` | `dist/android/seurat.aar` (`jni/<abi>/libseurat.so` 등) |
| iOS | `arm64` (device), `arm64-sim`, `x86_64` (sim) | `dist/ios/Libseurat.xcframework` |
| macOS | `arm64` (Apple Silicon), `x86_64` | `dist/osx/lib/libseurat.dylib` (lipo universal) |
| Windows (MSVC) | `x86_64` | 미구현 (`TODO.md` 참조) |

### 3.2 최소 환경 요구사항

| 항목 | 요구 |
|---|---|
| CMake | 3.21 이상 |
| OpenSSL | 3.3.2 (`OPENSSL_BUILD_VERSION`로 덮어쓰기 가능) |
| Android NDK | **r21 이상 (r26 권장)**, min API 24, STL `c++_static` |
| iOS | Xcode 14+, deployment target 13.0, bitcode OFF |
| macOS | deployment target 11.0 |
| Windows | MSVC + Perl + NASM (OpenSSL 빌드용) |

---

## 4. 기술 스택 (컴포넌트 구성)

```
libseurat (seurat)
│
├─ [영상 소스 처리]
│   └─ libyuv          색공간 변환 + 더미 이미지 합성
│
├─ [영상 인코딩]
│   ├─ H.264 Native    플랫폼별 네이티브 wrapper (seurat-h264-native, 기본)
│   └─ OpenH264        H.264 소프트웨어 인코더 (폴백용, 기본 OFF)
│
├─ [음성 인코딩]
│   └─ AAC Native      플랫폼별 네이티브 wrapper
│       ├─ iOS/macOS   AudioToolbox (AudioConverter)
│       ├─ Android     MediaCodec
│       └─ Windows     Media Foundation (MFT AAC Encoder)
│
├─ [암호화]
│   └─ OpenSSL 3.x     RTMPS TLS
│
├─ [송출 A: 일반 라이브 플랫폼]
│   ├─ seurat-flv      FLV 먹서 (자체 구현)
│   └─ seurat-rtmp     RTMP(S) 클라이언트 (자체 구현)
│
└─ [송출 B: 자체/프로 워크플로우]
    ├─ seurat-mpegts   MPEG-TS 먹서 (미구현)
    └─ libsrt          SRT 클라이언트
```

### 4.1 외부 의존성

| 라이브러리 | 버전 | 라이선스 | 용도 | 소스 |
|---|---|---|---|---|
| OpenSSL | 3.3.x / 3.4.x | Apache 2.0 | TLS (RTMPS), 암호화 | https://www.openssl.org/source/ |
| OpenH264 | 2.4.x+ | BSD 2-Clause | H.264 인코딩 (선택적) | https://github.com/cisco/openh264 |
| libyuv | main (rolling) | BSD 3-Clause | 색공간 변환, 스케일링 | https://chromium.googlesource.com/libyuv/libyuv |
| libsrt | 1.5.x+ | MPL 2.0 | SRT 전송 | https://github.com/Haivision/srt |
| libmpegts | 최신 | ISC | MPEG-TS 먹싱 (예정) | https://github.com/kierank/libmpegts |

### 4.2 자체 개발 컴포넌트 (모두 MIT, 내부)

| 이름 | 역할 | 예상 규모 |
|---|---|---|
| `seurat-flv` | FLV 태그 먹싱 (video/audio/metadata) | ~500 LOC |
| `seurat-rtmp` | RTMP(S) 클라이언트 (송출 전용) | ~2,380 LOC |
| `seurat-h264-native` | 플랫폼별 H.264 인코더 wrapper | 플랫폼별 ~300 LOC |
| `seurat-aac-native` | 플랫폼별 AAC 인코더 wrapper | 플랫폼별 ~200 LOC |
| `seurat-mpegts` | MPEG-TS 먹서 (SRT 페이로드용) | TBD |

### 4.3 CMake 옵션

```cmake
# [외부 의존성 — ExternalProject 기반]
option(SEURAT_CRYPTO   "OpenSSL (TLS for RTMPS, crypto)"    ON)
option(SEURAT_OPENH264 "Cisco OpenH264 (H.264 encoder)"     OFF)
option(SEURAT_YUV      "libyuv (colorspace, scaling)"       ON)
option(SEURAT_SRT      "SRT (pro workflow transport)"       ON)

# [자체 송출 컴포넌트]
option(SEURAT_FLV      "Build seurat-flv (FLV muxer)"       ON)
option(SEURAT_RTMP     "Build seurat-rtmp (RTMP publisher)" ON)
option(SEURAT_H264_NATIVE "Build seurat-h264-native"        ON)
option(SEURAT_AAC_NATIVE  "Build seurat-aac-native"         ON)

# [보류 — 설계/구현 완료 전까지 OFF]
option(SEURAT_RTC      "WebRTC (experimental)"              OFF)
```

- `SEURAT_RTMP=ON`은 `SEURAT_FLV=ON`을 강제합니다 (`rtmp → flv` 의존).
- 타겟/아키텍처는 `SEURAT_TARGET` (`ANDROID|IOS|OSX|MSVC`)과 `SEURAT_ARCH`로 지정.

---

## 5. 프로토콜별 송출 경로

### 5.1 RTMP(S) — 주 용도

```
[카메라/화면 캡처]
    ↓ NV12/NV21
    ↓ libyuv (NV12→I420, 스케일링, 로고 합성)
    ↓ I420
    ↓ H.264 Native (VideoToolbox / MediaCodec / MFT)
    ↓ H.264 NAL units
    ↓ seurat-flv (FLV video tag)
    ↓
[플랫폼 마이크]
    ↓ PCM 16-bit
    ↓ AAC Native (AudioToolbox / MediaCodec / MFT)
    ↓ AAC-LC
    ↓ seurat-flv (FLV audio tag)
    ↓
[FLV tag stream]
    ↓ seurat-rtmp (청크 스트림, AMF0 명령)
    ↓ OpenSSL (TLS, RTMPS인 경우)
    ↓
[YouTube Live / Twitch / 치지직 ...]
```

### 5.2 SRT — 보조 용도

```
[영상/음성 소스] → ... (동일 인코딩 경로)
    ↓ H.264 NAL + AAC-LC
    ↓ seurat-mpegts (PES, PAT, PMT, PCR)
    ↓ MPEG-TS 188-byte packets
    ↓ libsrt
    ↓
[자체 서버 / 방송 장비]
```

### 5.3 YouTube Live 기준 권장 파라미터

| 영역 | 파라미터 | 값 |
|---|---|---|
| 비디오 | 프로파일 / 레벨 | Main or High / 4.0+ |
| | 비트레이트 모드 | CBR |
| | 키프레임 간격 | 2초 (최대 4초) |
| | 색공간 | YUV 4:2:0 (I420) |
| | 해상도/FPS | 720p60 ~ 2160p60 |
| 오디오 | 코덱 | AAC-LC |
| | 샘플레이트 | 44.1 / 48 kHz |
| | 채널 / 비트레이트 | Stereo / 128 kbps+ |
| 전송 | 프로토콜 | RTMPS (기본) |
| | URL | `rtmps://a.rtmps.youtube.com/live2/<stream-key>` |

> YouTube Live는 2026년 4월 기준 **SRT, WebRTC/WHIP, RIST를 지원하지 않습니다.**

---

## 6. 빌드 흐름 개요

```
./build.sh {ANDROID|IOS|OSX|MSVC} [Release|Debug|Dev]
    │
    ├─ ARCHS=(플랫폼별 64bit 아키텍처 목록)
    │
    ▼ per-ARCH 루프
    cmake -S . -B WORK/${TARGET}-${ARCH}
          -D SEURAT_TARGET=${TARGET} -D SEURAT_ARCH=${ARCH}
    │       → out/<platform>/${ARCH}/{include,lib}/ 에 per-arch 산출물 설치
    │
    ▼ 전 ARCH 완료 후 packaging 단계
    scripts/package-android.sh  out/android dist/android "${ARCHS}"  → dist/android/seurat.aar
    scripts/package-ios.sh      out/ios     dist/ios     "${ARCHS}"  → dist/ios/Libseurat.xcframework
    scripts/package-osx.sh      out/osx     dist/osx     "${ARCHS}"  → dist/osx/{lib,include}/
    # MSVC 패키징은 미구현
```

### 6.1 산출물 레이아웃

```
out/
├── android/
│   ├── arm64-v8a/
│   └── x86_64/
├── ios/
│   ├── arm64/
│   ├── arm64-sim/
│   └── x86_64/
└── osx/
    ├── arm64/
    └── x86_64/
        ├── include/{openssl,srt,libyuv,wels,seurat}/*.h
        └── lib/lib{crypto,ssl,srt,yuv,seurat}.*

dist/
├── android/seurat.aar
├── ios/Libseurat.xcframework
└── osx/
    ├── include/
    └── lib/libseurat.dylib
```

---

## 7. 참고 자료

### 표준 스펙
- [Adobe RTMP 1.0 Specification](https://rtmp.veriskope.com/pdf/rtmp_specification_1.0.pdf)
- [Adobe FLV File Format Specification v10.1](https://wwwimages2.adobe.com/content/dam/acom/en/devnet/flv/video_file_format_spec_v10_1.pdf)
- [Enhanced RTMP v2 (Veovera)](https://github.com/veovera/enhanced-rtmp)
- [SRT Protocol RFC Draft](https://datatracker.ietf.org/doc/draft-sharabayko-srt/)
- [ISO/IEC 13818-1 MPEG-TS](https://www.iso.org/standard/75928.html)

### 참조 구현
- SRS (RTMP 레퍼런스): https://github.com/ossrs/srs
- MediaMTX (통합 서버): https://github.com/bluenviron/mediamtx
- libmpegts: https://github.com/kierank/libmpegts

### 플랫폼 네이티브 AAC 문서
- [AudioToolbox AudioConverter](https://developer.apple.com/documentation/audiotoolbox/audio_converter_services)
- [Android MediaCodec](https://developer.android.com/reference/android/media/MediaCodec)
- [Windows AAC Encoder (MFT)](https://learn.microsoft.com/en-us/windows/win32/medfound/aac-encoder)

### 플랫폼별 송출 기준
- [YouTube Live 인코더 요구사항](https://support.google.com/youtube/answer/2853702)
- [Twitch 브로드캐스팅 가이드라인](https://stream.twitch.tv/encoding/)

# IL2CPP Dumper

Unity IL2CPP 게임의 메타데이터를 추출하는 GUI 도구입니다.

![GUI Screenshot](docs/screenshot.png)

## 기능

- **GUI 인터페이스** - 버튼 클릭으로 간편하게 덤프
- **3가지 출력 모드**
  - Human: 완전한 C# 코드
  - AI: LLM 분석용 필터링된 JSON
  - Custom: 원하는 조합 선택
- **실시간 진행률** - 프로그레스바와 로그 표시
- **스마트 필터링** - Unity/System 어셈블리 자동 제외

## 빌드

### 요구 사항

- Windows 10/11 (x64)
- Visual Studio 2022 (v143 toolset)

### 빌드 방법

```cmd
# Visual Studio로 열기
start Dump.sln

# 또는 명령줄 빌드
msbuild Dump.sln /p:Configuration=Release /p:Platform=x64
```

### 출력

```
bin\Release\IL2CPP_Dumper.dll    (덤퍼 DLL)
bin\Release\IL2CPP_Injector.exe  (인젝터)
```

## 사용법

### 1단계: 게임 실행

덤프할 Unity IL2CPP 게임을 실행하고 완전히 로드될 때까지 기다립니다.

### 2단계: DLL 인젝션

#### 방법 1: 내장 인젝터 사용 (추천)

`IL2CPP_Injector.exe`를 실행하면 됩니다:

1. **Refresh** 클릭 - IL2CPP 게임 자동 탐지
2. 목록에서 게임 프로세스 선택
3. **Inject DLL** 클릭

> Manual Map 방식으로 인젝션하여 탐지율이 낮습니다.

#### 방법 2: 외부 인젝터 사용

다른 DLL 인젝터 사용:
- Process Hacker (무료)
- Extreme Injector
- 기타 인젝터

**인젝션 방법:**
1. 인젝터에서 게임 프로세스 선택
2. `IL2CPP_Dumper.dll` 선택
3. Inject 클릭

### 3단계: GUI 사용

인젝션 성공 시 GUI 창이 나타납니다:

```
┌─────────────────────────────────────────────────┐
│ IL2CPP Dumper                              [_][X]│
├─────────────────────────────────────────────────┤
│ ┌─ Output Mode ───────────────────────────────┐ │
│ │ ○ Human (C# only)  ● AI (JSON only)  ○ Custom│ │
│ │ Human: Full C# dump | AI: Filtered JSON     │ │
│ └─────────────────────────────────────────────┘ │
│                                                 │
│ ┌─ Filters (AI/Custom mode) ──────────────────┐ │
│ │ ☑ Skip UnityEngine.*    ☑ Skip System.*     │ │
│ │ ☑ Skip private members  ☑ Skip compiler-gen │ │
│ └─────────────────────────────────────────────┘ │
│                                                 │
│ ┌─ Output Formats (Custom mode) ──────────────┐ │
│ │ ☑ C# (.cs)  ☑ JSON Full  ☑ JSON Summary     │ │
│ └─────────────────────────────────────────────┘ │
│                                                 │
│ [Start Dump]  [Open Output Folder]              │
│                                                 │
│ ████████████████████████████░░░░░░░░░░ 75%     │
│ Assembly-CSharp.dll                             │
│                                                 │
│ Log:                                            │
│ ┌─────────────────────────────────────────────┐ │
│ │ === IL2CPP Dumper ===                       │ │
│ │ Initializing...                             │ │
│ │ [OK] Found 45 assemblies                    │ │
│ │                                             │ │
│ │ Dumping: Assembly-CSharp.dll                │ │
│ │   -> Assembly-CSharp_dll.json [JSON Full]   │ │
│ │   -> Assembly-CSharp_dll.json [JSON Summary]│ │
│ └─────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

### 4단계: 결과 확인

**Human 모드:**
```
C:\IL2CPP_Dump\
  └── *.cs (전체 어셈블리)
```

**AI 모드:**
```
C:\IL2CPP_Dump_JSON\
  └── *.json (필터링된 전체 메타데이터)

C:\IL2CPP_Dump_Summary\
  └── *.json (public API만)
```

## 출력 모드 비교

| 모드 | 출력 | 필터링 | 용도 |
|------|------|--------|------|
| Human | C# | 없음 | 코드 리버싱, 분석 |
| AI | JSON Full + Summary | Unity/System 제외 | LLM 분석 |
| Custom | 선택 가능 | 선택 가능 | 맞춤 설정 |

## 필터 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| Skip UnityEngine.* | Unity 엔진 어셈블리 제외 | ON |
| Skip System.* | .NET 기본 어셈블리 제외 | ON |
| Skip private members | private/internal 멤버 제외 | ON |
| Skip compiler-generated | `<>`, `__` 등 자동 생성 코드 제외 | ON |

## JSON 출력 예시

### JSON Full

```json
{
  "assembly": "Assembly-CSharp.dll",
  "classCount": 150,
  "classes": [
    {
      "name": "PlayerController",
      "namespace": "Game",
      "fullName": "Game.PlayerController",
      "type": "class",
      "token": "0x2000015",
      "extends": "MonoBehaviour",
      "fields": [
        {
          "name": "health",
          "type": "float",
          "access": "public",
          "offset": "0x18"
        }
      ],
      "methods": [
        {
          "name": "TakeDamage",
          "returnType": "void",
          "params": [{"type": "float", "name": "amount"}],
          "access": "public",
          "virtual": true
        }
      ]
    }
  ]
}
```

### JSON Summary (LLM용)

```json
{
  "assembly": "Assembly-CSharp.dll",
  "classCount": 150,
  "classes": [
    {
      "name": "PlayerController",
      "fullName": "Game.PlayerController",
      "type": "class",
      "extends": "MonoBehaviour",
      "fields": [
        {"name": "health", "type": "float"}
      ],
      "methods": [
        {
          "name": "TakeDamage",
          "returnType": "void",
          "params": [{"type": "float", "name": "amount"}]
        }
      ]
    }
  ]
}
```

## 문제 해결

### 게임이 목록에 안 나옴 (인젝터)

- 게임이 완전히 로드된 후 Refresh 클릭
- IL2CPP 게임만 탐지됨 (GameAssembly.dll 필요)
- 관리자 권한으로 인젝터 실행

### GUI 창이 안 나타남

- 인젝션 실패. 관리자 권한으로 실행
- 내장 인젝터 또는 다른 인젝터 사용
- 안티바이러스 일시 비활성화

### "No assemblies found"

- 게임이 완전히 로드된 후 인젝션
- 메인 메뉴/로비까지 진입 후 시도

### 일부 어셈블리만 덤프됨

- AI 모드는 Unity/System 필터링이 기본
- Human 모드 또는 Custom 모드 사용

### 빌드 실패

- Visual Studio 2022 설치 확인
- "C++을 사용한 데스크톱 개발" 워크로드 설치

## 라이선스

MIT License

# 📝 Nice Editor (ncurses 기반 C 텍스트 에디터)

**Nice Editor**는 C 언어로 제작된 터미널 기반 텍스트 에디터로,  
직관적인 UI와 `ncurses`, 시스템 콜, 스레드, 프로세스 제어 등  
고급 시스템 프로그래밍 기술을 활용한 프로젝트입니다.

---

## ✨ 주요 기능

- 📁 파일 열기, 저장, 새로 만들기, 다른 이름으로 저장
- 🧠 자동 저장 (AutoSave) – 설정된 주기로 파일 자동 저장
- 🖊 문법 강조(Syntax Highlight) – C 키워드 기반 색상 표시
- 🔢 라인 넘버 표시 / 숨기기
- 📦 Build 메뉴 – gcc 컴파일 및 실행 (gnome-terminal 사용)
- 📂 파일 선택창 / 파일명 입력창 / 팝업창 UI
- 💡 도움말(Guide) 및 상태(Status) 확인 창

---

## 🛠️ 사용된 기술

- `ncurses` – 터미널 기반 UI 구성
- `pthread` – AutoSave 백그라운드 스레드
- `fork`, `execvp` – 외부 프로세스 실행 (gcc 컴파일)
- `open`, `write`, `read`, `close` – 파일 I/O
- `signal` – 창 크기 변경 대응(SIGWINCH)
- `getch`, `wgetnstr` – 키보드 및 문자열 입력 처리

---

## 🧰 설치 방법

### 1. 라이브러리 설치
Ubuntu 기준:
```bash
sudo apt-get install libncurses5-dev libncursesw5-dev

```bash
make

```bash
./editor

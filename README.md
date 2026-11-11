# Calculator App — HTML/CSS/JS Frontend + C++ Backend

A calculator web app where the UI is plain **HTML, CSS and JavaScript**, and every
calculation is performed by a **C++ HTTP server** written from scratch — no external
libraries, no Node.js.

## Project structure

```
CalculatorApp/
├── backend/
│   └── server.cpp          # C++ HTTP server + math expression engine
├── frontend/
│   ├── index.html           # calculator page
│   ├── style.css            # styling (dark glassy theme)
│   └── script.js            # button/keyboard logic, calls the C++ API
├── .vscode/
│   └── tasks.json           # one-keystroke "Build & Run" task
├── build/
│   └── calculator-server.exe  (created when you build)
├── build-and-run.bat        # double-click to build + run (no VS Code needed)
└── CalculatorApp.code-workspace
```

## Requirements

- Windows
- A C++ compiler: MinGW g++ (on PATH) or MSVC
- VS Code (optional, but recommended)

## Run it in VS Code

1. Open the folder `D:\CalculatorApp` in VS Code (File → Open Folder…),
   or double-click `CalculatorApp.code-workspace`.
2. Press **Ctrl+Shift+B** (or Terminal → Run Build Task…).
3. The default task **"Build & Run Calculator (opens browser)"** compiles the
   C++ server, starts it, and opens `http://localhost:8080` in your browser.

Stop the server by closing the terminal panel (trash icon) or pressing Ctrl+C
in the server terminal.

## Run it without VS Code

Double-click `build-and-run.bat`.

## Manual build & run

```
g++ -std=c++14 -O2 -Wall backend/server.cpp -o build/calculator-server.exe -lws2_32
build\calculator-server.exe
```

## How it works

- `backend/server.cpp` is a small Winsock HTTP server. It:
  - serves the static files from `frontend/` (HTML, CSS, JS), and
  - exposes `POST /api/calculate` with body `{"expression":"2+3*4"}`,
    answered with `{"result":14}` or `{"error":"..."}`.
- The math engine is a recursive-descent parser supporting
  `+ - * / % ^`, parentheses, decimals and unary minus, with division/modulo
  by zero detection.
- The server listens on **localhost only** (no firewall popup). If port 8080
  is busy it automatically tries 8081–8095.
- The browser opens automatically on start. Set environment variable
  `CALC_NO_BROWSER=1` to disable that.

## Upload to GitHub

From a terminal in `D:\CalculatorApp` (the repo is already initialized):

```
git add .
git commit -m "Calculator app: HTML/CSS/JS frontend with C++ backend"
```

Then create an empty repository on https://github.com/new (do **not** add a
README there) and push:

```
git remote add origin https://github.com/<your-username>/calculator-app.git
git branch -M main
git push -u origin main
```

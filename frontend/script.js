// Calculator App - frontend logic.
// Every calculation is performed by the C++ backend via POST /api/calculate.

const historyEl = document.getElementById("history");
const displayEl = document.getElementById("display");
const statusEl = document.getElementById("status");
const serverStatusEl = document.getElementById("serverStatus");

const OPERATORS = "+-*/%^";
let expression = "";
let justEvaluated = false;
let busy = false;

function pretty(text) {
    return text.replace(/\*/g, "\u00d7").replace(/\//g, "\u00f7").replace(/-/g, "\u2212");
}

function render() {
    displayEl.textContent = expression === "" ? "0" : pretty(expression);
    displayEl.classList.remove("error");
}

function setStatus(message, isError) {
    statusEl.textContent = message || "";
    statusEl.classList.toggle("error", Boolean(isError));
}

function append(char) {
    // Starting a fresh number after a result clears the display first.
    if (justEvaluated && /[0-9.(]/.test(char)) {
        expression = "";
        historyEl.innerHTML = "&nbsp;";
    }
    justEvaluated = false;
    expression += char;
    render();
}

function press(char) {
    if (busy) return;
    const last = expression.slice(-1);

    if (OPERATORS.includes(char)) {
        if (expression === "" && char !== "-") return;   // only a leading minus is allowed
        if (OPERATORS.includes(last)) {
            if (char === "-" && !"+-".includes(last)) {
                append(char);                             // 5 * -3
            } else {
                while (OPERATORS.includes(expression.slice(-1))) {
                    expression = expression.slice(0, -1); // replace the previous operator
                }
                if (expression === "" && char !== "-") return;
                append(char);
            }
            return;
        }
        if (last === "(" && char !== "-") return;         // no operator right after "("
        append(char);
        return;
    }

    if (char === "(") {
        if (/[0-9.)]/.test(last)) append("*");           // 5(  ->  5*(
        append("(");
        return;
    }

    if (char === ")") {
        const opens = (expression.match(/\(/g) || []).length;
        const closes = (expression.match(/\)/g) || []).length;
        if (opens > closes && expression !== "" && !OPERATORS.includes(last) && last !== "(" && last !== ".") {
            append(")");
        }
        return;
    }

    if (char === ".") {
        if (justEvaluated) {
            expression = "";
            historyEl.innerHTML = "&nbsp;";
            justEvaluated = false;
        }
        const currentNumber = expression.match(/[0-9.]*$/)[0];
        if (currentNumber.includes(".")) return;           // one dot per number
        if (currentNumber === "") append("0");
        append(".");
        return;
    }

    append(char); // digit
}

function negate() {
    if (busy) return;
    if (expression === "") { append("-"); return; }
    if (expression === "-") { expression = ""; render(); return; }

    const match = expression.match(/(\d+\.?\d*)$/);
    if (!match) return; // last character is an operator or ")" - nothing to toggle

    const start = expression.length - match[1].length;
    const previous = expression[start - 1];
    const beforePrevious = expression[start - 2];

    if (previous === "-") {
        if (beforePrevious === undefined || OPERATORS.includes(beforePrevious) || beforePrevious === "(") {
            expression = expression.slice(0, start - 1) + expression.slice(start); // -5 -> 5
        } else {
            expression = expression.slice(0, start - 1) + "+" + expression.slice(start); // 5-3 -> 5+3
        }
    } else {
        expression = expression.slice(0, start) + "-" + expression.slice(start); // 5 -> -5
    }
    justEvaluated = false;
    render();
}

function deleteLast() {
    if (busy) return;
    if (justEvaluated) { clearAll(); return; }
    expression = expression.slice(0, -1);
    render();
}

function clearAll() {
    expression = "";
    justEvaluated = false;
    historyEl.innerHTML = "&nbsp;";
    setStatus("");
    render();
}

function formatResult(value) {
    if (!Number.isFinite(value)) return "Error";
    if (Number.isInteger(value)) return value.toLocaleString("en-US");
    return value.toLocaleString("en-US", { maximumFractionDigits: 10 });
}

async function evaluate() {
    if (busy || expression === "") return;
    const trailing = expression.slice(-1);
    if (OPERATORS.includes(trailing) || trailing === "(") {
        setStatus("Expression is incomplete", true);
        return;
    }

    busy = true;
    setStatus("Calculating on the C++ server\u2026");
    try {
        const response = await fetch("/api/calculate", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ expression }),
        });
        const data = await response.json();
        if (!response.ok || data.error) {
            setStatus(data.error || "Invalid expression", true);
            displayEl.classList.add("error");
            return;
        }
        historyEl.textContent = pretty(expression) + " =";
        displayEl.textContent = formatResult(Number(data.result));
        expression = String(data.result); // allow continued calculations
        justEvaluated = true;
        setStatus("");
    } catch (error) {
        setStatus("Cannot reach the C++ backend - is the server running?", true);
        displayEl.classList.add("error");
    } finally {
        busy = false;
    }
}

// Button input
document.querySelectorAll(".key").forEach((button) => {
    button.addEventListener("click", () => {
        if (button.dataset.key !== undefined) {
            press(button.dataset.key);
        } else if (button.dataset.action === "clear") {
            clearAll();
        } else if (button.dataset.action === "delete") {
            deleteLast();
        } else if (button.dataset.action === "negate") {
            negate();
        } else if (button.dataset.action === "equals") {
            evaluate();
        }
    });
});

// Keyboard input
window.addEventListener("keydown", (event) => {
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    const key = event.key;
    if (/^[0-9]$/.test(key) || "+-*/%^().".includes(key)) {
        press(key);
        event.preventDefault();
    } else if (key === "Enter" || key === "=") {
        evaluate();
        event.preventDefault();
    } else if (key === "Backspace") {
        deleteLast();
        event.preventDefault();
    } else if (key === "Escape") {
        clearAll();
        event.preventDefault();
    }
});

// Backend health check on load
(async function checkServer() {
    try {
        const response = await fetch("/api/calculate", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ expression: "1" }),
        });
        serverStatusEl.textContent = response.ok ? "C++ server online" : "server error";
    } catch (error) {
        serverStatusEl.textContent = "server offline";
    }
})();

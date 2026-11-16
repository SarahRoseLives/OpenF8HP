(() => {
  "use strict";

  const keyMap = {
    "0": "0",
    "1": "1",
    "2": "2",
    "3": "3",
    "4": "4",
    "5": "5",
    "6": "6",
    "7": "7",
    "8": "8",
    "9": "9",
    "*": "star",
    "#": "hash",
    ArrowUp: "up",
    ArrowDown: "down",
    Enter: "menu",
    Escape: "exit",
    v: "vm",
    V: "vm",
    a: "ab",
    A: "ab",
    b: "band",
    B: "band",
    s: "sk1",
    S: "sk1",
    l: "sk2",
    L: "sk2",
  };

  let pttHeld = false;
  let heldButton = null;

  function isEditable(target) {
    return (
      target instanceof HTMLElement &&
      (target.matches("input, select, textarea") || target.isContentEditable)
    );
  }

  function sendKey(key) {
    if (!window.htmx || !document.getElementById("lcd")) return;
    window.htmx.ajax("POST", "/key", {
      values: { key },
      target: "#lcd",
      swap: "innerHTML",
    });
  }

  function sendPtt(on) {
    if (!window.htmx || !document.getElementById("lcd")) return;
    if (pttHeld === on) return;
    pttHeld = on;
    window.htmx.ajax("POST", "/ptt", {
      values: { state: on ? "on" : "off" },
      target: "#lcd",
      swap: "innerHTML",
    });
  }

  window.addEventListener("keydown", (event) => {
    if (isEditable(event.target)) return;

    if (event.code === "Space") {
      event.preventDefault();
      if (!event.repeat) sendPtt(true);
      return;
    }

    const key = keyMap[event.key];
    if (!key) return;
    event.preventDefault();
    if (!event.repeat) sendKey(key);
  });

  window.addEventListener("keyup", (event) => {
    if (event.code !== "Space") return;
    event.preventDefault();
    sendPtt(false);
  });

  document.addEventListener("pointerdown", (event) => {
    const button = event.target.closest("[data-ptt]");
    if (!button) return;
    event.preventDefault();
    heldButton = button;
    button.classList.add("held");
    try {
      button.setPointerCapture(event.pointerId);
    } catch (_) {
      /* not fatal */
    }
    sendPtt(true);
  });

  function releasePointer(event) {
    if (!heldButton && !event.target.closest("[data-ptt]")) return;
    if (heldButton) heldButton.classList.remove("held");
    heldButton = null;
    sendPtt(false);
  }

  document.addEventListener("pointerup", releasePointer);
  document.addEventListener("pointercancel", releasePointer);
  window.addEventListener("blur", () => sendPtt(false));
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) sendPtt(false);
  });

  // Keep the little simulator readouts in sync while dragging.
  document.addEventListener("input", (event) => {
    if (!event.target.matches(".slider input")) return;
    const output = event.target.closest(".slider")?.querySelector("output");
    if (output) output.textContent = event.target.value;
  });
})();

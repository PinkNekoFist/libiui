/*
 * libiui WebAssembly JavaScript Glue Code
 *
 * Provides Canvas 2D rendering and event handling for the WASM backend.
 * Pattern inspired by Mado's mado-wasm.js.
 */

/* Canvas rendering module */
const IuiCanvas = {
    canvas: null,
    ctx: null,
    imageData: null,
    width: 0,
    height: 0,
    /* Cached framebuffer pointer for direct memory access */
    fbPtr: 0,
    fbBuffer: null,
    dpr: 1, /* Device pixel ratio */

    /* Initialize Canvas context */
    init: function (width, height) {
        console.log("[IuiCanvas] init() called with " + width + "x" + height);
        if (typeof addConsoleLine === "function") {
            addConsoleLine("Canvas init: " + width + "x" + height, false);
        }

        this.canvas = document.getElementById("iui-canvas");
        if (!this.canvas) {
            /* Create canvas if not found */
            this.canvas = document.createElement("canvas");
            this.canvas.id = "iui-canvas";
            document.body.appendChild(this.canvas);
        }

        this.width = width;
        this.height = height;
        this.dpr = window.devicePixelRatio || 1;

        /* Set canvas size (CSS pixels) */
        this.canvas.style.width = width + "px";
        this.canvas.style.height = height + "px";

        /* Set actual canvas size (physical pixels for HiDPI) */
        this.canvas.width = width * this.dpr;
        this.canvas.height = height * this.dpr;

        this.ctx = this.canvas.getContext("2d");
        this.ctx.scale(this.dpr, this.dpr);

        /* Create ImageData for framebuffer transfer (logical size) */
        this.imageData = this.ctx.createImageData(width, height);

        /* Setup event listeners */
        this.setupEvents();

        console.log("[IuiCanvas] Canvas initialized: " + width + "x" + height + " (DPR: " + this.dpr + ")");
        return true;
    },

    /* Setup event listeners */
    setupEvents: function () {
        var canvas = this.canvas;
        var self = this;

        /* Mouse motion - get fresh bounding rect each event for scroll/resize handling */
        canvas.addEventListener("mousemove", function (e) {
            var rect = canvas.getBoundingClientRect();
            var x = Math.floor(e.clientX - rect.left);
            var y = Math.floor(e.clientY - rect.top);
            var buttons = e.buttons;
            Module._iui_wasm_mouse_motion(x, y, buttons);
        });

        /* Mouse button down */
        canvas.addEventListener("mousedown", function (e) {
            e.preventDefault();
            var rect = canvas.getBoundingClientRect();
            var x = Math.floor(e.clientX - rect.left);
            var y = Math.floor(e.clientY - rect.top);
            Module._iui_wasm_mouse_button(x, y, e.button, 1);
        });

        /* Mouse button up */
        canvas.addEventListener("mouseup", function (e) {
            e.preventDefault();
            var rect = canvas.getBoundingClientRect();
            var x = Math.floor(e.clientX - rect.left);
            var y = Math.floor(e.clientY - rect.top);
            Module._iui_wasm_mouse_button(x, y, e.button, 0);
        });

        /* Mouse wheel */
        canvas.addEventListener("wheel", function (e) {
            Module._iui_wasm_scroll(e.deltaX, e.deltaY);
            e.preventDefault();
        }, { passive: false });

        /* Keyboard events */
        document.addEventListener("keydown", function (e) {
            var key = self.mapKey(e.keyCode, e.key);
            if (key !== 0) {
                Module._iui_wasm_key(key, 1, e.shiftKey ? 1 : 0);
                /* Prevent default for navigation keys */
                if (key >= 1 && key <= 12) e.preventDefault();
            }
        });

        document.addEventListener("keyup", function (e) {
            var key = self.mapKey(e.keyCode, e.key);
            if (key !== 0) {
                Module._iui_wasm_key(key, 0, e.shiftKey ? 1 : 0);
            }
        });

        /* Text input (keypress for printable characters) */
        document.addEventListener("keypress", function (e) {
            if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) {
                Module._iui_wasm_char(e.key.charCodeAt(0));
            }
        });

        /* Prevent context menu on canvas */
        canvas.addEventListener("contextmenu", function (e) {
            e.preventDefault();
        });

        console.log("[libiui] Event handlers initialized");
    },

    /* Map browser keyCode to iui_key_code enum */
    mapKey: function (keyCode, key) {
        switch (keyCode) {
            case 8: return 1;   /* Backspace -> key_backspace */
            case 46: return 2;  /* Delete -> key_delete */
            case 37: return 3;  /* Left -> key_left */
            case 39: return 4;  /* Right -> key_right */
            case 36: return 5;  /* Home -> key_home */
            case 35: return 6;  /* End -> key_end */
            case 13: return 7;  /* Enter -> key_enter */
            case 9: return 8;   /* Tab -> key_tab */
            case 27: return 9;  /* Escape -> key_escape */
            case 38: return 10; /* Up -> key_up */
            case 40: return 11; /* Down -> key_down */
            case 32: return 12; /* Space -> key_space */
            default: return 0;
        }
    },

    /* Set framebuffer pointer from WASM */
    setFramebufferPtr: function (ptr) {
        this.fbPtr = ptr;
        console.log("[IuiCanvas] Framebuffer pointer set: " + ptr);
        if (typeof addConsoleLine === "function") {
            addConsoleLine("Framebuffer ptr: " + ptr, false);
        }
    },

    /* Update Canvas from framebuffer */
    updateCanvas: function () {
        if (!this.fbPtr || !this.ctx || !this.imageData) {
            return;
        }

        /* Access WASM memory - try multiple methods for different Emscripten versions */
        var wasmBuffer = null;

        /* Method 1: Direct HEAPU8 buffer (most common) */
        if (Module.HEAPU8 && Module.HEAPU8.buffer) {
            wasmBuffer = Module.HEAPU8.buffer;
        }
        /* Method 2: wasmMemory object */
        else if (Module.wasmMemory && Module.wasmMemory.buffer) {
            wasmBuffer = Module.wasmMemory.buffer;
        }
        /* Method 3: asm.memory (older Emscripten) */
        else if (Module.asm && Module.asm.memory && Module.asm.memory.buffer) {
            wasmBuffer = Module.asm.memory.buffer;
        }
        /* Method 4: Direct memory export */
        else if (typeof wasmMemory !== "undefined" && wasmMemory.buffer) {
            wasmBuffer = wasmMemory.buffer;
        }

        if (!wasmBuffer) {
            /* Log diagnostic info only once per second to avoid spam */
            if (!this._lastMemError || Date.now() - this._lastMemError > 1000) {
                this._lastMemError = Date.now();
                console.error("[IuiCanvas] Cannot access WASM memory. Available:",
                    "HEAPU8=" + (typeof Module.HEAPU8),
                    "wasmMemory=" + (typeof Module.wasmMemory),
                    "asm=" + (typeof Module.asm));
            }
            return;
        }

        /* Create view into framebuffer (re-create each frame in case memory grows) */
        var pixelCount = this.width * this.height;
        var fb = new Uint32Array(wasmBuffer, this.fbPtr, pixelCount);

        /* Convert ARGB32 to RGBA for Canvas ImageData */
        var data = this.imageData.data;
        for (var i = 0; i < pixelCount; i++) {
            var argb = fb[i];
            var offset = i * 4;
            data[offset + 0] = (argb >> 16) & 0xFF; /* R */
            data[offset + 1] = (argb >> 8) & 0xFF;  /* G */
            data[offset + 2] = argb & 0xFF;         /* B */
            data[offset + 3] = (argb >> 24) & 0xFF; /* A */
        }

        /* For HiDPI: use offscreen canvas at logical size, then draw scaled to main canvas */
        if (this.dpr > 1) {
            /* Create offscreen canvas if needed */
            if (!this.offscreenCanvas) {
                this.offscreenCanvas = document.createElement("canvas");
                this.offscreenCanvas.width = this.width;
                this.offscreenCanvas.height = this.height;
                this.offscreenCtx = this.offscreenCanvas.getContext("2d");
            }
            /* Put image data to offscreen canvas */
            this.offscreenCtx.putImageData(this.imageData, 0, 0);
            /* Draw scaled to main canvas with smoothing for better text quality */
            this.ctx.save();
            this.ctx.setTransform(1, 0, 0, 1, 0, 0);
            /* Enable smoothing for better text rendering on HiDPI */
            this.ctx.imageSmoothingEnabled = true;
            this.ctx.imageSmoothingQuality = "high";
            this.ctx.drawImage(this.offscreenCanvas, 0, 0, this.width, this.height,
                               0, 0, this.canvas.width, this.canvas.height);
            this.ctx.restore();
        } else {
            /* Non-HiDPI: direct putImageData */
            this.ctx.putImageData(this.imageData, 0, 0);
        }
    },

    /* Cleanup */
    destroy: function () {
        this.canvas = null;
        this.ctx = null;
        this.imageData = null;
        this.fbPtr = 0;
        this.fbBuffer = null;
    }
};

/* Emscripten Module configuration */
var Module = Module || {};

/* Tell Emscripten where to find the WASM file */
Module.locateFile = function (path, prefix) {
    console.log("[libiui] locateFile: " + path + " (prefix: " + prefix + ")");
    /* Use provided prefix to support different deployment paths */
    return prefix + path;
};

/* Error handling */
Module.onAbort = function (what) {
    console.error("[libiui] ABORT: " + what);
    if (typeof addConsoleLine === "function") {
        addConsoleLine("ABORT: " + what, true);
    }
};

/* Called when WebAssembly module is ready */
Module.onRuntimeInitialized = function () {
    console.log("[libiui] WebAssembly runtime initialized");

    /* Event handlers are initialized in IuiCanvas.init() called from C code */
    /* Main loop is started by C code via emscripten_set_main_loop_arg */
};

/* Monitor loading progress */
Module.monitorRunDependencies = function (left) {
    console.log("[libiui] Run dependencies remaining: " + left);
};

/* Print output to console */
Module.print = function (text) {
    console.log("[C stdout] " + text);
    if (typeof addConsoleLine === "function") {
        addConsoleLine("[C] " + text, false);
    }
};

/* Print errors to console */
Module.printErr = function (text) {
    console.error("[C stderr] " + text);
    if (typeof addConsoleLine === "function") {
        addConsoleLine("[C err] " + text, true);
    }
};

/* Intercept ALL console.log to capture EM_ASM output */
(function() {
    var originalLog = console.log;
    var originalError = console.error;
    console.log = function() {
        originalLog.apply(console, arguments);
        /* Forward important messages to on-page console */
        var msg = Array.prototype.slice.call(arguments).join(" ");
        if (msg.indexOf("[example]") >= 0 || msg.indexOf("[libiui]") >= 0 || msg.indexOf("[IuiCanvas]") >= 0) {
            if (typeof addConsoleLine === "function") {
                addConsoleLine(msg, false);
            }
        }
    };
    console.error = function() {
        originalError.apply(console, arguments);
        var msg = Array.prototype.slice.call(arguments).join(" ");
        if (typeof addConsoleLine === "function") {
            addConsoleLine("ERROR: " + msg, true);
        }
    };
})();

/* Canvas element reference */
Module.canvas = (function () {
    return document.getElementById("iui-canvas");
})();

/* Global error handler */
window.onerror = function (msg, url, line, col, error) {
    console.error("[iui-wasm.js] ERROR: " + msg + " at " + url + ":" + line + ":" + col);
    if (typeof addConsoleLine === "function") {
        addConsoleLine("ERROR: " + msg, true);
    }
    return false;
};

/* Handle promise rejections */
window.onunhandledrejection = function (event) {
    console.error("[iui-wasm.js] Unhandled rejection: " + event.reason);
    if (typeof addConsoleLine === "function") {
        addConsoleLine("Unhandled rejection: " + event.reason, true);
    }
};

console.log("[iui-wasm.js] Module configured, waiting for libiui_example.js...");
if (typeof addConsoleLine === "function") {
    addConsoleLine("Module configured, loading WASM...", false);
}
